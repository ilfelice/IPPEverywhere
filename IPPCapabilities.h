/*
 * IPPCapabilities.h
 * What a printer can do, derived from its IPP attributes, and stored as an
 * attribute of the printer folder so it survives between sessions.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */
#ifndef IPP_CAPABILITIES_H
#define IPP_CAPABILITIES_H


#include <OS.h>

#include <string>
#include <vector>


class BMessage;
class BNode;
class IPPAttributes;


struct IPPMediaSize {
	std::string	pwgName;		// "iso_a4_210x297mm"
	std::string	label;			// "A4"
	float		widthPoints;	// portrait
	float		heightPoints;
	bool		isDefault;
};


struct IPPKeyword {
	std::string	keyword;		// "photographic-glossy"
	std::string	label;			// "Photo paper (glossy)"
	bool		isDefault;
};


struct IPPResolution {
	int			x;
	int			y;
	bool		isDefault;
};


class IPPCapabilities {
public:
	enum SheetBack {
		kNormal,
		kRotated,
		kFlipped,
		kManualTumble
	};

								IPPCapabilities();

	// generic set used when the printer cannot be asked
			void				SetDefaults();
	// fills everything from a Get-Printer-Attributes response
			void				SetFrom(const IPPAttributes& attributes);

			void				ToMessage(BMessage& message) const;
			bool				FromMessage(const BMessage& message);

	// attribute on the printer folder node
			status_t			Save(BNode& node) const;
			status_t			Load(BNode& node);

	static	const char*			kAttributeName;

	// translated form of a label produced by this class
	static	std::string			Translate(const std::string& label);

	std::string					makeModel;
	bool						fromPrinter;	// false = SetDefaults()
	bigtime_t					queried;		// real_time_clock_usecs()
	bool						pwgRaster;		// image/pwg-raster accepted
	bool						color;
	bool						gray;
	bool						duplex;
	bool						duplexShortEdge;
	SheetBack					sheetBack;
	std::vector<IPPMediaSize>	sizes;
	std::vector<IPPKeyword>		mediaTypes;
	std::vector<IPPKeyword>		sources;		// media-source-supported
	std::vector<IPPResolution>	resolutions;
	std::vector<int>			qualities;		// IPP print-quality values
	float						marginPoints;	// hardware margin, all edges
	int							maxCopies;		// copies-supported upper
	bool						collate;		// printer collates copies
	bool						colorMode;		// print-color-mode accepted

private:
	static	bool				ParseMediaName(const std::string& name,
									IPPMediaSize& size);
	static	std::string			MediaLabel(const std::string& pwgName,
									float widthPoints, float heightPoints);
	static	std::string			MediaTypeLabel(const std::string& keyword);
	static	std::string			SourceLabel(const std::string& keyword);
};


#endif // IPP_CAPABILITIES_H
