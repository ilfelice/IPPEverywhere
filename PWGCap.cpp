/*
 * PWGCap.cpp
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "PWGCap.h"

#include <Node.h>
#include <String.h>

#include <stdio.h>
#include <string.h>

#include "IPPClient.h"
#include "PrinterData.h"

#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PWGCap"


const char* PWGCap::kMediaTypeKey = "media-type";
const char* PWGCap::kQualityKey = "print-quality";
const char* PWGCap::kDuplexEdgeKey = "duplex-edge";

// attributes on the printer folder
static const char* kTransportAddressAttribute = "transport_address";
static const char* kLastQueryAttribute = "ipp-everywhere:last-query";
static const char* kRawAttributesAttribute = "ipp-everywhere:attributes";

static const bigtime_t kCacheLifetime = 24LL * 3600 * 1000000;	// a day
static const bigtime_t kRetryInterval = 10LL * 60 * 1000000;	// 10 minutes
static const bigtime_t kQueryTimeout = 3000000;					// 3 seconds


// #pragma mark - helpers


// list item with an explicit key (libprint does not set fKey)
struct KeyedListItemCap : public ListItemCap {
	KeyedListItemCap(const string& label, bool isDefault, int32 id,
		const string& key)
		:
		ListItemCap(label, isDefault, id)
	{
		fKey = key;
	}
};


struct KeyedDriverSpecificCap : public DriverSpecificCap {
	KeyedDriverSpecificCap(const string& label, int32 category, Type type,
		const string& key)
		:
		DriverSpecificCap(label, category, type)
	{
		fKey = key;
	}
};


template<typename T>
static void
DeleteAll(std::vector<T*>& items)
{
	for (size_t i = 0; i < items.size(); i++)
		delete items[i];
	items.clear();
}


template<typename T>
static void
FillArray(const std::vector<T*>& items, std::vector<const BaseCap*>& array)
{
	array.clear();
	for (size_t i = 0; i < items.size(); i++)
		array.push_back(items[i]);
}


static const BaseCap**
ArrayPointer(const std::vector<const BaseCap*>& array)
{
	// libprint's GetCaps() returns a non-const pointer to the array even
	// though it never writes through it
	return array.empty() ? NULL : const_cast<const BaseCap**>(&array[0]);
}


struct PaperIDEntry {
	const char*		key;
	JobData::Paper	paper;
};

static const PaperIDEntry kPaperIDs[] = {
	{ "iso_a3", JobData::kA3 },
	{ "iso_a4", JobData::kA4 },
	{ "iso_a5", JobData::kA5 },
	{ "iso_a6", JobData::kA6 },
	{ "iso_b4", JobData::kIsoB4 },
	{ "iso_c5", JobData::kEnvC5 },
	{ "iso_c6", JobData::kEnvC6 },
	{ "iso_dl", JobData::kEnvDL },
	{ "jis_b4", JobData::kB4 },
	{ "jis_b5", JobData::kB5 },
	{ "jis_b6", JobData::kB6JIS },
	{ "na_letter", JobData::kLetter },
	{ "na_legal", JobData::kLegal },
	{ "na_ledger", JobData::kLedger },
	{ "na_tabloid", JobData::kTabloid },
	{ "na_executive", JobData::kExecutive },
	{ "na_invoice", JobData::kStatement },
	{ "na_number-10", JobData::kEnv10 },
	{ "na_number-9", JobData::kEnv9 },
	{ "na_monarch", JobData::kEnvMonarch },
	{ "jpn_hagaki", JobData::kJapanesePostcard },
	{ "jpn_oufuku", JobData::kDBLJapanesePostcard },
	{ "jpn_chou3", JobData::kJEnvChou3 },
	{ "jpn_chou4", JobData::kJEnvChou4 },
	{ "jpn_kaku2", JobData::kJEnvKaku2 },
	{ "jpn_kaku3", JobData::kJEnvKaku3 },
	{ "jpn_you4", JobData::kJEnvYou4 },
	{ NULL, JobData::kA4 }
};


int32
PWGCap::_PaperID(const std::string& pwgName)
{
	size_t last = pwgName.rfind('_');
	std::string key = last == std::string::npos ? pwgName
		: pwgName.substr(0, last);
	for (int i = 0; kPaperIDs[i].key != NULL; i++) {
		if (key == kPaperIDs[i].key)
			return kPaperIDs[i].paper;
	}
	// libprint has no id for this size: derive a stable one from the name
	uint32 hash = 5381;
	for (size_t i = 0; i < pwgName.size(); i++)
		hash = hash * 33 + (uint8)pwgName[i];
	return JobData::kUserDefined + 1 + (int32)(hash % 100000);
}


// #pragma mark - PWGCap


PWGCap::PWGCap(const PrinterData* printerData)
	:
	PrinterCap(printerData)
{
	std::string path;
	if (printerData != NULL && printerData->GetPath(path)) {
		BNode node(path.c_str());
		if (node.InitCheck() == B_OK)
			Update(node, fCapabilities, false);
	}
	_Build();
}


PWGCap::~PWGCap()
{
	_Clear();
}


void
PWGCap::_Clear()
{
	DeleteAll(fPapers);
	DeleteAll(fSources);
	DeleteAll(fResolutions);
	DeleteAll(fPrintStyles);
	DeleteAll(fColors);
	DeleteAll(fDriverCaps);
	DeleteAll(fMediaTypes);
	DeleteAll(fQualities);
	DeleteAll(fDuplexEdges);
}


void
PWGCap::_Build()
{
	_Clear();
	const IPPCapabilities& caps = fCapabilities;

	// paper sizes: the physical (imageable) rect equals the paper rect,
	// PWG raster describes the whole sheet
	std::vector<int32> usedIDs;
	for (size_t i = 0; i < caps.sizes.size(); i++) {
		const IPPMediaSize& size = caps.sizes[i];
		int32 id = _PaperID(size.pwgName);
		bool taken = true;
		while (taken) {
			taken = false;
			for (size_t j = 0; j < usedIDs.size(); j++) {
				if (usedIDs[j] == id) {
					taken = true;
					id++;
				}
			}
		}
		usedIDs.push_back(id);
		BRect rect(0, 0, (int)(size.widthPoints + 0.5f),
			(int)(size.heightPoints + 0.5f));
		// the imageable area is the sheet minus the hardware margin; the
		// driver pads the raster back to the full sheet
		BRect physical(rect);
		float margin = (int)(caps.marginPoints + 0.5f);
		if (margin > 0 && rect.Width() > 4 * margin
			&& rect.Height() > 4 * margin)
			physical.InsetBy(margin, margin);
		fPapers.push_back(new PaperCap(IPPCapabilities::Translate(size.label),
			size.isDefault, (JobData::Paper)id, rect, physical));
	}

	for (size_t i = 0; i < caps.sources.size(); i++) {
		const IPPKeyword& source = caps.sources[i];
		fSources.push_back(new PaperSourceCap(
			IPPCapabilities::Translate(source.label), source.isDefault,
			(JobData::PaperSource)i));
	}
	if (fSources.empty())
		fSources.push_back(new PaperSourceCap(B_TRANSLATE("Automatic"), true,
			JobData::kAuto));

	for (size_t i = 0; i < caps.resolutions.size(); i++) {
		const IPPResolution& resolution = caps.resolutions[i];
		char label[32];
		snprintf(label, sizeof(label), B_TRANSLATE("%d dpi"), resolution.x);
		fResolutions.push_back(new ResolutionCap(label, resolution.isDefault,
			i, resolution.x, resolution.y));
	}

	fPrintStyles.push_back(new PrintStyleCap(B_TRANSLATE("Simplex"), true,
		JobData::kSimplex));
	if (caps.duplex) {
		fPrintStyles.push_back(new PrintStyleCap(B_TRANSLATE("Duplex"), false,
			JobData::kDuplex));
		fPrintStyles.push_back(new PrintStyleCap(B_TRANSLATE("Booklet"), false,
			JobData::kBooklet));
	}

	if (caps.color)
		fColors.push_back(new ColorCap(B_TRANSLATE("Color"), true, JobData::kColor));
	if (caps.gray)
		fColors.push_back(new ColorCap(B_TRANSLATE("Grayscale"), !caps.color,
			JobData::kMonochrome));

	// driver specific settings shown in the job setup dialog
	if (caps.mediaTypes.size() > 0) {
		fDriverCaps.push_back(new KeyedDriverSpecificCap(B_TRANSLATE("Media type"),
			kMediaType, DriverSpecificCap::kList, kMediaTypeKey));
		for (size_t i = 0; i < caps.mediaTypes.size(); i++) {
			const IPPKeyword& type = caps.mediaTypes[i];
			fMediaTypes.push_back(new KeyedListItemCap(
				IPPCapabilities::Translate(type.label), type.isDefault, i,
				type.keyword));
		}
	}

	if (caps.qualities.size() > 1) {
		fDriverCaps.push_back(new KeyedDriverSpecificCap(B_TRANSLATE("Quality"),
			kQuality, DriverSpecificCap::kList, kQualityKey));
		bool haveDefault = false;
		for (size_t i = 0; i < caps.qualities.size(); i++) {
			int quality = caps.qualities[i];
			const char* label = quality == 3 ? B_TRANSLATE("Draft")
				: quality == 5 ? B_TRANSLATE("High") : B_TRANSLATE("Normal");
			const char* key = quality == 3 ? "draft"
				: quality == 5 ? "high" : "normal";
			bool isDefault = quality == 4;
			haveDefault |= isDefault;
			fQualities.push_back(new KeyedListItemCap(label, isDefault,
				quality, key));
		}
		if (!haveDefault)
			fQualities[0]->fIsDefault = true;
	}

	if (caps.duplex && caps.duplexShortEdge) {
		fDriverCaps.push_back(new KeyedDriverSpecificCap(B_TRANSLATE("Duplex binding"),
			kDuplexEdge, DriverSpecificCap::kList, kDuplexEdgeKey));
		fDuplexEdges.push_back(new KeyedListItemCap(B_TRANSLATE("Long edge"), true, 0,
			"long-edge"));
		fDuplexEdges.push_back(new KeyedListItemCap(B_TRANSLATE("Short edge"), false, 1,
			"short-edge"));
	}

	FillArray(fPapers, fPaperArray);
	FillArray(fSources, fSourceArray);
	FillArray(fResolutions, fResolutionArray);
	FillArray(fPrintStyles, fPrintStyleArray);
	FillArray(fColors, fColorArray);
	FillArray(fDriverCaps, fDriverCapArray);
	FillArray(fMediaTypes, fMediaTypeArray);
	FillArray(fQualities, fQualityArray);
	FillArray(fDuplexEdges, fDuplexEdgeArray);
}


int
PWGCap::CountCap(CapID category) const
{
	switch ((int)category) {
		case kPaper:
			return fPaperArray.size();
		case kPaperSource:
			return fSourceArray.size();
		case kResolution:
			return fResolutionArray.size();
		case kColor:
			return fColorArray.size();
		case kPrintStyle:
			return fPrintStyleArray.size();
		case kDriverSpecificCapabilities:
			return fDriverCapArray.size();
		case kMediaType:
			return fMediaTypeArray.size();
		case kQuality:
			return fQualityArray.size();
		case kDuplexEdge:
			return fDuplexEdgeArray.size();
		default:
			return 0;
	}
}


const BaseCap**
PWGCap::GetCaps(CapID category) const
{
	switch ((int)category) {
		case kPaper:
			return ArrayPointer(fPaperArray);
		case kPaperSource:
			return ArrayPointer(fSourceArray);
		case kResolution:
			return ArrayPointer(fResolutionArray);
		case kColor:
			return ArrayPointer(fColorArray);
		case kPrintStyle:
			return ArrayPointer(fPrintStyleArray);
		case kDriverSpecificCapabilities:
			return ArrayPointer(fDriverCapArray);
		case kMediaType:
			return ArrayPointer(fMediaTypeArray);
		case kQuality:
			return ArrayPointer(fQualityArray);
		case kDuplexEdge:
			return ArrayPointer(fDuplexEdgeArray);
		default:
			return NULL;
	}
}


bool
PWGCap::Supports(CapID category) const
{
	switch ((int)category) {
		case kPaper:
		case kPaperSource:
		case kResolution:
		case kColor:
			return true;
		case kPrintStyle:
			return fCapabilities.duplex;
		case kDriverSpecificCapabilities:
			return !fDriverCapArray.empty();
		case kMediaType:
			return !fMediaTypeArray.empty();
		case kQuality:
			return !fQualityArray.empty();
		case kDuplexEdge:
			return !fDuplexEdgeArray.empty();
		case PrinterCap::kScaleToFit:
			// "Scale to fit page" in the page setup, done by libprint
			return true;
		case kCopyCommand:
			// copies are requested from the printer (IPP "copies")
			return fCapabilities.maxCopies > 1;
		case kCollateCommand:
			return fCapabilities.collate;
		default:
			// kHalftone: 8 bit output, no dithering needed
			// kCanRotatePageInLandscape: libprint rotates the bands for us
			return false;
	}
}


const IPPMediaSize*
PWGCap::MediaSize(int paper) const
{
	for (size_t i = 0; i < fPapers.size(); i++) {
		if (fPapers[i]->fPaper == paper)
			return &fCapabilities.sizes[i];
	}
	return NULL;
}


const char*
PWGCap::MediaSizeName(int paper) const
{
	const IPPMediaSize* size = MediaSize(paper);
	return size == NULL ? "" : size->pwgName.c_str();
}


const char*
PWGCap::MediaSourceKeyword(int source) const
{
	if (source >= 0 && source < (int)fCapabilities.sources.size())
		return fCapabilities.sources[source].keyword.c_str();
	return "";
}


// #pragma mark - querying the printer


bool
PWGCap::Update(BNode& node, IPPCapabilities& capabilities, bool force)
{
	bool haveCache = capabilities.Load(node) == B_OK;
	bigtime_t now = real_time_clock_usecs();

	bool stale = !haveCache || !capabilities.fromPrinter
		|| now - capabilities.queried > kCacheLifetime;
	if (!stale && !force)
		return true;

	BString url;
	node.ReadAttrString(kTransportAddressAttribute, &url);
	if (url.Length() == 0 || url.IFindFirst("ipp") != 0) {
		if (!haveCache)
			capabilities.SetDefaults();
		return false;
	}

	// don't hold up every dialog while the printer is switched off
	int64 lastQuery = 0;
	node.ReadAttr(kLastQueryAttribute, B_INT64_TYPE, 0, &lastQuery,
		sizeof(lastQuery));
	if (!force && now - lastQuery < kRetryInterval) {
		if (!haveCache)
			capabilities.SetDefaults();
		return false;
	}
	node.WriteAttr(kLastQueryAttribute, B_INT64_TYPE, 0, &now, sizeof(now));

	IPPAttributes attributes;
	BString error;
	if (IPPClient::GetPrinterAttributes(url.String(), attributes, error,
			kQueryTimeout) != B_OK) {
		if (!haveCache)
			capabilities.SetDefaults();
		return false;
	}

	capabilities.SetFrom(attributes);
	capabilities.Save(node);

	// keep the raw answer for troubleshooting (listattr / catattr)
	std::string dump = attributes.Dump();
	node.WriteAttr(kRawAttributesAttribute, B_STRING_TYPE, 0, dump.c_str(),
		dump.size() + 1);
	return true;
}
