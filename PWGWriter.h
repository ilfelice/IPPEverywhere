/*
 * PWGWriter.h
 * PWG Raster (PWG 5102.4) page encoder for the IPP Everywhere printer driver.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */
#ifndef PWG_WRITER_H
#define PWG_WRITER_H


#include <SupportDefs.h>

#include <string>
#include <vector>


class PWGWriter {
public:
	enum ColorSpace {
		kGray = 18,		// sgray_8
		kRGB = 19		// srgb_8
	};

	struct PageInfo {
		uint32		width;				// pixels
		uint32		height;				// pixels
		uint32		xres;				// dpi
		uint32		yres;				// dpi
		ColorSpace	colorSpace;
		bool		duplex;
		bool		tumble;				// true = short edge binding
		bool		backSide;			// true = this is the back of a sheet
		// transforms applied to back sides only (see the printer's
		// pwg-raster-document-sheet-back); both = rotate by 180 degrees
		bool		reversePixels;		// mirror each line (cross feed)
		bool		reverseLines;		// bottom to top (feed)
		uint32		pageWidthPoints;
		uint32		pageHeightPoints;
		std::string	mediaType;			// IPP media-type keyword
		std::string	pageSizeName;		// PWG media size name
		uint32		quality;			// 0 = default, 3..5 = IPP print-quality
		uint32		totalPages;			// 0 = unknown
		// imageable area in pixels (ImageBox); all zero = whole page
		uint32		imageLeft;
		uint32		imageTop;
		uint32		imageRight;
		uint32		imageBottom;

		PageInfo();
	};

								PWGWriter();

	static	const char*			FileMagic() { return "RaS2"; }
	static	uint32				FileMagicSize() { return 4; }

			void				BeginPage(const PageInfo& info);
			// pixels: width * BytesPerPixel() bytes, top to bottom order
			void				AddLine(const uint8* pixels);
			// Appends the page header and compressed data to output.
			void				EndPage(std::vector<uint8>& output);

			uint32				BytesPerPixel() const;
			uint32				BytesPerLine() const;

private:
	struct LineRun {
		std::vector<uint8>		data;
		uint8					repeat;		// 0 = line appears once
	};

			void				_EncodeLine(const uint8* pixels,
									std::vector<uint8>& out) const;
			void				_WriteHeader(std::vector<uint8>& out) const;

	static	void				_PutU32(std::vector<uint8>& out, size_t offset,
									uint32 value);
	static	void				_PutString(std::vector<uint8>& out,
									size_t offset, const std::string& value);

			PageInfo			fInfo;
			std::vector<LineRun>	fRuns;
			std::vector<uint8>	fLastLine;
			std::vector<uint8>	fTemp;
			uint32				fLines;
};


#endif // PWG_WRITER_H
