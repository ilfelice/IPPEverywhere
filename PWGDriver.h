/*
 * PWGDriver.h
 * IPP Everywhere (PWG Raster) printer driver for Haiku.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */
#ifndef PWG_DRIVER_H
#define PWG_DRIVER_H


#include "GraphicsDriver.h"
#include "PWGWriter.h"

#include <vector>


class PWGCap;


class PWGDriver : public GraphicsDriver {
public:
								PWGDriver(BMessage* message,
									PrinterData* printerData,
									const PrinterCap* printerCap);

protected:
	virtual	bool				StartDocument();
	virtual	bool				StartPage(int page);
	virtual	bool				NextBand(BBitmap* bitmap, BPoint* offset);
	virtual	bool				EndPage(int page);
	virtual	bool				EndDocument(bool success);

private:
			const PWGCap*		_Cap() const;
			bool				_UseColor() const;
			std::string			_Setting(const char* key,
									const char* defaultValue) const;
			void				_ConvertLine(const uint8* source,
									int sourceWidth, uint8* target) const;
			void				_WriteJobAttributes();
			void				_WriteJobSummary();

			PWGWriter			fWriter;
			std::vector<uint8>	fLineBuffer;
			std::vector<uint8>	fPageBuffer;
			int					fPageIndex;
			uint32				fPageWidth;		// sheet, pixels, portrait
			uint32				fPageHeight;
			uint32				fContentWidth;	// imageable area, pixels
			uint32				fContentHeight;
			uint32				fPadX;			// imageable area offset
			uint32				fPadY;
};


#endif // PWG_DRIVER_H
