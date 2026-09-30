/*
 * PWGEntry.cpp
 * Entry point of the IPP Everywhere (PWG Raster) printer driver.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "AddIPPPrinterWindow.h"
#include "PWGCap.h"
#include "PWGDriver.h"
#include "PrinterData.h"
#include "PrinterDriver.h"

#include <Alert.h>
#include <Catalog.h>
#include <Node.h>
#include <String.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PWGEntry"


// attribute names used by print_server and the IPP transport
static const char* kTransportAttribute = "transport";
static const char* kTransportAddressAttribute = "transport_address";

// raster format chosen when the printer was added: auto, pwg-raster, urf
static const char* kFormatAttribute = "ipp-everywhere:format";

// the only transport this driver is used with (see AddPrinter)
static const char* kSupportedTransport = "IPP (network or USB)";


class PWGPrinterDriver : public PrinterDriver {
public:
	PWGPrinterDriver(BNode* printerFolder)
		:
		PrinterDriver(printerFolder)
	{
	}

	const char* GetSignature() const
	{
		return "application/x-vnd.ipp-everywhere";
	}

	const char* GetDriverName() const
	{
		return "IPP Everywhere";
	}

	const char* GetVersion() const
	{
		return "0.1";
	}

	const char* GetCopyright() const
	{
		return "IPP Everywhere (PWG Raster) driver.\n"
			"Built on libprint, Copyright 1999-2000 Y.Takagi, "
			"2003-2010 Michael Pfeiffer.\n";
	}

	PrinterCap* InstantiatePrinterCap(PrinterData* printerData)
	{
		return new PWGCap(printerData);
	}

	GraphicsDriver* InstantiateGraphicsDriver(BMessage* settings,
		PrinterData* printerData, PrinterCap* printerCap)
	{
		return new PWGDriver(settings, printerData, printerCap);
	}

	// Called by print_server once the printer folder exists. If the
	// printer uses an IPP transport, let the user pick it from the
	// network or type its URL, and store the URL where the transport
	// looks for it.
	char* AddPrinter(char* printerName)
	{
		std::string path;
		if (!GetPrinterData()->GetPath(path))
			return printerName;

		BNode node(path.c_str());
		if (node.InitCheck() != B_OK)
			return printerName;

		// The driver only works with its own transport; anything else
		// (Haiku's raw USB/serial/LPR transports, its stock IPP transport
		// that is broken on 64 bit) would just cause support questions.
		BString transport;
		node.ReadAttrString(kTransportAttribute, &transport);
		if (transport != kSupportedTransport) {
			BString text(B_TRANSLATE("The IPP Everywhere driver was not "
				"designed to work with the transport you selected "
				"(\"%transport%\").\n\nPlease add the printer again and "
				"choose the \"IPP (network or USB)\" transport, which works "
				"with both network and USB printers."));
			text.ReplaceFirst("%transport%", transport);
			BAlert* alert = new BAlert("", text.String(),
				B_TRANSLATE("Close"), NULL, NULL, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			alert->Go();
			return NULL;			// print_server removes the printer
		}

		BString currentURL;
		node.ReadAttrString(kTransportAddressAttribute, &currentURL);

		BString url;
		BString format;
		AddIPPPrinterWindow* window = new AddIPPPrinterWindow(
			currentURL.String(), &url, &format);
		if (window->Go() != B_OK)
			return NULL;			// cancelled: print_server removes the printer

		node.WriteAttrString(kTransportAddressAttribute, &url);
		node.WriteAttrString(kFormatAttribute, &format);

		// Ask the printer what it can do, so the dialogs are right from
		// the start. If it does not answer, generic defaults are used and
		// the driver retries later.
		IPPCapabilities capabilities;
		PWGCap::Update(node, capabilities, true);
		return printerName;
	}
};


PrinterDriver*
instantiate_printer_driver(BNode* printerFolder)
{
	return new PWGPrinterDriver(printerFolder);
}
