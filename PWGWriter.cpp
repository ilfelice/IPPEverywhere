/*
 * PWGWriter.cpp
 * PWG Raster (PWG 5102.4) page encoder for the IPP Everywhere printer driver.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "PWGWriter.h"

#include <string.h>


// PWG raster page header layout (byte offsets, all integers big-endian)
static const size_t kHeaderSize = 1796;
static const size_t kOffsetSynopsis = 0;			// "PwgRaster"
static const size_t kOffsetMediaType = 128;
static const size_t kOffsetDuplex = 272;
static const size_t kOffsetHWResolution = 276;
static const size_t kOffsetNumCopies = 340;
static const size_t kOffsetPageSize = 352;
static const size_t kOffsetTumble = 368;
static const size_t kOffsetWidth = 372;
static const size_t kOffsetHeight = 376;
static const size_t kOffsetBitsPerColor = 384;
static const size_t kOffsetBitsPerPixel = 388;
static const size_t kOffsetBytesPerLine = 392;
static const size_t kOffsetColorOrder = 396;
static const size_t kOffsetColorSpace = 400;
static const size_t kOffsetNumColors = 420;
static const size_t kOffsetTotalPageCount = 452;
static const size_t kOffsetCrossFeedTransform = 456;
static const size_t kOffsetFeedTransform = 460;
static const size_t kOffsetImageBoxLeft = 464;
static const size_t kOffsetImageBoxTop = 468;
static const size_t kOffsetImageBoxRight = 472;
static const size_t kOffsetImageBoxBottom = 476;
static const size_t kOffsetAlternatePrimary = 480;
static const size_t kOffsetPrintQuality = 484;
static const size_t kOffsetPageSizeName = 1732;


PWGWriter::PageInfo::PageInfo()
	:
	width(0),
	height(0),
	xres(300),
	yres(300),
	colorSpace(kRGB),
	duplex(false),
	tumble(false),
	backSide(false),
	reversePixels(false),
	reverseLines(false),
	pageWidthPoints(0),
	pageHeightPoints(0),
	quality(0),
	totalPages(0),
	imageLeft(0),
	imageTop(0),
	imageRight(0),
	imageBottom(0)
{
}


PWGWriter::PWGWriter()
	:
	fLines(0)
{
}


uint32
PWGWriter::BytesPerPixel() const
{
	return fInfo.colorSpace == kGray ? 1 : 3;
}


uint32
PWGWriter::BytesPerLine() const
{
	return fInfo.width * BytesPerPixel();
}


void
PWGWriter::BeginPage(const PageInfo& info)
{
	fInfo = info;
	fRuns.clear();
	fLastLine.clear();
	fLines = 0;
	fTemp.resize(BytesPerLine());
}


void
PWGWriter::AddLine(const uint8* pixels)
{
	if (fLines >= fInfo.height)
		return;

	const uint32 bpp = BytesPerPixel();
	const uint32 bytesPerLine = BytesPerLine();
	const uint8* line = pixels;

	if (fInfo.backSide && fInfo.reversePixels) {
		// Mirroring the line here plus reversing the line order in
		// EndPage() gives a 180 degree rotation.
		for (uint32 x = 0; x < fInfo.width; x++) {
			memcpy(&fTemp[x * bpp], pixels + (fInfo.width - 1 - x) * bpp,
				bpp);
		}
		line = &fTemp[0];
	}

	if (!fRuns.empty() && fLastLine.size() == bytesPerLine
		&& fRuns.back().repeat < 255
		&& memcmp(&fLastLine[0], line, bytesPerLine) == 0) {
		fRuns.back().repeat++;
	} else {
		fRuns.push_back(LineRun());
		fRuns.back().repeat = 0;
		_EncodeLine(line, fRuns.back().data);
		fLastLine.assign(line, line + bytesPerLine);
	}
	fLines++;
}


void
PWGWriter::EndPage(std::vector<uint8>& output)
{
	// pad with white if the driver delivered fewer lines than the page has
	if (fLines < fInfo.height) {
		std::vector<uint8> white(BytesPerLine(), 0xff);
		while (fLines < fInfo.height)
			AddLine(&white[0]);
	}

	_WriteHeader(output);

	bool reverse = fInfo.backSide && fInfo.reverseLines;
	size_t count = fRuns.size();
	for (size_t i = 0; i < count; i++) {
		const LineRun& run = reverse ? fRuns[count - 1 - i] : fRuns[i];
		output.push_back(run.repeat);
		output.insert(output.end(), run.data.begin(), run.data.end());
	}

	fRuns.clear();
	fLastLine.clear();
	fLines = 0;
}


void
PWGWriter::_EncodeLine(const uint8* pixels, std::vector<uint8>& out) const
{
	// PWG raster line compression:
	//   byte 0..127   : the following pixel is repeated (byte + 1) times
	//   byte 129..255 : the following (257 - byte) pixels are literal
	//   byte 128      : reserved, never generated
	const uint32 bpp = BytesPerPixel();
	const uint32 width = fInfo.width;
	uint32 x = 0;

	while (x < width) {
		// count identical pixels starting at x
		uint32 run = 1;
		while (x + run < width && run < 128
			&& memcmp(pixels + x * bpp, pixels + (x + run) * bpp, bpp) == 0) {
			run++;
		}

		if (run > 1) {
			out.push_back((uint8)(run - 1));
			out.insert(out.end(), pixels + x * bpp, pixels + (x + 1) * bpp);
			x += run;
			continue;
		}

		// literal run: extend until the next two pixels are identical
		uint32 start = x;
		x++;
		while (x < width && (x - start) < 128) {
			if (x + 1 < width
				&& memcmp(pixels + x * bpp, pixels + (x + 1) * bpp, bpp) == 0)
				break;
			x++;
		}

		uint32 n = x - start;
		if (n == 1)
			out.push_back(0);
		else
			out.push_back((uint8)(257 - n));
		out.insert(out.end(), pixels + start * bpp, pixels + x * bpp);
	}
}


void
PWGWriter::_PutU32(std::vector<uint8>& out, size_t offset, uint32 value)
{
	out[offset + 0] = (uint8)(value >> 24);
	out[offset + 1] = (uint8)(value >> 16);
	out[offset + 2] = (uint8)(value >> 8);
	out[offset + 3] = (uint8)(value);
}


void
PWGWriter::_PutString(std::vector<uint8>& out, size_t offset,
	const std::string& value)
{
	size_t length = value.size();
	if (length > 63)
		length = 63;
	memcpy(&out[offset], value.data(), length);
}


void
PWGWriter::_WriteHeader(std::vector<uint8>& out) const
{
	size_t base = out.size();
	out.resize(base + kHeaderSize, 0);
	std::vector<uint8> header(kHeaderSize, 0);

	_PutString(header, kOffsetSynopsis, "PwgRaster");
	_PutString(header, kOffsetMediaType, fInfo.mediaType);
	_PutString(header, kOffsetPageSizeName, fInfo.pageSizeName);

	_PutU32(header, kOffsetDuplex, fInfo.duplex ? 1 : 0);
	_PutU32(header, kOffsetTumble, fInfo.duplex && fInfo.tumble ? 1 : 0);
	_PutU32(header, kOffsetHWResolution, fInfo.xres);
	_PutU32(header, kOffsetHWResolution + 4, fInfo.yres);
	_PutU32(header, kOffsetNumCopies, 1);
	_PutU32(header, kOffsetPageSize, fInfo.pageWidthPoints);
	_PutU32(header, kOffsetPageSize + 4, fInfo.pageHeightPoints);
	_PutU32(header, kOffsetWidth, fInfo.width);
	_PutU32(header, kOffsetHeight, fInfo.height);
	_PutU32(header, kOffsetBitsPerColor, 8);
	_PutU32(header, kOffsetBitsPerPixel, BytesPerPixel() * 8);
	_PutU32(header, kOffsetBytesPerLine, BytesPerLine());
	_PutU32(header, kOffsetColorOrder, 0);		// chunky
	_PutU32(header, kOffsetColorSpace, fInfo.colorSpace);
	_PutU32(header, kOffsetNumColors, BytesPerPixel());
	_PutU32(header, kOffsetTotalPageCount, fInfo.totalPages);

	bool mirrored = fInfo.backSide && fInfo.reversePixels;
	bool flipped = fInfo.backSide && fInfo.reverseLines;
	_PutU32(header, kOffsetCrossFeedTransform, mirrored ? 0xffffffff : 1);
	_PutU32(header, kOffsetFeedTransform, flipped ? 0xffffffff : 1);

	bool haveBox = fInfo.imageRight > fInfo.imageLeft
		&& fInfo.imageBottom > fInfo.imageTop;
	_PutU32(header, kOffsetImageBoxLeft, haveBox ? fInfo.imageLeft : 0);
	_PutU32(header, kOffsetImageBoxTop, haveBox ? fInfo.imageTop : 0);
	_PutU32(header, kOffsetImageBoxRight,
		haveBox ? fInfo.imageRight : fInfo.width);
	_PutU32(header, kOffsetImageBoxBottom,
		haveBox ? fInfo.imageBottom : fInfo.height);
	_PutU32(header, kOffsetAlternatePrimary, 0x00ffffff);
	_PutU32(header, kOffsetPrintQuality, fInfo.quality);

	memcpy(&out[base], &header[0], kHeaderSize);
}
