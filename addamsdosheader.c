/**
 * @file addamsdosheader.c
 * @brief Prepend an AMSDOS header to a Basic or binary file.
 *
 * This utility prepares a file so it can be written on an AMSDOS-formatted
 * floppy disk. Without a valid AMSDOS header, the Amstrad CPC firmware treats
 * a file as ASCII Basic. The program reads an existing file, builds the
 * appropriate 128-byte AMSDOS header, and overwrites the original file with
 * the header followed by the original content.
 *
 * @warning This program overwrites the source file.
 *
 * Usage:
 * @code
 *   addamsdosheader <file> <type> <load-address> <entry-point>
 * @endcode
 *
 * Compile:
 * @code
 *   gcc -O2 -Wall -Wextra addamsdosheader.c -o addamsdosheader
 * @endcode
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* bool */
#include <stdbool.h>

/* strdup, memset */
#include <string.h>

/* basename */
#include <libgen.h>

/* toupper */
#include <ctype.h>

/* Shorthand for GCC's __attribute__((__packed__)). When applied to a struct, it
tells the compiler to lay out fields with no padding bytes between them, so the
struct's in-memory layout matches the exact binary format of the AMSDOS header
on disk (128 bytes). Without it, the compiler might insert alignment padding
that would break the byte-for-byte correspondence with the Amstrad CPC disk
format. */
#define PACKED __attribute__((__packed__))

#define ERROR_INCORRECT_ARG_NUMBER 1
#define ERROR_AMSDOS_HEADER_PRESENT 2

#define FILE_TYPE_BASIC 0
#define FILE_TYPE_BINARY 2

#define ARG_FILE_PATH 1
#define ARG_FILE_TYPE 2
#define ARG_FILE_START 3
#define ARG_FILE_ENTRY 4
#define ARG_NUMBER 5

/**
 * @brief AMSDOS filename record.
 *
 * Stores the user number, 8-character name, 3-character extension, and
 * padding as found in the AMSDOS directory entry.
 */
typedef struct PACKED {
    uint8_t user_number;       /**< CP/M user number (usually 0). */
    char name[8];              /**< Filename, padded with spaces. */
    char extension[3];         /**< Extension, padded with spaces. */
    uint8_t _filename_padding[4]; /**< Reserved padding bytes. */
} filename;

/**
 * @brief Media-level header portion of the AMSDOS header.
 *
 * Contains file metadata such as the type, load address, entry point,
 * and logical length, along with block bookkeeping fields.
 */
typedef struct PACKED {
    filename file;             /**< Filename record. */
    uint8_t block_number;      /**< Sequential block number. */
    uint8_t last_block;        /**< Last block flag. */
    uint8_t file_type;         /**< File type (0 = Basic, 2 = binary). */
    uint16_t data_length;      /**< Length of the data area. */
    uint16_t data_location;    /**< Memory address where data is loaded. */
    uint8_t first_block;       /**< First block flag. */
    uint16_t logical_length;   /**< Logical length of the file. */
    uint16_t entry_address;    /**< Execution entry point address. */
    uint8_t _media_header_padding[36]; /**< Reserved padding bytes. */
} media_header;

/**
 * @brief Complete 128-byte AMSDOS header.
 *
 * Wraps the media header and appends a 3-byte file length, a 16-bit
 * checksum, and padding to fill the 128-byte sector.
 */
typedef struct PACKED {
    media_header fields;       /**< Media header fields. */
    uint8_t file_length[3];    /**< 24-bit file length (little-endian). */
    uint16_t checksum;         /**< Sum of the first 67 bytes. */
    uint8_t _amsdos_header_padding[59]; /**< Padding to 128 bytes. */
} amsdos_header;

/**
 * @brief In-memory representation of a file with its AMSDOS header.
 */
typedef struct {
    char * filepath;           /**< Full path to the file on disk. */
    char * filename;           /**< Base name extracted from the path. */
    long size;                 /**< Size of the original file content in bytes. */
    amsdos_header header;      /**< AMSDOS header to prepend. */
    uint8_t *content;          /**< Raw file content. */
} amsdos_file;

/**
 * @brief Load a file from disk into an amsdos_file structure.
 *
 * Opens the file in binary mode, reads its entire contents into a
 * heap-allocated buffer, and records its path, base name, and size.
 *
 * @param file_path Path to the file to load.
 * @return Pointer to a newly allocated amsdos_file, or NULL on failure.
 */
amsdos_file * load_file(char *file_path) {
    FILE *file;
    amsdos_file *content;

    /* Try opening the file first; return NULL if it fails */
    file = fopen(file_path, "rb");
    if(file == NULL) return NULL;

    content = malloc(sizeof(amsdos_file));

    content->filepath = strdup(file_path);
    content->filename = strdup(basename(file_path));

    fseek(file, 0, SEEK_END);
    content->size = ftell(file);
    fseek(file, 0, SEEK_SET);

    content->content = malloc(content->size);
    fread(content->content, content->size, 1, file);
    fclose(file);

    return content;
}

/**
 * @brief Convert a type string to an AMSDOS file-type constant.
 *
 * Recognises "basic" (case-insensitive prefix "ba") and treats
 * everything else as binary.
 *
 * @param string User-supplied type string (e.g. "basic" or "binary").
 * @return FILE_TYPE_BASIC (0) or FILE_TYPE_BINARY (2).
 */
uint8_t string2filetype(const char *string) {
    if(string[0] != 'b' && string[0] != 'B') return FILE_TYPE_BINARY;
    if(string[1] == 'a' || string[1] == 'A') return FILE_TYPE_BASIC;
    return FILE_TYPE_BINARY;
}

/**
 * @brief Parse a hexadecimal string into a 16-bit word.
 *
 * Reads up to four hexadecimal digits from @p string.
 *
 * @param string Null-terminated hexadecimal string (e.g. "C000").
 * @return Parsed 16-bit value, or 0 if parsing fails.
 */
uint16_t string2word(const char *string) {
    unsigned int word = 0;
    sscanf(string, "%4x", &word);
    return (uint16_t) word;
}

/**
 * @brief Compute the AMSDOS header checksum.
 *
 * The checksum is the arithmetic sum of the first 67 bytes of the
 * header, stored as a 16-bit value.
 *
 * @param header Pointer to the AMSDOS header.
 * @return Computed checksum value.
 */
uint16_t compute_checksum(const amsdos_header *header) {
    uint8_t *raw = NULL;
    uint16_t checksum = 0;
    int i;

    raw = (uint8_t *)header;

    for(i = 0; i < 67; i++) checksum += raw[i];

    return checksum;
}

/**
 * @brief Test whether a file already contains a valid AMSDOS header.
 *
 * A valid header is detected when the file is larger than 128 bytes and
 * the checksum of the first 67 bytes matches the stored checksum field.
 *
 * @param file Pointer to the loaded amsdos_file.
 * @return true if a valid AMSDOS header is present, false otherwise.
 */
bool has_amsdos_header(const amsdos_file *file) {
    amsdos_header *header;
    uint16_t checksum;

    if(file->size <= (long)sizeof(amsdos_header)) return false;

    header = (amsdos_header *)(file->content);
    checksum = compute_checksum(header);

    return checksum == header->checksum;
}

/**
 * @brief Print command-line usage information to stdout.
 */
void print_usage() {
    printf("Usage: addamsdosheader <filename> <type> <start> <entry>\n");
    printf("- type: basic or binary\n");
    printf("- start: hexadecimal address at which the file will be loaded\n");
    printf("- entry: hexadecimal address of the entry point (binary)\n");
}

/**
 * @brief Copy a string to a destination buffer, converting to uppercase.
 *
 * Copying stops at the first null terminator, dot character, or when
 * @p maxlen characters have been copied, whichever comes first.
 *
 * @param src   Source string.
 * @param dst   Destination buffer (must be at least @p maxlen bytes).
 * @param maxlen Maximum number of characters to copy.
 */
void copy_string_toupper(const char *src, char *dst, int maxlen) {
    int i; 
    
    for(i = 0; src[i] != '\0' && src[i] != '.' && i < maxlen; i++) {
        dst[i] = toupper(src[i]);
    }
}

/**
 * @brief Initialise the AMSDOS header for the given file.
 *
 * Fills the header structure with the filename (uppercased, space-padded),
 * file type, load address, entry point, file size, and checksum.
 *
 * @param file  Pointer to the amsdos_file whose header will be initialised.
 * @param type  AMSDOS file type (FILE_TYPE_BASIC or FILE_TYPE_BINARY).
 * @param start Memory address where the file should be loaded.
 * @param entry Execution entry point address.
 */
void init_header(amsdos_file *file, uint8_t type, uint16_t start, uint16_t entry) {
    char *extension;

    memset((void *)(&file->header), 0, sizeof(amsdos_header));

    /* Initialize file name and extension */
    memset((void *)(&file->header.fields.file.name), 0x20, 8);
    memset((void *)(&file->header.fields.file.extension), 0x20, 3);

    copy_string_toupper(file->filename, file->header.fields.file.name, 8);

    extension = strchr(file->filename, '.');
    if(extension != NULL) {
        copy_string_toupper(extension + 1, file->header.fields.file.extension, 3);
    }

    /* Initialize file type and memory */
    file->header.fields.file_type = type;
    file->header.fields.data_location = start;
    file->header.fields.logical_length = file->size;
    file->header.fields.entry_address = entry;

    /* Initialize file size */    
    file->header.file_length[0] = file->size & 0xFF;
    file->header.file_length[1] = (file->size >> 8) & 0xFF;
    file->header.file_length[2] = (file->size >> 16) & 0xFF;

    /* Compute checksum */
    file->header.checksum = compute_checksum(&file->header);
}

/**
 * @brief Write the AMSDOS header followed by the file content to disk.
 *
 * Overwrites the original file at @c amsfile->filepath.
 *
 * @param amsfile Pointer to the amsdos_file to write.
 */
void write_file(const amsdos_file *amsfile) {
    FILE *file;

    file = fopen(amsfile->filepath, "wb");
    fwrite(&amsfile->header, 1, sizeof(amsdos_header), file);
    fwrite(amsfile->content, amsfile->size, 1, file);
    fclose(file);
}

/**
 * @brief Program entry point.
 *
 * Parses command-line arguments, loads the target file, verifies that it
 * does not already contain an AMSDOS header, builds one, and writes the
 * result back to disk.
 *
 * @param argc Argument count (expected: 5).
 * @param argv Argument vector: program, filepath, type, start, entry.
 * @return 0 on success, non-zero error code on failure.
 */
int main(int argc, char **argv) {
    amsdos_file *file;
    uint8_t file_type;
    uint16_t start;
    uint16_t entry;

    /* Check argument bumber */
    if(argc != ARG_NUMBER) {
        printf("Incorrect number of arguments\n");
        print_usage();
        return ERROR_INCORRECT_ARG_NUMBER;
    }

    /* Analyze arguments */
    file = load_file(argv[ARG_FILE_PATH]);
    if(file == NULL) {
        printf("Error: cannot open file '%s'\n", argv[ARG_FILE_PATH]);
        return 1;
    }

    file_type = string2filetype(argv[ARG_FILE_TYPE]);    
    start = string2word(argv[ARG_FILE_START]);
    entry = string2word(argv[ARG_FILE_ENTRY]);

    /* Check for AMSDOS header presence */
    if(has_amsdos_header(file)) {
        printf("File already has AMSDOS header\n");
        return ERROR_AMSDOS_HEADER_PRESENT;
    }

    /* Initialize the header */
    init_header(file, file_type, start, entry);
    
    /* Write file with the AMSDOS header */
    write_file(file);

    return 0;
}
