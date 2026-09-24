/*
 * PWGDriver.cpp
 * IPP Everywhere (PWG Raster) printer driver for Haiku.
 *
 * libprint renders every page into RGB32 bitmap bands and hands them to
 * NextBand(). This driver converts the bands to 8 bit gray or RGB, compresses
 * them as PWG Raster and writes the result to the transport (normally IPP).
 *
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "PWGDriver.h"

#include <Alert.h>
#include <Bitmap.h>
#include <Node.h>

#include <algorithm>
#include <stdio.h>
#include <string.h>

#include "JobData.h"
#include "PWGCap.h"
#include "PrinterData.h"

#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PWGDriver"


PWGDriver::PWGDriver(BMessage* message, PrinterData* printerData,
	const PrinterCap* printerCap)
	:
	GraphicsDriver(message, printerData, printerCap),
	fPageIndex(0),
	fPageWidth(0),
	fPageHeight(0),
	fContentWidth(0),
	fContentHeight(0),
	fPadX(0),
	fPadY(0)
{
}


const PWGCap*
PWGDriver::_Cap() const
{
	return dynamic_cast<const PWGCap*>(GetPrinterCap());
}


bool
PWGDriver::_UseColor() const
{
	const PWGCap* cap = _Cap();
	if (cap != NULL) {
		if (!cap->Capabilities().color)
			return false;
		if (!cap->Capabilities().gray)
			return true;
	}
	return GetJobData()->GetColor() != JobData::kMonochrome;
}


std::string
PWGDriver::_Setting(const char* key, const char* defaultValue) const
{
	const DriverSpecificSettings& settings = GetJobData()->Settings();
	if (settings.HasString(key))
		return settings.GetString(key);
	return defaultValue;
}


// IPP attribute encoding helpers for the job attributes group
static void
PutU16(std::vector<uint8>& out, uint16 value)
{
	out.push_back((uint8)(value >> 8));
	out.push_back((uint8)value);
}


static void
PutValue(std::vector<uint8>& out, uint8 tag, const char* name,
	const std::string& value)
{
	out.push_back(tag);
	PutU16(out, strlen(name));
	out.insert(out.end(), name, name + strlen(name));
	PutU16(out, value.size());
	out.insert(out.end(), value.begin(), value.end());
}


static void
PutInteger(std::vector<uint8>& out, uint8 tag, const char* name, int32 value)
{
	out.push_back(tag);
	PutU16(out, strlen(name));
	out.insert(out.end(), name, name + strlen(name));
	PutU16(out, 4);
	out.push_back((uint8)(value >> 24));
	out.push_back((uint8)(value >> 16));
	out.push_back((uint8)(value >> 8));
	out.push_back((uint8)value);
}


// collection member: memberAttrName followed by the value with empty name
static void
PutMemberName(std::vector<uint8>& out, const char* member)
{
	PutValue(out, 0x4a, "", member);
}


void
PWGDriver::_WriteJobAttributes()
{
	const PWGCap* cap = _Cap();
	if (cap == NULL)
		return;
	const IPPCapabilities& caps = cap->Capabilities();
	const JobData* job = GetJobData();

	std::vector<uint8> attrs;

	if (cap->Supports(PrinterCap::kCopyCommand)) {
		int copies = job->GetCopies();
		if (copies < 1)
			copies = 1;
		if (copies > caps.maxCopies)
			copies = caps.maxCopies;
		PutInteger(attrs, 0x21, "copies", copies);
		if (caps.collate && copies > 1) {
			PutValue(attrs, 0x44, "multiple-document-handling",
				job->GetCollate() ? "separate-documents-collated-copies"
					: "separate-documents-uncollated-copies");
		}
	}

	if (caps.duplex) {
		const char* sides = "one-sided";
		if (job->GetPrintStyle() != JobData::kSimplex) {
			bool shortEdge = caps.duplexShortEdge
				&& _Setting(PWGCap::kDuplexEdgeKey, "long-edge") == "short-edge";
			sides = shortEdge ? "two-sided-short-edge" : "two-sided-long-edge";
		}
		PutValue(attrs, 0x44, "sides", sides);
	}

	if (!caps.qualities.empty()) {
		std::string quality = _Setting(PWGCap::kQualityKey, "normal");
		PutInteger(attrs, 0x23, "print-quality",
			quality == "high" ? 5 : quality == "draft" ? 3 : 4);
	}

	if (caps.colorMode)
		PutValue(attrs, 0x44, "print-color-mode",
			_UseColor() ? "color" : "monochrome");

	const IPPMediaSize* size = cap->MediaSize(job->GetPaper());
	if (size != NULL) {
		PutValue(attrs, 0x34, "media-col", "");		// begCollection
		PutMemberName(attrs, "media-size");
		PutValue(attrs, 0x34, "", "");
		PutMemberName(attrs, "x-dimension");
		PutInteger(attrs, 0x21, "",
			(int32)(size->widthPoints * 2540.0f / 72.0f + 0.5f));
		PutMemberName(attrs, "y-dimension");
		PutInteger(attrs, 0x21, "",
			(int32)(size->heightPoints * 2540.0f / 72.0f + 0.5f));
		PutValue(attrs, 0x37, "", "");					// endCollection

		std::string source = cap->MediaSourceKeyword(job->GetPaperSource());
		if (!source.empty()) {
			PutMemberName(attrs, "media-source");
			PutValue(attrs, 0x44, "", source);
		}
		if (!caps.mediaTypes.empty()) {
			PutMemberName(attrs, "media-type");
			PutValue(attrs, 0x44, "", _Setting(PWGCap::kMediaTypeKey,
				"stationery"));
		}
		PutValue(attrs, 0x37, "", "");					// endCollection
	}

	if (attrs.empty())
		return;

	// left on the printer folder for the IPP transport, which sends them
	// as the job's attributes and removes the attribute again
	std::string path;
	if (!GetPrinterData()->GetPath(path))
		return;
	BNode node(path.c_str());
	if (node.InitCheck() != B_OK)
		return;
	node.WriteAttr("ipp-everywhere:job-attributes", B_RAW_TYPE, 0, &attrs[0],
		attrs.size());
}


// A one-line description of the job on the printer folder, for
// troubleshooting: catattr ipp-everywhere:last-job <printer folder>
void
PWGDriver::_WriteJobSummary()
{
	const JobData* job = GetJobData();
	std::string path;
	if (!GetPrinterData()->GetPath(path))
		return;
	BNode node(path.c_str());
	if (node.InitCheck() != B_OK)
		return;
	BRect paper = job->GetPaperRect();
	BRect physical = job->GetPhysicalRect();
	BRect printable = job->GetPrintableRect();
	char text[512];
	snprintf(text, sizeof(text),
		"pages %ld, nup %d, copies %d, collate %d, orientation %s, "
		"style %d, paper %d, %dx%d dpi, scaling %g, fit %d, "
		"paper rect %gx%g, physical %g,%g-%g,%g, printable %g,%g-%g,%g",
		(long)GetPageCount(), (int)job->GetNup(), (int)job->GetCopies(),
		(int)job->GetCollate(),
		job->GetOrientation() == JobData::kLandscape ? "landscape" : "portrait",
		(int)job->GetPrintStyle(), (int)job->GetPaper(), (int)job->GetXres(),
		(int)job->GetYres(), job->GetScaling(), (int)job->GetScaleToFit(),
		paper.Width(), paper.Height(), physical.left, physical.top,
		physical.right, physical.bottom, printable.left, printable.top,
		printable.right, printable.bottom);
	node.WriteAttr("ipp-everywhere:last-job", B_STRING_TYPE, 0, text,
		strlen(text) + 1);
}


bool
PWGDriver::StartDocument()
{
	try {
		fPageIndex = 0;
		_WriteJobSummary();
		_WriteJobAttributes();
		WriteSpoolData(PWGWriter::FileMagic(), PWGWriter::FileMagicSize());
		return true;
	} catch (TransportException& err) {
		return false;
	}
}


bool
PWGDriver::StartPage(int page)
{
	const JobData* job = GetJobData();
	const PWGCap* cap = _Cap();
	static const IPPCapabilities kGeneric;
	const IPPCapabilities& caps = cap != NULL ? cap->Capabilities() : kGeneric;

	// libprint always delivers portrait bands (it rotates landscape pages
	// itself, see PWGCap::Supports(kCanRotatePageInLandscape)), so describe
	// the page in portrait orientation whatever the job says. The bands
	// cover the imageable area (physical rect); the raster describes the
	// whole sheet (paper rect), so the bands are padded with the hardware
	// margin, which is the same on all edges.
	BRect paper = job->GetPaperRect();
	BRect physical = job->GetPhysicalRect();
	uint32 widthPoints = paper.IntegerWidth();
	uint32 heightPoints = paper.IntegerHeight();
	uint32 xres = job->GetXres();
	uint32 yres = job->GetYres();
	uint32 width = (widthPoints * xres + 71) / 72;
	uint32 height = (heightPoints * yres + 71) / 72;
	uint32 contentWidth = (physical.IntegerWidth() * xres + 71) / 72;
	uint32 contentHeight = (physical.IntegerHeight() * yres + 71) / 72;
	uint32 margin = (uint32)((physical.left - paper.left) * xres / 72.0f
		+ 0.5f);

	// The sheet and the imageable area are made portrait independently:
	// libprint rotates the imageable rect but not the paper rect for
	// 2/8/32 pages per sheet, so their orientations can differ.
	if (width > height) {
		std::swap(width, height);
		std::swap(widthPoints, heightPoints);
		std::swap(xres, yres);
	}
	if (contentWidth > contentHeight)
		std::swap(contentWidth, contentHeight);
	if (contentWidth + 2 * margin > width)
		contentWidth = width > 2 * margin ? width - 2 * margin : width;
	if (contentHeight + 2 * margin > height)
		contentHeight = height > 2 * margin ? height - 2 * margin : height;

	fPageWidth = width;
	fPageHeight = height;
	fContentWidth = contentWidth;
	fContentHeight = contentHeight;
	fPadX = margin;
	fPadY = margin;

	PWGWriter::PageInfo info;
	info.width = width;
	info.height = height;
	info.xres = xres;
	info.yres = yres;
	info.colorSpace = _UseColor() ? PWGWriter::kRGB : PWGWriter::kGray;
	info.duplex = caps.duplex && job->GetPrintStyle() != JobData::kSimplex;
	info.tumble = info.duplex && caps.duplexShortEdge
		&& _Setting(PWGCap::kDuplexEdgeKey, "long-edge") == "short-edge";
	info.backSide = info.duplex && (fPageIndex % 2) == 1;

	// How the printer wants back sides oriented
	// (pwg-raster-document-sheet-back), same rules as CUPS' rastertopwg.
	info.reversePixels = false;
	info.reverseLines = false;
	switch (caps.sheetBack) {
		case IPPCapabilities::kRotated:
			if (!info.tumble)
				info.reversePixels = info.reverseLines = true;
			break;
		case IPPCapabilities::kFlipped:
			if (info.tumble)
				info.reversePixels = true;
			else
				info.reverseLines = true;
			break;
		case IPPCapabilities::kManualTumble:
			if (info.tumble)
				info.reversePixels = info.reverseLines = true;
			break;
		default:
			break;
	}

	info.pageWidthPoints = widthPoints;
	info.pageHeightPoints = heightPoints;
	info.mediaType = _Setting(PWGCap::kMediaTypeKey, "stationery");
	info.pageSizeName = cap != NULL ? cap->MediaSizeName(job->GetPaper()) : "";

	info.quality = 0;
	if (!caps.qualities.empty()) {
		std::string quality = _Setting(PWGCap::kQualityKey, "normal");
		info.quality = quality == "high" ? 5 : quality == "draft" ? 3 : 4;
	}
	info.totalPages = 0;
	info.imageLeft = fPadX;
	info.imageTop = fPadY;
	info.imageRight = fPadX + fContentWidth;
	info.imageBottom = fPadY + fContentHeight;

	fWriter.BeginPage(info);
	fLineBuffer.assign(fWriter.BytesPerLine(), 0xff);

	// top margin
	for (uint32 i = 0; i < fPadY; i++)
		fWriter.AddLine(&fLineBuffer[0]);
	return true;
}


void
PWGDriver::_ConvertLine(const uint8* source, int sourceWidth,
	uint8* target) const
{
	// source: B_RGB32 little endian, i.e. bytes are B, G, R, A
	int width = std::min((uint32)sourceWidth, fContentWidth);
	if (_UseColor()) {
		for (int x = 0; x < width; x++) {
			target[0] = source[2];
			target[1] = source[1];
			target[2] = source[0];
			target += 3;
			source += 4;
		}
	} else {
		for (int x = 0; x < width; x++) {
			uint32 gray = source[2] * 299 + source[1] * 587 + source[0] * 114;
			*target++ = (uint8)(gray / 1000);
			source += 4;
		}
	}
	// anything beyond the band width stays white (buffer is prefilled)
}


bool
PWGDriver::NextBand(BBitmap* bitmap, BPoint* offset)
{
	int y = (int)offset->y;
	int bandWidth = bitmap->Bounds().IntegerWidth() + 1;
	int bandHeight = bitmap->Bounds().IntegerHeight() + 1;
	int pageHeight = GetPageHeight();

	int lines = std::min(bandHeight, pageHeight - y);
	lines = std::min(lines, (int)fContentHeight - y);
	const uint8* bits = (const uint8*)bitmap->Bits();
	int bytesPerRow = bitmap->BytesPerRow();
	uint32 bpp = fWriter.BytesPerPixel();

	for (int line = 0; line < lines; line++) {
		std::fill(fLineBuffer.begin(), fLineBuffer.end(), 0xff);
		_ConvertLine(bits + line * bytesPerRow, bandWidth,
			&fLineBuffer[fPadX * bpp]);
		fWriter.AddLine(&fLineBuffer[0]);
	}

	if (y + bandHeight >= pageHeight) {
		// last band of the page
		offset->x = -1.0;
		offset->y = -1.0;
	} else
		offset->y += bandHeight;

	return true;
}


bool
PWGDriver::EndPage(int page)
{
	try {
		fPageBuffer.clear();
		fWriter.EndPage(fPageBuffer);
		WriteSpoolData(&fPageBuffer[0], fPageBuffer.size());
		fPageIndex++;
		return true;
	} catch (TransportException& err) {
		BAlert* alert = new BAlert("", err.What(), B_TRANSLATE("OK"));
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
		return false;
	}
}


bool
PWGDriver::EndDocument(bool success)
{
	// PWG raster has no trailer; the transport sends the job when it is
	// destroyed by libprint.
	return true;
}
