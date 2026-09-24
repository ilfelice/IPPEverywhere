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

#include <Node.h>
#include <String.h>


// attribute names used by print_server and the IPP transport
static const char* kTransportAttribute = "transport";
static const char* kTransportAddressAttribute = "transport_address";


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

		BString transport;
		node.ReadAttrString(kTransportAttribute, &transport);
		if (transport.IFindFirst("IPP") < 0)
			return printerName;		// e.g. USB: nothing to configure

		BString currentURL;
		node.ReadAttrString(kTransportAddressAttribute, &currentURL);

		BString url;
		AddIPPPrinterWindow* window = new AddIPPPrinterWindow(
			currentURL.String(), &url);
		if (window->Go() != B_OK)
			return NULL;			// cancelled: print_server removes the printer

		node.WriteAttrString(kTransportAddressAttribute, &url);

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
