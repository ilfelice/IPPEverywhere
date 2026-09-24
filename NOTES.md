# IPP Everywhere driver for Haiku: project notes

Status as of 2026-09-24 (discovery, capability polling, scale to fit,
localization, IPP-USB included). Target printer: Epson EW-M630T series (EW-M630TW),
Haiku R1~beta6+development hrev60082, x86_64.

## 1. Goal and approach

Print from Haiku to the EW-M630T. The printer's native language (ESC/P
raster, ESC/P-R) is undocumented, but the printer supports AirPrint, i.e.
IPP Everywhere: it accepts PWG Raster (and Apple URF and JPEG) over IPP on
port 631. So the driver produces PWG Raster and Haiku's IPP transport
uploads it. Nothing Epson-specific is involved apart from the capability
table (paper sizes, media types, resolutions), which is why the driver is
named after the protocol rather than the printer.

## 2. What the printer reports

Obtained with `ippprobe.py` (a Python script that sends
Get-Printer-Attributes). Endpoint: `ipp://192.168.3.12:631/ipp/print`.

- IPP versions 1.0, 1.1, 2.0; features airprint-1.7, wfds-print-1.0
- document-format: application/octet-stream, image/pwg-raster,
  image/urf, image/jpeg
- PWG raster resolutions 300x300, 600x600; types sgray_8, srgb_8;
  sheet back: rotated
- media: A4 (default), Letter, Legal, A5, A6, B5, B6, Hagaki, 4x6, 5x7,
  8x10, L, 2L, C6, Chou3, Chou4, You1, You3, You4, custom 89x127 mm to
  215.9x1200 mm
- media types: stationery, stationery-inkjet, stationery-coated,
  stationery-letterhead, photographic(-glossy/-high-gloss/-matte),
  com.epson-hagaki(-glossy/-addr), com.epson-matte-business-card,
  envelope, com.epson-business-plain
- sides: one-sided, two-sided-long-edge, two-sided-short-edge
- print-color-mode: color, monochrome, auto (and variants)
- print-quality: 4 (normal), 5 (high); copies 1-99; one paper source

## 3. Project layout

```
IPPEverywhere/
  Makefile            Haiku makefile-engine build; `make install-driver`
  PWGEntry.cpp        driver entry point: name, signature, factories
  PWGCap.cpp/.h       capability table shown in the dialogs, built at
                      runtime from IPPCapabilities
  IPPClient.cpp/.h    minimal IPP client (Get-Printer-Attributes)
  IPPCapabilities.cpp/.h  printer capability model: built from the IPP
                      answer, stored as an attribute of the printer folder
  PWGDriver.cpp/.h    GraphicsDriver subclass: bands -> PWG raster pages
  PWGWriter.cpp/.h    PWG raster header + line compression
  IPPDiscovery.cpp/.h minimal mDNS/DNS-SD browser for _ipp._tcp
  AddIPPPrinterWindow.cpp/.h  dialog shown when the printer is added
  IPPEverywhere.rdef  resources
  libprint/           Haiku's libprint framework, copied from the Haiku
                      source tree (MIT). Not shipped in haiku_devel, so it
                      is compiled into the add-on. One fix applied (5.2).
  IPPTransport/       fixed build of Haiku's IPP transport (see 5.1)
```

Installed locations (`/boot/home/config/non-packaged/add-ons/Print/`):

- `IPP Everywhere` (driver)
- `transport/IPP (fixed)` (transport)

Both are user add-ons; the system ones are untouched.

## 4. How the driver works

libprint does the heavy lifting: it provides the Page Setup and Print
dialogs, reads the spool file, and renders each page into B_RGB32 bitmap
bands, calling `PWGDriver::NextBand()` for each band.

- `PWGCap` declares what the dialogs offer. Since the capability polling
  work it builds its lists from `IPPCapabilities` (see 4.2). Paper rects
  are the full sheet (physical rect = paper rect): PWG raster describes
  the whole sheet and the printer clips to its own margins. Extra job
  settings (media type, quality, duplex edge) use libprint's
  driver-specific capability mechanism, so they appear in the Print
  dialog with no custom UI.
- `kCanRotatePageInLandscape` is not supported on purpose: libprint then
  rotates landscape pages itself and the driver always emits portrait
  pages.
- `kCopyCommand` is not supported: libprint sends the page N times for
  N copies. Simple and works with duplex/collate.
- `PWGDriver::StartPage()` computes the page geometry from the job data
  (swapping to portrait if needed) and starts a page in `PWGWriter`.
- `NextBand()` converts BGRA to RGB or 8 bit gray and feeds lines to the
  writer. `EndPage()` writes the compressed page to the transport.
- `PWGWriter` buffers a page as compressed line runs (identical adjacent
  lines are merged). The 1796 byte header is filled per PWG 5102.4.
  For duplex, back sides are transformed according to the printer's
  `pwg-raster-document-sheet-back` with the same rules as CUPS'
  rastertopwg (rotated: 180 degrees for long-edge; flipped: mirror for
  short edge, vertical flip for long edge; manual-tumble: 180 degrees for
  short edge), and the Feed/CrossFeed transform header fields are set to
  -1 accordingly. The EW-M630T reports "rotated".
- The file starts with the `RaS2` magic; there is no trailer.
- The transport sends `document-format: application/octet-stream`; the
  printer auto-detects PWG raster from the magic.

### 4.1 Adding a printer: discovery

`PWGPrinterDriver::AddPrinter()` runs when print_server has created the
printer folder. If the printer's transport is an IPP one (attribute
`transport` contains "IPP"), it shows `AddIPPPrinterWindow`: a list of
printers found on the network, a "Search again" button and a URL field.
The chosen URL is written to the `transport_address` attribute, which is
where the IPP transport reads it, so the transport never has to ask.
For other transports (USB...) nothing is shown.

`IPPDiscovery::Browse()` is a self-contained mDNS browser: it binds UDP
5353 (falling back to an ephemeral port with the QU bit if 5353 is taken,
e.g. by mdnsd), joins 224.0.0.251, sends a PTR query for
`_ipp._tcp.local`, and collects PTR/SRV/TXT/A records for about 3 s,
sending follow-up SRV/TXT/A queries half way if responders left them out.
The URL is `ipp://<IPv4>:<port>/<rp>`; the IP is used rather than the
.local hostname because Haiku cannot resolve .local names. Tested with a
crafted responder (compression pointers, staggered records); needs testing
against the real printer.

### 4.2 Capability polling

The driver asks the printer what it can do and builds the dialogs from
the answer, so it works with any IPP Everywhere printer.

- `IPPClient::GetPrinterAttributes()` sends Get-Printer-Attributes over
  HTTP/1.1 with plain sockets (non-blocking connect with timeout, chunked
  responses decoded) and parses the reply into `IPPAttributes`.
  Collections (`media-col-database` etc.) are skipped.
- `IPPCapabilities::SetFrom()` turns the attributes into a model:
  media sizes (parsed from PWG self-describing names such as
  `iso_a4_210x297mm`; `custom_*` and `roll_*` skipped; labels from a table
  with a "Name (W x H unit)" fallback), media types (labels from a table,
  vendor keywords like `com.epson-...` become "Epson: ..."), resolutions
  (`pwg-raster-document-resolution-supported`, square only, else
  `printer-resolution-supported`), color/gray (`pwg-raster-document-type-
  supported`), duplex and sheet-back, print qualities, and whether
  `image/pwg-raster` is accepted.
- The model is stored as a flattened BMessage in the attribute
  `ipp-everywhere:capabilities` of the printer folder (a file there would
  show up as a job in the Printers preflet). The raw attribute dump is
  kept in `ipp-everywhere:attributes` for troubleshooting
  (`catattr ipp-everywhere:attributes <printer folder>`).
- `PWGCap::Update()` decides when to ask: cache missing, generic (the
  printer never answered), older than a day, or forced. Failed attempts
  are not repeated for 10 minutes (`ipp-everywhere:last-query`), so a
  switched-off printer costs at most one 3 s delay per 10 minutes. It is
  called from the `PWGCap` constructor (i.e. before every dialog and every
  job) and, forced, from `AddPrinter()` right after the URL is chosen.
- `PWGCap::_Build()` creates libprint capability objects from the model.
  Sizes libprint has an enum for get that id (a table maps PWG names to
  `JobData::Paper`); others get `kUserDefined + hash(name)`, which is
  stable across sessions. Duplex, grayscale, quality and duplex-edge
  menus only appear when the printer supports them.
- If the printer cannot be reached and nothing is cached,
  `IPPCapabilities::SetDefaults()` gives A4/Letter/Legal/A5, 300 dpi,
  color and grayscale, simplex.

### 4.3 Scale to fit page

A "Scale to fit page" checkbox in the Page Setup dialog under
"Scale [%]" (libprint: capability `PrinterCap::kScaleToFit`, job data
field `JJJJ_scale_to_fit`, shown when the driver supports it). Applications draw to the paper size
Page Setup gave them, but some overflow it (StyledEdit prints at window
width, BePDF at the PDF's own size). The driver cannot know the extent of
the drawing from the spool file, so `GraphicsDriver::_MeasureContent()`
(added to the bundled libprint) renders the page a first time, small and
unclipped, into the band bitmap and finds the bounds of what was drawn
(`get_valid_rect`). If that lies outside the printable area,
`_PrintPage()` renders the page again with an extra scale factor
(shrink only, never enlarge) and moves the content's top left corner to
the printable area's top left corner. Only for one page per sheet; N-up
is left alone. Costs one extra render per page, no extra memory. The
measuring pass sees content up to one page beyond each edge.

### 4.4 Job attributes and the rest of the printer's answer

Everything the dialogs offer now comes from the printer; nothing about
the printer is fixed in the driver any more (the remaining tables are
only English labels for IPP keywords, with a generic fallback).

- Paper source: filled from `media-source-supported`; the chosen source
  is sent with the job.
- Hardware margins: from `media-*-margin-supported` (smallest non-zero
  value per edge, the largest of the four used on all edges, so it does
  not depend on orientation). The paper's imageable area is the sheet
  minus that margin, so Page Setup cannot place content where the printer
  cannot print. The driver pads the rendered bands back to the full sheet
  and reports the imageable area in the raster header's ImageBox.
- Copies: if `copies-supported` allows it, the printer makes the copies
  (`PrinterCap::kCopyCommand`), and if it can collate
  (`multiple-document-handling-supported`), it collates too
  (`kCollateCommand`, new in libprint: `GraphicsDriver` then no longer
  repeats the document for collated copies). Otherwise libprint repeats
  pages as before.
- Booklet is offered whenever the printer does duplex.
- Non-square resolutions (600x1200) are still left out: libprint stores
  one dpi pair but renders with a single scale factor, so it cannot
  produce them.

The job's settings are sent to the printer as proper IPP job attributes
rather than only in the raster header: `copies`,
`multiple-document-handling`, `sides`, `print-quality`,
`print-color-mode` and `media-col` (size, source, type).
`PWGDriver::_WriteJobAttributes()` encodes them and leaves the bytes in
the printer folder attribute `ipp-everywhere:job-attributes`; the IPP
transport (which only knows the printer folder) sends them as the job
attributes group of the Print-Job request and removes the attribute. If
the printer answers with a client error, the transport retries once
without them. This side channel is needed because libprint's transport
interface carries only the document bytes.

### 4.5 Localization

All user-visible strings go through Haiku's Locale Kit (`B_TRANSLATE`),
in the driver's own files and in the bundled libprint dialogs. Each file
has its own translation context (the class name). The paper size, media
type and paper source labels are stored in the capability cache in
English and translated when the dialogs are built
(`IPPCapabilities::Translate()`, `B_TRANSLATE_MARK` in the tables), so a
language change does not need a new query of the printer. libprint's
static halftone tables are left untranslated (static initializers run
before a catalog can be loaded; halftoning is not used by this driver).

Workflow: `make catkeys` writes `locales/en.catkeys`; a translation is a
copy of that file named `locales/<lang>.catkeys` with the last column
filled in and the language added to `LOCALES` in the Makefile;
`make install-driver` binds the catalogs into the add-on
(`make bindcatalogs`), so nothing else needs installing. The catalog is
found through the add-on's signature (`application/x-vnd.ipp-everywhere`
in the rdef).

### 4.6 IPP over USB

The EW-M630T's classic USB printer interface (class 7/1/2, the one
Haiku's usb_printer driver claims and the "USB Port" transport uses)
only understands ESC/P: sending PWG raster there prints garbage. But the
printer also implements IPP-USB (USB-IF, 2012): interfaces 1, 3, 4 and 5
each have an alternate setting with class 7/1/4 and a bulk in/out pair,
over which the very same HTTP/IPP exchange as on the network runs.

`IPPUSB` (shared by the driver and the transport, the transport compiles
`../IPPUSB.cpp`) uses Haiku's USB Kit (libdevice) directly:
- `Enumerate()` scans `/dev/bus/usb` for devices with such an interface;
  the discovery dialog lists them as "<name> (USB)" ahead of the network
  printers. Their URL is `ipp-usb://<vendor>:<product>/<serial>`.
- `Exchange()` opens the device, picks an IPP-USB interface whose default
  alternate is not the classic printer interface (to stay clear of the
  kernel driver), selects the alternate setting, writes the HTTP request
  to the bulk out endpoint and reads the bulk in endpoint until the HTTP
  message is complete (Content-Length or chunked; there is no
  "connection close" on USB, so framing has to be exact). Requests are
  addressed to `ipp://localhost:60000/ipp/print`, as the ipp-usb daemon
  on Linux does.
- `IPPClient::GetPrinterAttributes()` and the transport's Print-Job both
  branch on `IPPUSB::IsUSBURL()`; everything above the byte pipe is the
  same code. So "IPP (fixed)" is the transport for USB as well.

Caveat: the USB Kit's bulk transfers have no timeout, so a printer that
never answers would block the job.

## 5. Haiku bugs found on the way

### 5.1 IPP transport is broken on x86_64

`src/add-ons/print/transports/ipp/IppContent.*` uses C `long` for the IPP
request-id and integer attributes. On x86_64 that is 8 bytes, IPP needs
4, so every request is malformed and printers answer
`client-error-bad-request`. Fixed in `IPPTransport/` by changing the
types to `int32`/`uint32`. While there:

- IPP version bumped from 1.0 to 1.1
- HTTP request changed from 1.1 to 1.0 (the transport cannot decode
  chunked responses)
- CUPS-broadcast printer roster removed; `list_transport_ports()` is
  deliberately absent (see 5.3)
- temporary job file moved from the printer's spool folder to the system
  temp directory (it showed up as a "??? pages / Unknown status" job in
  the Printers preflet)
- `server-error-busy` (printer still working on a previous job) is
  retried every 3 s for up to 10 minutes instead of failing

Should be reported upstream.

### 5.2 libprint "all pages" convention

libprint's Print dialog stores `last_page = -1` for "all pages". Apps
follow BeOS convention and expect `INT32_MAX` (which the Preview driver
uses); with -1, BePDF and other apps loop over zero pages and BPrintJob
reports "No pages to print!". Fixed in `libprint/JobSetupDlg.cpp` (two
lines). Affects every libprint-based driver in Haiku (PCL5, PCL6,
PostScript...). Should be reported upstream.

### 5.3 libprint's "Pages per sheet" never worked

`JobSetupView::GetID()` in `src/libs/print/libprint/JobSetupDlg.cpp`
walks the capability array without ever advancing the pointer, so it
always compares the chosen label with the first entry and returns the
default (1) for anything else. N-up printing has therefore never worked
in any libprint driver. Fixed in the bundled copy (one line). Should be
reported upstream.

### 5.4 Printers preferences: status text grows

`PrinterItem::UpdatePendingJobs()` in
`src/preferences/printers/PrinterListView.cpp` calls
`BStringFormat::Format(fPendingJobs, count)` without clearing
`fPendingJobs` first, and `Format()` appends to the string it is given.
Every printer event (printer added, print_server restarted, job added,
job status changed, job removed) therefore appends another
"N pending jobs" to the line, which ends up as
"No pending jobsNo pending jobs1 pending job..." (truncated with "..."
when the column is narrow, so the status looks frozen). Verified with a
Preview/Print To File printer, i.e. it is independent of this driver.
Workaround: close and reopen the Printers window. Not fixable from a
driver.

### 5.5 Printers preferences special-cases the name "IPP"

`AddPrinterDialog::_FillTransportMenu()` gives a transport a plain menu
entry if it has no port list, but if it *does* export
`list_transport_ports()` and returns nothing, it shows a "No printer
found!" submenu, except when the transport is literally named "IPP".
Our transport therefore does not export `list_transport_ports()`.

## 6. Test status

Working: text (StyledEdit), images (ShowImage, WonderBrush), PDF (BePDF),
email client (plain and HTML), landscape, duplex long edge, queueing a
second job while the first prints, network discovery when adding the
printer.

Capability polling: works against the real printer. Page Setup shows all
sizes the printer reported; a Hagaki-sized page (a size that only exists
because of polling) printed at the right size and position. Media types
other than plain are untested (only A4 plain paper at hand).

Not our bugs: StyledEdit clips text wider than the printable area (it
prints at window width; narrow the window). BePDF's "Print settings" tool
window stays open after printing by design.

Scale to fit page: works (StyledEdit, HTML email).

Job attributes, printer-side copies, hardware margins, paper source
menu, pages per sheet: work. Booklet: untested.

Localization: works (Japanese catalog).

IPP-USB: works (printer added from the USB entry in the discovery
dialog, capability query and printing over interface 3).

Untested: 600 dpi, grayscale, short-edge duplex, media types other than
plain, quality "High", envelopes, copies > 1, Pages per sheet.

Not working: Pe (fails silently; not investigated yet).

## 7. Ideas and to-do

- Discovery: also browse `_ipps._tcp`; IPv6 (AAAA) once Haiku needs it.
- URF-only printers (no `image/pwg-raster`): needs an Apple Raster
  encoder; `IPPCapabilities::pwgRaster` already records the situation.
- Custom paper sizes from the printer's `custom_min`/`custom_max` range.
- Make transport failures mark the job as failed instead of leaving it
  in "Processing".
- Package both add-ons as an .hpkg; submit to HaikuPorts.
- The Haiku issues in section 5 are documented here only; upstream
  reports are not planned.

## 8. Build and install (quick reference)

```
cd /boot/home/projects/IPPEverywhere
make -j4 && make install-driver
cd IPPTransport
make -j4 && make install-transport
hey print_server quit
```

Then Preferences > Printers > Add: driver "IPP Everywhere", transport
"IPP (fixed)". A dialog lists the IPP printers found on the network;
pick one or type the URL.
