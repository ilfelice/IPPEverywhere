# IPP (network or USB) transport for Haiku

Haiku's bundled IPP transport writes the IPP request-id as a C `long`,
which is 8 bytes on x86_64 instead of the 4 bytes IPP requires. Every
request is therefore malformed and printers answer
`client-error-bad-request`. This transport started as that one with the
integer types fixed; since then:

- IPP version 1.1
- The HTTP exchange is the driver's `IPPClient` (compiled in from the
  parent directory): HTTP/1.1 requests, replies read until complete
  (Content-Length, chunked or close-delimited). Haiku's own HTTP code
  could not decode chunked replies, so until October 2026 the transport
  sent HTTP/1.0, which printers such as the HP Envy refuse with 505.
- IPP over USB (`ipp-usb://` URLs) through `IPPUSB`, also shared with
  the driver
- the CUPS-browsing printer roster removed; the driver asks for the URL
- the temporary job file kept out of the spool folder
- `server-error-busy` retried for up to 10 minutes
- job attributes prepared by the driver (printer folder attribute
  `ipp-everywhere:job-attributes`) sent with the job, with one retry
  without them on a client error

    make -j4
    make install-transport

The transport then appears as "IPP (network or USB)" when adding a printer.
