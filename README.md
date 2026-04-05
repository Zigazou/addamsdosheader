addamsdosheader
===============

Prepare a Basic or binary file to be written on an AMSDOS formatted floppy disk.

If AMSDOS does not find the appropriate header, the file is considered to be
ASCII Basic.

If the file cannot be found, it prints an error message and exits with a return
code of 1.

Compile
-------

    make

Running tests
-------------

    make tests

Warning
-------

**This program overwrites the source file!**

Usage
-----

    addamsdosheader [-f] <file> binary <load-address> <entry-point>
    addamsdosheader [-f] <file> basic

Notes:

- **-f**: force rewriting of AMSDOS header (it fails if an AMSDOS header already
  exists)
- **file**: the path to the file to be modified
- **type**: basic or binary
- **load-address**: the address at which the file must be loaded in memory (it
  must be written in hexadecimal!). 0170 for Basic programs.
- **entry-point**: the address of the entry point, for binary files which can
  be run directly without requiring a Basic loader program. 0000 for Basic
  programs.
