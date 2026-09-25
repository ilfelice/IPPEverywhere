# IPP Everywhere for Haiku: test build

This package adds a printer driver, "IPP Everywhere", and a transport,
"IPP (fixed)", to Haiku. Together they print to any printer that supports
IPP Everywhere or AirPrint, over the network or over USB. Most printers
sold since about 2015 do; the printer's spec sheet or its network status
page will say "AirPrint" or "IPP Everywhere".

Developed and tested with an Epson EW-M630T. Other printers are exactly
what this test build is for.

## Install

1. Double-click the .hpkg file, or run
   `pkgman install ipp_everywhere-<version>-<arch>.hpkg`.
2. In Terminal, run `hey print_server quit` (or reboot).

## Add the printer

1. Preferences > Printers > Add.
2. Give it a name. Printer type: "IPP Everywhere". Connected to:
   "IPP (fixed)" (also for USB; do not use "USB Port").
3. A dialog lists the printers found on your network and on USB. Pick
   yours and click OK. If it is not listed, type its URL in the field;
   for most printers it is `ipp://<printer IP address>:631/ipp/print`.

The driver then asks the printer what it can do (paper sizes, media
types, resolutions, duplex...) and builds its dialogs from the answer.

## What to test

Print from a few applications (StyledEdit, ShowImage, BePDF, your mail
client...) and check:

- Text and images are the right size and in the right place on the page.
- File > Page setup: paper sizes, orientation (try landscape), resolution,
  "Scale to fit page".
- The Print dialog: copies (with and without "Collate"), page range,
  Duplex if your printer has it, "Pages per sheet", color/grayscale, and
  the media type and quality menus at the bottom.
- Printing while the printer is still busy with a previous job (it should
  wait, not fail).
- If your printer has a USB port: add it a second time over USB and print.

## Known limitations

- Photo paper, envelopes and other media types are selected in the Print
  dialog; whether the printer honours them depends on the printer.
- Printers that only accept Apple's URF raster format, not PWG raster
  (some older AirPrint models), are not supported yet.
- Pe fails to print (silently). Being looked at.
- The Printers preferences may show the status of a printer as
  "No pending jobsNo pending jobs1 pending job...". That is a Haiku bug,
  not the driver's; closing and reopening the window clears it.

## Reporting

Please report problems at

    https://github.com/ilfelice/IPPEverywhere/issues

with:

- The printer's make and model, and whether it was connected over
  network or USB.
- What you printed from, and what you expected versus what came out (a
  photo of the page helps).
- The output of these two Terminal commands (replace "Epson" with the
  name you gave the printer):

      catattr ipp-everywhere:attributes /boot/home/config/settings/printers/Epson
      catattr ipp-everywhere:last-job /boot/home/config/settings/printers/Epson

  The first is everything the printer told the driver; the second is the
  driver's summary of the last job.
- If print_server crashed, the crash report Haiku offers to save.

## Uninstall

    pkgman uninstall ipp_everywhere
