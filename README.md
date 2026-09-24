# IPP Everywhere printer driver for Haiku

Prints to any IPP Everywhere / AirPrint printer by sending PWG Raster
over IPP, on the network or over USB (IPP-USB). Written for and tested
with the Epson EW-M630T series.

The driver is built on Haiku's libprint framework, so it offers the
standard Page Setup dialog (paper size, orientation, resolution, margins)
and Print dialog (copies, page range, duplex, color), plus media type,
quality and duplex binding edge. What the dialogs offer comes from the
printer itself: the driver asks it (IPP Get-Printer-Attributes) when it
is added and refreshes the answer automatically.

It needs the fixed IPP transport in `IPPTransport/`; Haiku's bundled one
does not work on x86_64. See `NOTES.md` for the details and the design.

## Build and install

    cd IPPEverywhere
    make -j4
    make install-driver
    cd IPPTransport
    make -j4
    make install-transport
    hey print_server quit

Both add-ons install under `/boot/home/config/non-packaged/add-ons/Print/`.
The system add-ons are not touched.

## Add the printer

1. Preferences > Printers > Add.
2. Name it, choose driver "IPP Everywhere" and transport "IPP (fixed)"
   (for USB as well).
3. A dialog lists the IPP printers found on your network and the ones
   plugged in by USB. Pick yours, or type the URL by hand (for the
   EW-M630T: `ipp://<printer address>:631/ipp/print`).

## Translating

    make catkeys

writes the English strings to `locales/en.catkeys`. Copy it to
`locales/ja.catkeys` (or another language code), fill in the last column
of each line, add the language to `LOCALES` in the Makefile, and run
`make install-driver` again.

## Files

- `PWGEntry.cpp`  - driver entry point (name, signature, factories)
- `PWGCap.cpp`    - capability table: paper sizes, resolutions, duplex,
                    color, media type, quality, built from the printer's
                    answer
- `IPPClient.cpp` - asks the printer for its attributes
- `IPPUSB.cpp`    - IPP over USB (also used by the transport)
- `IPPCapabilities.cpp` - the printer capability model and its storage
- `PWGDriver.cpp` - takes the rendered bands, converts them, writes pages
- `PWGWriter.cpp` - PWG Raster header + line compression
- `IPPDiscovery.cpp` - finds IPP printers on the network (mDNS)
- `AddIPPPrinterWindow.cpp` - the dialog shown when adding a printer
- `libprint/`     - Haiku's printer driver framework (MIT), with the
                    "all pages" fix described in NOTES.md
- `IPPTransport/` - fixed IPP transport
- `NOTES.md`      - design notes, Haiku bugs found, test status, to-do
