/*
 * PWGCap.h
 * Capabilities of the IPP Everywhere (PWG Raster) printer driver, built at
 * runtime from what the printer reported (see IPPCapabilities).
 * Copyright 2026. Distributed under the terms of the MIT License.
 */
#ifndef PWG_CAP_H
#define PWG_CAP_H


#include "IPPCapabilities.h"
#include "PrinterCap.h"

#include <vector>


class BNode;


class PWGCap : public PrinterCap {
public:
	// driver specific setting categories
	enum {
		kMediaType = kDriverSpecificCapabilitiesBegin,
		kQuality,
		kDuplexEdge
	};

	// setting keys (stored in the job settings)
	static const char* kMediaTypeKey;
	static const char* kQualityKey;
	static const char* kDuplexEdgeKey;

								PWGCap(const PrinterData* printerData);
	virtual						~PWGCap();

	virtual	int					CountCap(CapID category) const;
	virtual	bool				Supports(CapID category) const;
	virtual	const BaseCap**		GetCaps(CapID category) const;

			const IPPCapabilities&	Capabilities() const
									{ return fCapabilities; }
	// PWG media size name ("iso_a4_210x297mm") for a paper id, and the
	// size itself (NULL if unknown)
			const char*			MediaSizeName(int paper) const;
			const IPPMediaSize*	MediaSize(int paper) const;
	// IPP media-source keyword for a paper source id ("" if unknown)
			const char*			MediaSourceKeyword(int source) const;

	// Loads the cached capabilities from the printer folder and asks the
	// printer again when the cache is missing, generic, stale, or `force`
	// is set. Saves the result. Returns whether the printer answered.
	static	bool				Update(BNode& printerFolder,
									IPPCapabilities& capabilities,
									bool force);

private:
			void				_Build();
			void				_Clear();
	static	int32				_PaperID(const std::string& pwgName);

			IPPCapabilities		fCapabilities;

			std::vector<PaperCap*>			fPapers;
			std::vector<PaperSourceCap*>	fSources;
			std::vector<ResolutionCap*>		fResolutions;
			std::vector<PrintStyleCap*>		fPrintStyles;
			std::vector<ColorCap*>			fColors;
			std::vector<DriverSpecificCap*>	fDriverCaps;
			std::vector<ListItemCap*>		fMediaTypes;
			std::vector<ListItemCap*>		fQualities;
			std::vector<ListItemCap*>		fDuplexEdges;

			// the same objects as const BaseCap* arrays for GetCaps()
			std::vector<const BaseCap*>		fPaperArray;
			std::vector<const BaseCap*>		fSourceArray;
			std::vector<const BaseCap*>		fResolutionArray;
			std::vector<const BaseCap*>		fPrintStyleArray;
			std::vector<const BaseCap*>		fColorArray;
			std::vector<const BaseCap*>		fDriverCapArray;
			std::vector<const BaseCap*>		fMediaTypeArray;
			std::vector<const BaseCap*>		fQualityArray;
			std::vector<const BaseCap*>		fDuplexEdgeArray;
};


#endif // PWG_CAP_H
