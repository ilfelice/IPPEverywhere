/*
 * IPPCapabilities.cpp
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "IPPCapabilities.h"

#include <Catalog.h>
#include <Message.h>
#include <Node.h>
#include <fs_attr.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "IPPClient.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "IPPCapabilities"


const char* IPPCapabilities::kAttributeName = "ipp-everywhere:capabilities";


IPPCapabilities::IPPCapabilities()
{
	SetDefaults();
}


void
IPPCapabilities::SetDefaults()
{
	makeModel = "IPP printer";
	fromPrinter = false;
	queried = 0;
	pwgRaster = true;
	color = true;
	gray = true;
	duplex = false;
	duplexShortEdge = false;
	sheetBack = kNormal;

	sizes.clear();
	IPPMediaSize size;
	ParseMediaName("iso_a4_210x297mm", size);
	size.isDefault = true;
	sizes.push_back(size);
	ParseMediaName("na_letter_8.5x11in", size);
	size.isDefault = false;
	sizes.push_back(size);
	ParseMediaName("na_legal_8.5x14in", size);
	sizes.push_back(size);
	ParseMediaName("iso_a5_148x210mm", size);
	sizes.push_back(size);

	mediaTypes.clear();
	IPPKeyword type;
	type.keyword = "stationery";
	type.label = MediaTypeLabel(type.keyword);
	type.isDefault = true;
	mediaTypes.push_back(type);

	resolutions.clear();
	IPPResolution resolution;
	resolution.x = resolution.y = 300;
	resolution.isDefault = true;
	resolutions.push_back(resolution);

	qualities.clear();

	sources.clear();
	IPPKeyword source;
	source.keyword = "auto";
	source.label = SourceLabel(source.keyword);
	source.isDefault = true;
	sources.push_back(source);

	marginPoints = 0;
	maxCopies = 1;
	collate = false;
	colorMode = false;
}


// #pragma mark - labels


namespace {

struct LabelEntry {
	const char*	key;
	const char*	label;
};

const LabelEntry kSizeLabels[] = {
	{ "iso_a3", B_TRANSLATE_MARK("A3") },
	{ "iso_a4", B_TRANSLATE_MARK("A4") },
	{ "iso_a5", B_TRANSLATE_MARK("A5") },
	{ "iso_a6", B_TRANSLATE_MARK("A6") },
	{ "iso_b4", B_TRANSLATE_MARK("B4 (ISO)") },
	{ "iso_b5", B_TRANSLATE_MARK("B5 (ISO)") },
	{ "iso_c5", B_TRANSLATE_MARK("Envelope C5") },
	{ "iso_c6", B_TRANSLATE_MARK("Envelope C6") },
	{ "iso_dl", B_TRANSLATE_MARK("Envelope DL") },
	{ "jis_b4", B_TRANSLATE_MARK("B4 (JIS)") },
	{ "jis_b5", B_TRANSLATE_MARK("B5 (JIS)") },
	{ "jis_b6", B_TRANSLATE_MARK("B6 (JIS)") },
	{ "na_letter", B_TRANSLATE_MARK("Letter") },
	{ "na_legal", B_TRANSLATE_MARK("Legal") },
	{ "na_ledger", B_TRANSLATE_MARK("Ledger") },
	{ "na_tabloid", B_TRANSLATE_MARK("Tabloid") },
	{ "na_executive", B_TRANSLATE_MARK("Executive") },
	{ "na_invoice", B_TRANSLATE_MARK("Statement") },
	{ "na_govt-letter", B_TRANSLATE_MARK("8 x 10 in") },
	{ "na_index-3x5", B_TRANSLATE_MARK("3 x 5 in") },
	{ "na_index-4x6", B_TRANSLATE_MARK("4 x 6 in") },
	{ "na_index-5x8", B_TRANSLATE_MARK("5 x 8 in") },
	{ "na_5x7", B_TRANSLATE_MARK("5 x 7 in") },
	{ "na_number-10", B_TRANSLATE_MARK("Envelope #10") },
	{ "na_number-9", B_TRANSLATE_MARK("Envelope #9") },
	{ "na_monarch", B_TRANSLATE_MARK("Envelope Monarch") },
	{ "jpn_hagaki", B_TRANSLATE_MARK("Hagaki (postcard)") },
	{ "jpn_oufuku", B_TRANSLATE_MARK("Oufuku hagaki") },
	{ "jpn_chou2", B_TRANSLATE_MARK("Envelope Chou 2") },
	{ "jpn_chou3", B_TRANSLATE_MARK("Envelope Chou 3") },
	{ "jpn_chou4", B_TRANSLATE_MARK("Envelope Chou 4") },
	{ "jpn_kaku2", B_TRANSLATE_MARK("Envelope Kaku 2") },
	{ "jpn_kaku3", B_TRANSLATE_MARK("Envelope Kaku 3") },
	{ "jpn_you4", B_TRANSLATE_MARK("Envelope You 4") },
	{ "jpn_you6", B_TRANSLATE_MARK("Envelope You 6") },
	{ "om_you1", B_TRANSLATE_MARK("Envelope You 1") },
	{ "om_you3", B_TRANSLATE_MARK("Envelope You 3") },
	{ "oe_photo-l", B_TRANSLATE_MARK("L (89 x 127 mm)") },
	{ "oe_photo-2l", B_TRANSLATE_MARK("2L (127 x 178 mm)") },
	{ "om_hivision", B_TRANSLATE_MARK("Hi-Vision (101.6 x 180.6 mm)") },
	{ "om_square-photo", B_TRANSLATE_MARK("Square photo (127 x 127 mm)") },
	{ NULL, NULL }
};

const LabelEntry kTypeLabels[] = {
	{ "auto", B_TRANSLATE_MARK("Automatic") },
	{ "stationery", B_TRANSLATE_MARK("Plain paper") },
	{ "stationery-inkjet", B_TRANSLATE_MARK("Inkjet paper") },
	{ "stationery-coated", B_TRANSLATE_MARK("Coated paper") },
	{ "stationery-letterhead", B_TRANSLATE_MARK("Letterhead") },
	{ "stationery-heavyweight", B_TRANSLATE_MARK("Heavyweight paper") },
	{ "stationery-lightweight", B_TRANSLATE_MARK("Lightweight paper") },
	{ "stationery-fine", B_TRANSLATE_MARK("Fine paper") },
	{ "stationery-preprinted", B_TRANSLATE_MARK("Preprinted paper") },
	{ "stationery-prepunched", B_TRANSLATE_MARK("Prepunched paper") },
	{ "photographic", B_TRANSLATE_MARK("Photo paper") },
	{ "photographic-glossy", B_TRANSLATE_MARK("Photo paper (glossy)") },
	{ "photographic-high-gloss", B_TRANSLATE_MARK("Photo paper (high gloss)") },
	{ "photographic-semi-gloss", B_TRANSLATE_MARK("Photo paper (semi-gloss)") },
	{ "photographic-satin", B_TRANSLATE_MARK("Photo paper (satin)") },
	{ "photographic-matte", B_TRANSLATE_MARK("Photo paper (matte)") },
	{ "envelope", B_TRANSLATE_MARK("Envelope") },
	{ "labels", B_TRANSLATE_MARK("Labels") },
	{ "transparency", B_TRANSLATE_MARK("Transparency") },
	{ "cardstock", B_TRANSLATE_MARK("Cardstock") },
	{ "com.epson-hagaki", B_TRANSLATE_MARK("Hagaki") },
	{ "com.epson-hagaki-glossy", B_TRANSLATE_MARK("Hagaki (glossy)") },
	{ "com.epson-hagaki-addr", B_TRANSLATE_MARK("Hagaki (address side)") },
	{ "com.epson-matte-business-card", B_TRANSLATE_MARK("Business card (matte)") },
	{ "com.epson-business-plain", B_TRANSLATE_MARK("Business plain paper") },
	{ NULL, NULL }
};


const LabelEntry kSourceLabels[] = {
	{ "auto", B_TRANSLATE_MARK("Automatic") },
	{ "main", B_TRANSLATE_MARK("Main tray") },
	{ "alternate", B_TRANSLATE_MARK("Alternate tray") },
	{ "manual", B_TRANSLATE_MARK("Manual feed") },
	{ "large-capacity", B_TRANSLATE_MARK("Large capacity tray") },
	{ "envelope", B_TRANSLATE_MARK("Envelope feeder") },
	{ "photo", B_TRANSLATE_MARK("Photo tray") },
	{ "hagaki", B_TRANSLATE_MARK("Hagaki tray") },
	{ "rear", B_TRANSLATE_MARK("Rear tray") },
	{ "top", B_TRANSLATE_MARK("Top tray") },
	{ "middle", B_TRANSLATE_MARK("Middle tray") },
	{ "bottom", B_TRANSLATE_MARK("Bottom tray") },
	{ "by-pass-tray", B_TRANSLATE_MARK("Bypass tray") },
	{ "disc", B_TRANSLATE_MARK("Disc tray") },
	{ "main-roll", B_TRANSLATE_MARK("Main roll") },
	{ "alternate-roll", B_TRANSLATE_MARK("Alternate roll") },
	{ NULL, NULL }
};


std::string
Humanize(const std::string& keyword)
{
	std::string s;
	bool start = true;
	for (size_t i = 0; i < keyword.size(); i++) {
		char c = keyword[i];
		if (c == '-' || c == '_') {
			s += ' ';
			start = true;
		} else {
			s += start ? toupper((unsigned char)c) : c;
			start = false;
		}
	}
	return s;
}


std::string
FormatDimension(float points, bool inches)
{
	char buffer[32];
	if (inches)
		snprintf(buffer, sizeof(buffer), "%g", points / 72.0f);
	else
		snprintf(buffer, sizeof(buffer), "%g", points * 25.4f / 72.0f);
	return buffer;
}

}	// namespace


std::string
IPPCapabilities::MediaLabel(const std::string& pwgName, float widthPoints,
	float heightPoints)
{
	// "class_name_WxHunit" -> "class_name"
	size_t last = pwgName.rfind('_');
	std::string key = last == std::string::npos ? pwgName
		: pwgName.substr(0, last);
	for (int i = 0; kSizeLabels[i].key != NULL; i++) {
		if (key == kSizeLabels[i].key)
			return kSizeLabels[i].label;
	}

	// unknown: "Name (W x H unit)"
	size_t first = key.find('_');
	std::string name = first == std::string::npos ? key : key.substr(first + 1);
	bool inches = pwgName.size() > 2
		&& pwgName.compare(pwgName.size() - 2, 2, "in") == 0;
	std::string label = Humanize(name);
	label += " (";
	label += FormatDimension(widthPoints, inches);
	label += " x ";
	label += FormatDimension(heightPoints, inches);
	label += inches ? " in)" : " mm)";
	return label;
}


std::string
IPPCapabilities::MediaTypeLabel(const std::string& keyword)
{
	for (int i = 0; kTypeLabels[i].key != NULL; i++) {
		if (keyword == kTypeLabels[i].key)
			return kTypeLabels[i].label;
	}
	// vendor keyword "com.vendor-something" -> "Vendor: Something"
	if (keyword.compare(0, 4, "com.") == 0) {
		size_t dash = keyword.find('-');
		if (dash != std::string::npos) {
			return Humanize(keyword.substr(4, dash - 4)) + ": "
				+ Humanize(keyword.substr(dash + 1));
		}
	}
	return Humanize(keyword);
}


std::string
IPPCapabilities::SourceLabel(const std::string& keyword)
{
	for (int i = 0; kSourceLabels[i].key != NULL; i++) {
		if (keyword == kSourceLabels[i].key)
			return kSourceLabels[i].label;
	}
	// English here; Translate() handles these two at display time
	if (keyword.compare(0, 5, "tray-") == 0)
		return "Tray " + keyword.substr(5);
	if (keyword.compare(0, 5, "roll-") == 0)
		return "Roll " + keyword.substr(5);
	return Humanize(keyword);
}


bool
IPPCapabilities::ParseMediaName(const std::string& name, IPPMediaSize& size)
{
	// PWG 5101.1 self describing name: class_name_WxHunit, unit = mm | in
	if (name.compare(0, 7, "custom_") == 0 || name.compare(0, 5, "roll_") == 0)
		return false;
	size_t last = name.rfind('_');
	if (last == std::string::npos)
		return false;
	std::string dims = name.substr(last + 1);
	bool inches;
	if (dims.size() > 2 && dims.compare(dims.size() - 2, 2, "mm") == 0)
		inches = false;
	else if (dims.size() > 2 && dims.compare(dims.size() - 2, 2, "in") == 0)
		inches = true;
	else
		return false;
	dims = dims.substr(0, dims.size() - 2);
	size_t x = dims.find('x');
	if (x == std::string::npos)
		return false;
	float width = atof(dims.substr(0, x).c_str());
	float height = atof(dims.substr(x + 1).c_str());
	if (width <= 0 || height <= 0)
		return false;
	float scale = inches ? 72.0f : 72.0f / 25.4f;
	size.pwgName = name;
	size.widthPoints = width * scale;
	size.heightPoints = height * scale;
	if (size.widthPoints > size.heightPoints) {
		float t = size.widthPoints;
		size.widthPoints = size.heightPoints;
		size.heightPoints = t;
	}
	size.label = MediaLabel(name, size.widthPoints, size.heightPoints);
	size.isDefault = false;
	return true;
}


// #pragma mark - from the printer


void
IPPCapabilities::SetFrom(const IPPAttributes& attributes)
{
	SetDefaults();
	fromPrinter = true;
	queried = real_time_clock_usecs();

	std::string model = attributes.String("printer-make-and-model");
	if (!model.empty())
		makeModel = model;

	// formats
	if (attributes.Has("document-format-supported")) {
		pwgRaster = attributes.Contains("document-format-supported",
			"image/pwg-raster");
	}

	// media sizes
	std::vector<std::string> media = attributes.Strings("media-supported");
	std::string defaultMedia = attributes.String("media-default");
	if (!media.empty()) {
		sizes.clear();
		for (size_t i = 0; i < media.size(); i++) {
			IPPMediaSize size;
			if (!ParseMediaName(media[i], size))
				continue;
			bool duplicate = false;
			for (size_t j = 0; j < sizes.size(); j++) {
				if (sizes[j].pwgName == size.pwgName)
					duplicate = true;
			}
			if (duplicate)
				continue;
			size.isDefault = media[i] == defaultMedia;
			sizes.push_back(size);
		}
		if (sizes.empty()) {
			IPPMediaSize a4;
			ParseMediaName("iso_a4_210x297mm", a4);
			sizes.push_back(a4);
		}
		bool haveDefault = false;
		for (size_t i = 0; i < sizes.size(); i++)
			haveDefault |= sizes[i].isDefault;
		if (!haveDefault)
			sizes[0].isDefault = true;
	}

	// media types
	std::vector<std::string> types = attributes.Strings("media-type-supported");
	if (!types.empty()) {
		mediaTypes.clear();
		for (size_t i = 0; i < types.size(); i++) {
			IPPKeyword type;
			type.keyword = types[i];
			type.label = MediaTypeLabel(types[i]);
			type.isDefault = false;
			mediaTypes.push_back(type);
		}
		bool haveDefault = false;
		for (size_t i = 0; i < mediaTypes.size(); i++) {
			if (mediaTypes[i].keyword == "stationery") {
				mediaTypes[i].isDefault = true;
				haveDefault = true;
				break;
			}
		}
		if (!haveDefault)
			mediaTypes[0].isDefault = true;
	}

	// resolutions: prefer what the printer accepts for PWG raster
	const char* resolutionAttribute = "pwg-raster-document-resolution-supported";
	if (!attributes.Has(resolutionAttribute))
		resolutionAttribute = "printer-resolution-supported";
	int count = attributes.Count(resolutionAttribute);
	if (count > 0) {
		resolutions.clear();
		for (int i = 0; i < count; i++) {
			const IPPValue* value = attributes.Value(resolutionAttribute, i);
			if (value == NULL || value->tag != 0x32 || value->integer <= 0)
				continue;
			if (value->integer != value->integer2)
				continue;		// libprint assumes square pixels
			IPPResolution resolution;
			resolution.x = value->integer;
			resolution.y = value->integer2;
			resolution.isDefault = false;
			resolutions.push_back(resolution);
		}
		if (resolutions.empty()) {
			IPPResolution resolution;
			resolution.x = resolution.y = 300;
			resolution.isDefault = false;
			resolutions.push_back(resolution);
		}
		bool haveDefault = false;
		for (size_t i = 0; i < resolutions.size(); i++) {
			if (resolutions[i].x == 300) {
				resolutions[i].isDefault = true;
				haveDefault = true;
				break;
			}
		}
		if (!haveDefault)
			resolutions[0].isDefault = true;
	}

	// color
	if (attributes.Has("pwg-raster-document-type-supported")) {
		color = attributes.Contains("pwg-raster-document-type-supported",
			"srgb_8");
		gray = attributes.Contains("pwg-raster-document-type-supported",
			"sgray_8");
	} else if (attributes.Has("print-color-mode-supported")) {
		color = attributes.Contains("print-color-mode-supported", "color");
		gray = attributes.Contains("print-color-mode-supported", "monochrome");
	}
	if (!color && !gray)
		color = gray = true;

	// duplex
	duplex = attributes.Contains("sides-supported", "two-sided-long-edge");
	duplexShortEdge = attributes.Contains("sides-supported",
		"two-sided-short-edge");
	std::string back = attributes.String("pwg-raster-document-sheet-back");
	if (back == "rotated")
		sheetBack = kRotated;
	else if (back == "flipped")
		sheetBack = kFlipped;
	else if (back == "manual-tumble")
		sheetBack = kManualTumble;
	else
		sheetBack = kNormal;

	// paper sources
	std::vector<std::string> sourceNames
		= attributes.Strings("media-source-supported");
	if (!sourceNames.empty()) {
		sources.clear();
		for (size_t i = 0; i < sourceNames.size(); i++) {
			IPPKeyword source;
			source.keyword = sourceNames[i];
			source.label = SourceLabel(sourceNames[i]);
			source.isDefault = false;
			sources.push_back(source);
		}
		bool haveDefault = false;
		for (size_t i = 0; i < sources.size(); i++) {
			if (sources[i].keyword == "auto") {
				sources[i].isDefault = true;
				haveDefault = true;
				break;
			}
		}
		if (!haveDefault)
			sources[0].isDefault = true;
	}

	// hardware margins: the smallest non-zero value of every edge, in
	// hundredths of a millimeter; the largest of the four is used for all
	// edges so it does not depend on the orientation
	marginPoints = 0;
	const char* marginAttributes[] = {
		"media-left-margin-supported", "media-right-margin-supported",
		"media-top-margin-supported", "media-bottom-margin-supported"
	};
	for (int i = 0; i < 4; i++) {
		int count = attributes.Count(marginAttributes[i]);
		int smallest = 0;
		for (int j = 0; j < count; j++) {
			const IPPValue* value = attributes.Value(marginAttributes[i], j);
			if (value == NULL || value->tag != 0x21 || value->integer <= 0)
				continue;
			if (smallest == 0 || value->integer < smallest)
				smallest = value->integer;
		}
		float points = smallest / 100.0f * 72.0f / 25.4f;
		if (points > marginPoints)
			marginPoints = points;
	}

	// copies
	maxCopies = 1;
	const IPPValue* copies = attributes.Value("copies-supported");
	if (copies != NULL) {
		if (copies->tag == 0x33)
			maxCopies = copies->integer2;
		else if (copies->tag == 0x21)
			maxCopies = copies->integer;
	}
	collate = attributes.Contains("multiple-document-handling-supported",
		"separate-documents-collated-copies");
	colorMode = attributes.Has("print-color-mode-supported");

	// quality
	qualities.clear();
	count = attributes.Count("print-quality-supported");
	for (int i = 0; i < count; i++) {
		const IPPValue* value = attributes.Value("print-quality-supported", i);
		if (value != NULL && value->integer >= 3 && value->integer <= 5)
			qualities.push_back(value->integer);
	}
}


// #pragma mark - storage


void
IPPCapabilities::ToMessage(BMessage& message) const
{
	message.MakeEmpty();
	message.AddString("make-model", makeModel.c_str());
	message.AddBool("from-printer", fromPrinter);
	message.AddInt64("queried", queried);
	message.AddBool("pwg-raster", pwgRaster);
	message.AddBool("color", color);
	message.AddBool("gray", gray);
	message.AddBool("duplex", duplex);
	message.AddBool("duplex-short-edge", duplexShortEdge);
	message.AddInt32("sheet-back", sheetBack);
	for (size_t i = 0; i < sizes.size(); i++) {
		message.AddString("size-name", sizes[i].pwgName.c_str());
		message.AddString("size-label", sizes[i].label.c_str());
		message.AddFloat("size-width", sizes[i].widthPoints);
		message.AddFloat("size-height", sizes[i].heightPoints);
		message.AddBool("size-default", sizes[i].isDefault);
	}
	for (size_t i = 0; i < mediaTypes.size(); i++) {
		message.AddString("type-keyword", mediaTypes[i].keyword.c_str());
		message.AddString("type-label", mediaTypes[i].label.c_str());
		message.AddBool("type-default", mediaTypes[i].isDefault);
	}
	for (size_t i = 0; i < resolutions.size(); i++) {
		message.AddInt32("resolution-x", resolutions[i].x);
		message.AddInt32("resolution-y", resolutions[i].y);
		message.AddBool("resolution-default", resolutions[i].isDefault);
	}
	for (size_t i = 0; i < qualities.size(); i++)
		message.AddInt32("quality", qualities[i]);
	for (size_t i = 0; i < sources.size(); i++) {
		message.AddString("source-keyword", sources[i].keyword.c_str());
		message.AddString("source-label", sources[i].label.c_str());
		message.AddBool("source-default", sources[i].isDefault);
	}
	message.AddFloat("margin", marginPoints);
	message.AddInt32("max-copies", maxCopies);
	message.AddBool("collate", collate);
	message.AddBool("color-mode", colorMode);
}


bool
IPPCapabilities::FromMessage(const BMessage& message)
{
	if (!message.HasString("size-name") || !message.HasInt32("resolution-x"))
		return false;

	SetDefaults();
	makeModel = message.GetString("make-model", makeModel.c_str());
	fromPrinter = message.GetBool("from-printer", false);
	queried = message.GetInt64("queried", 0);
	pwgRaster = message.GetBool("pwg-raster", true);
	color = message.GetBool("color", true);
	gray = message.GetBool("gray", true);
	duplex = message.GetBool("duplex", false);
	duplexShortEdge = message.GetBool("duplex-short-edge", false);
	sheetBack = (SheetBack)message.GetInt32("sheet-back", kNormal);

	sizes.clear();
	const char* name;
	for (int32 i = 0; message.FindString("size-name", i, &name) == B_OK; i++) {
		IPPMediaSize size;
		size.pwgName = name;
		size.label = message.GetString("size-label", i, "");
		size.widthPoints = message.GetFloat("size-width", i, 595);
		size.heightPoints = message.GetFloat("size-height", i, 842);
		size.isDefault = message.GetBool("size-default", i, false);
		sizes.push_back(size);
	}

	mediaTypes.clear();
	for (int32 i = 0; message.FindString("type-keyword", i, &name) == B_OK;
			i++) {
		IPPKeyword type;
		type.keyword = name;
		type.label = message.GetString("type-label", i, "");
		type.isDefault = message.GetBool("type-default", i, false);
		mediaTypes.push_back(type);
	}
	if (mediaTypes.empty()) {
		IPPKeyword type;
		type.keyword = "stationery";
		type.label = MediaTypeLabel(type.keyword);
		type.isDefault = true;
		mediaTypes.push_back(type);
	}

	resolutions.clear();
	int32 x;
	for (int32 i = 0; message.FindInt32("resolution-x", i, &x) == B_OK; i++) {
		IPPResolution resolution;
		resolution.x = x;
		resolution.y = message.GetInt32("resolution-y", i, x);
		resolution.isDefault = message.GetBool("resolution-default", i, false);
		resolutions.push_back(resolution);
	}

	qualities.clear();
	int32 quality;
	for (int32 i = 0; message.FindInt32("quality", i, &quality) == B_OK; i++)
		qualities.push_back(quality);

	if (message.HasString("source-keyword")) {
		sources.clear();
		for (int32 i = 0; message.FindString("source-keyword", i, &name) == B_OK;
				i++) {
			IPPKeyword source;
			source.keyword = name;
			source.label = message.GetString("source-label", i, "");
			source.isDefault = message.GetBool("source-default", i, false);
			sources.push_back(source);
		}
	}
	marginPoints = message.GetFloat("margin", 0);
	maxCopies = message.GetInt32("max-copies", 1);
	collate = message.GetBool("collate", false);
	colorMode = message.GetBool("color-mode", false);

	return true;
}


std::string
IPPCapabilities::Translate(const std::string& label)
{
	// labels are stored in English; the known ones are in the catalog
	if (label.compare(0, 5, "Tray ") == 0)
		return std::string(B_TRANSLATE("Tray")) + label.substr(4);
	if (label.compare(0, 5, "Roll ") == 0)
		return std::string(B_TRANSLATE("Roll")) + label.substr(4);
	return B_TRANSLATE_NOCOLLECT(label.c_str());
}


status_t
IPPCapabilities::Save(BNode& node) const
{
	BMessage message;
	ToMessage(message);
	ssize_t size = message.FlattenedSize();
	if (size <= 0)
		return B_ERROR;
	std::vector<char> buffer(size);
	status_t status = message.Flatten(&buffer[0], size);
	if (status != B_OK)
		return status;
	ssize_t written = node.WriteAttr(kAttributeName, B_MESSAGE_TYPE, 0,
		&buffer[0], size);
	return written == size ? B_OK : B_ERROR;
}


status_t
IPPCapabilities::Load(BNode& node)
{
	attr_info info;
	if (node.GetAttrInfo(kAttributeName, &info) != B_OK || info.size <= 0)
		return B_ENTRY_NOT_FOUND;
	std::vector<char> buffer(info.size);
	ssize_t read = node.ReadAttr(kAttributeName, B_MESSAGE_TYPE, 0,
		&buffer[0], info.size);
	if (read != info.size)
		return B_ERROR;
	BMessage message;
	if (message.Unflatten(&buffer[0]) != B_OK)
		return B_ERROR;
	return FromMessage(message) ? B_OK : B_ERROR;
}
