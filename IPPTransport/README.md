# Fixed IPP transport for Haiku (x86_64)

Haiku's bundled IPP transport writes the IPP request-id as a C `long`,
which is 8 bytes on x86_64 instead of the 4 bytes IPP requires. Every
request is therefore malformed and printers answer
`client-error-bad-request`. This is the same transport with the integer
types fixed (`long` -> `int32`/`uint32`), IPP version bumped to 1.1, and
the unused CUPS-browsing printer roster removed.

    make -j4
    make install-transport

The transport then appears as "IPP (fixed)" when adding a printer.
