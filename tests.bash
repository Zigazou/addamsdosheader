#!/usr/bin/env bash
#
# @file tests.bash
# @brief Integration tests for addamsdosheader.
#
# Verifies the addamsdosheader utility by exercising the binary file-type
# workflow: adding a header, recovering the original payload by trimming
# the first 128 bytes, rejecting a file that already has a header, and
# force-replacing an existing header with the -f flag.
#
# Prerequisites:
#   - ./addamsdosheader must be compiled and present in the current directory.
#   - md5sum must be available.
#
# Usage:
#   bash tests.bash
#
# Exit status:
#   0 - All tests passed.
#   1 - One or more tests failed, or a prerequisite is missing.

set -euo pipefail

# @brief Remove the temporary directory on exit.
# @global TMPDIR Path to the temporary working directory.
function cleanup() {
    if [[ -v TMPDIR && -d "$TMPDIR" ]]
    then
        rm --force --recursive "$TMPDIR"
    fi
}

trap cleanup EXIT

# @brief Record and display a passing test result.
# @param $1 printf format string describing the result.
# @param ... Optional printf arguments.
# @global PASS Incremented by one.
function pass() {
    local template="$1"
    shift

    PASS=$((PASS + 1))
    printf "    PASS: $template\n" "$@"
}

# @brief Record and display a failing test result.
# @param $1 printf format string describing the failure.
# @param ... Optional printf arguments.
# @global FAIL Incremented by one.
function fail() {
    local template="$1"
    shift

    FAIL=$((FAIL + 1))
    printf "    FAIL: $template\n" "$@"
}

# @brief Print a section header for a test case.
# @param $1 Test number.
# @param $2 Short description of the test.
function title() {
    local test_num="$1"
    local description="$2"

    printf "=== Test %d: %s ===\n" "$test_num" "$description"
}

# @brief Generate a random 256-byte binary payload used as test input.
# @stdout Path to the created payload file.
# @global TMPDIR Directory where the payload is written.
function generate_payload() {
    local payload_path="$TMPDIR/payload.bin"

    dd \
        if=/dev/random \
        bs=1 \
        count=256 \
        of="$TMPDIR/payload.bin" \
        2>/dev/null

    printf "%s" "$payload_path"
}

# @brief Print the size of a file in bytes.
# @param $1 Path to the file.
# @stdout File size in bytes.
function get_file_size() {
    local file="$1"

    stat --format=%s "$file"
}

# @brief Print the MD5 hash of a file.
# @param $1 Path to the file.
# @stdout 32-character hexadecimal MD5 digest.
function get_file_md5() {
    local file="$1"

    md5sum "$file" \
        | cut --delimiter=' ' --fields=1
}

# @brief Verify that required external tools are available.
# Exits with status 1 if md5sum or ./addamsdosheader is missing.
function check_prerequisites() {
    if ! command -v md5sum &>/dev/null
    then
        echo "md5sum not found, exiting"
        exit 1
    fi

    if [[ ! -x ./addamsdosheader ]]
    then
        echo "addamsdosheader not found, exiting"
        exit 1
    fi
}

# @brief Test that adding an AMSDOS header increases the file size by 128 bytes.
# @global PAYLOAD_PATH  Path to the reference payload.
# @global EXPECTED_SIZE  Expected size after header insertion.
function test_add_header() {
    local result_size

    title 1 "Add AMSDOS header to a binary file"

    cp "$PAYLOAD_PATH" "$TMPDIR/test1.bin"

    ./addamsdosheader "$TMPDIR/test1.bin" binary C000 C000

    result_size=$(get_file_size "$TMPDIR/test1.bin")
    if [[ "$result_size" -eq "$EXPECTED_SIZE" ]]
    then
        pass "Output size is original + 128 bytes (%d)" "$result_size"
    else
        fail "Expected size %d, got %d" "$EXPECTED_SIZE" "$result_size"
    fi
}

# @brief Test that stripping the first 128 bytes yields the original file.
# @global PAYLOAD_PATH  Path to the reference payload.
# @global PAYLOAD_MD5   MD5 of the original payload for comparison.
test_trim_recovers_payload() {
    local recovered_md5

    title 2 "Trimming first 128 bytes recovers original payload"

    cp "$PAYLOAD_PATH" "$TMPDIR/test2.bin"

    ./addamsdosheader "$TMPDIR/test2.bin" binary C000 C000

    dd \
        if="$TMPDIR/test2.bin" \
        bs=1 \
        skip=128 \
        of="$TMPDIR/recovered2.bin" \
        2>/dev/null

    recovered_md5=$(get_file_md5 "$TMPDIR/recovered2.bin")
    if [[ "$recovered_md5" == "$PAYLOAD_MD5" ]]
    then
        pass "Recovered file matches original payload"
    else
        fail "Recovered file differs (expected %s, got %s)" \
            "$PAYLOAD_MD5" \
            "$recovered_md5"
    fi
}

# @brief Test that a second header insertion is rejected without -f.
# @global PAYLOAD_PATH Path to the reference payload.
test_reject_existing_header() {
    title 3 "Reject file that already has an AMSDOS header"

    cp "$PAYLOAD_PATH" "$TMPDIR/test3.bin"
    ./addamsdosheader "$TMPDIR/test3.bin" binary C000 C000

    if ./addamsdosheader "$TMPDIR/test3.bin" binary C000 C000 2>/dev/null
    then
        fail "Should have rejected file with existing header"
    else
        pass "Correctly rejected file with existing AMSDOS header"
    fi

    cp "$PAYLOAD_PATH" "$TMPDIR/test3.bin"
    ./addamsdosheader "$TMPDIR/test3.bin" basic

    if ./addamsdosheader "$TMPDIR/test3.bin" basic 2>/dev/null
    then
        fail "Should have rejected file with existing header"
    else
        pass "Correctly rejected file with existing AMSDOS header"
    fi
}

# @brief Test that -f replaces an existing header without growing the file.
# @global PAYLOAD_PATH  Path to the reference payload.
# @global EXPECTED_SIZE  Expected size (original + 128).
test_force_replace_header() {
    local result_size

    title 4 "Force-replace existing AMSDOS header with -f"

    cp "$PAYLOAD_PATH" "$TMPDIR/test4.bin"
    ./addamsdosheader "$TMPDIR/test4.bin" binary C000 C000
    ./addamsdosheader -f "$TMPDIR/test4.bin" binary A000 A000
    result_size=$(get_file_size "$TMPDIR/test4.bin")
    if [[ "$result_size" -eq "$EXPECTED_SIZE" ]]; then
        pass "Size unchanged after forced header replacement (%d)" \
            "$result_size"
    else
        fail "Expected size %d after force, got %d" \
            "$EXPECTED_SIZE" \
            "$result_size"
    fi

    cp "$PAYLOAD_PATH" "$TMPDIR/test4.bin"
    ./addamsdosheader "$TMPDIR/test4.bin" basic
    ./addamsdosheader -f "$TMPDIR/test4.bin" basic
    result_size=$(get_file_size "$TMPDIR/test4.bin")
    if [[ "$result_size" -eq "$EXPECTED_SIZE" ]]; then
        pass "Size unchanged after forced header replacement (%d)" \
            "$result_size"
    else
        fail "Expected size %d after force, got %d" \
            "$EXPECTED_SIZE" \
            "$result_size"
    fi

}

# @brief Test that the payload is unchanged after a forced header replacement.
# @global PAYLOAD_PATH  Path to the reference payload.
# @global PAYLOAD_MD5   MD5 of the original payload for comparison.
test_payload_intact_after_force() {
    local recovered_md5

    title 5 "Payload intact after forced header replacement"

    cp "$PAYLOAD_PATH" "$TMPDIR/test5.bin"

    ./addamsdosheader "$TMPDIR/test5.bin" binary C000 C000
    ./addamsdosheader -f "$TMPDIR/test5.bin" binary A000 A000
    dd \
        if="$TMPDIR/test5.bin" \
        bs=1 \
        skip=128 \
        of="$TMPDIR/recovered5.bin" \
        2>/dev/null

    recovered_md5=$(get_file_md5 "$TMPDIR/recovered5.bin")
    if [[ "$recovered_md5" == "$PAYLOAD_MD5" ]]; then
        pass "Payload still matches original after -f replacement"
    else
        fail "Payload differs after -f (expected %s, got %s)" \
            "$PAYLOAD_MD5" \
            "$recovered_md5"
    fi

    cp "$PAYLOAD_PATH" "$TMPDIR/test5.bin"

    ./addamsdosheader "$TMPDIR/test5.bin" basic
    ./addamsdosheader -f "$TMPDIR/test5.bin" basic
    dd \
        if="$TMPDIR/test5.bin" \
        bs=1 \
        skip=128 \
        of="$TMPDIR/recovered5.bin" \
        2>/dev/null

    recovered_md5=$(get_file_md5 "$TMPDIR/recovered5.bin")
    if [[ "$recovered_md5" == "$PAYLOAD_MD5" ]]; then
        pass "Payload still matches original after -f replacement"
    else
        fail "Payload differs after -f (expected %s, got %s)" \
            "$PAYLOAD_MD5" \
            "$recovered_md5"
    fi
}

# @brief Test that invoking the program with an incorrect number of arguments
# results in an error for each combination of file type and force option.
function test_invalid_arguments_count() {
    title 6 "Invalid argument count is rejected"

    if ./addamsdosheader "$TMPDIR/test6.bin" basic C000 C000 2>/dev/null >/dev/null
    then
        fail "Should have rejected basic with extra arguments"
    else
        pass "Correctly rejected basic with extra arguments"
    fi

    if ./addamsdosheader -f "$TMPDIR/test6.bin" basic C000 C000 2>/dev/null >/dev/null
    then
        fail "Should have rejected basic -f with extra arguments"
    else
        pass "Correctly rejected basic -f with extra arguments"
    fi

    if ./addamsdosheader "$TMPDIR/test6.bin" binary C000 2>/dev/null >/dev/null
    then
        fail "Should have rejected binary with missing argument"
    else
        pass "Correctly rejected binary with missing argument"
    fi

    if ./addamsdosheader -f "$TMPDIR/test6.bin" binary C000 2>/dev/null >/dev/null
    then
        fail "Should have rejected binary -f with missing argument"
    else
        pass "Correctly rejected binary -f with missing argument"
    fi
}

# @brief Prepare the testing environment by creating a temporary directory and
# generating a reference payload. Sets global variables for the payload path,
# size, MD5 hash, and expected size after header insertion.
function prepare_environment() {
    TMPDIR="$(mktemp -d)"
    PASS=0
    FAIL=0
    PAYLOAD_PATH=$(generate_payload)
    PAYLOAD_SIZE=$(get_file_size "$PAYLOAD_PATH")
    PAYLOAD_MD5=$(get_file_md5 "$PAYLOAD_PATH")
    EXPECTED_SIZE=$((PAYLOAD_SIZE + 128))
}

# @brief Main function to run all tests and report results.
function run_all_tests() {
    test_add_header
    test_trim_recovers_payload
    test_reject_existing_header
    test_force_replace_header
    test_payload_intact_after_force
    test_invalid_arguments_count

    printf "\nResults: %d passed, %d failed\n" "$PASS" "$FAIL"
    [[ "$FAIL" -eq 0 ]]
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]
then
    check_prerequisites
    prepare_environment
    run_all_tests
fi