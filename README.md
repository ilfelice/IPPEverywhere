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

## Features

- PWG/URF raster (user selectable)
- IPP over network or USB (IPP-USB)
- Printer auto-discovery

## Installation

1) Download latest hpkg package from:

[https://github.com/ilfelice/IPPEverywhere/releases]https://github.com/ilfelice/IPPEverywhere/releases

2) Double-click to install and restart the print_daemon.

## Adding a printer

1. Preferences > Printers > Add.
2. Name it, choose driver "IPP Everywhere" and transport "IPP (network or USB)"
   (for USB as well).
3. A dialog lists the IPP printers found on your network and the ones
   plugged in by USB. Pick yours, or type the URL by hand (for the
   EW-M630T: `ipp://<printer address>:631/ipp/print`). You can also choose
   the raster format (PWG or URF) from the `Raster format` menu.

## Bug reports

Report bugs at:

[https://github.com/ilfelice/IPPEverywhere/issues]https://github.com/ilfelice/IPPEverywhere/issues

## AI disclaimer

This software was created using AI.
