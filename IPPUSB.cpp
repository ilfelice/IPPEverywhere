/*
 * IPPUSB.cpp
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "IPPUSB.h"

#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <USBKit.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <string>


static const char* kScheme = "ipp-usb://";
static const char* kDeviceRoot = "/dev/bus/usb";

// USB printer class, IPP-USB protocol
static const uint8 kPrinterClass = 7;
static const uint8 kPrinterSubclass = 1;
static const uint8 kIPPUSBProtocol = 4;

static const size_t kTransferSize = 16384;
static const size_t kMaxResponse = 16 * 1024 * 1024;


BString
IPPUSBPrinter::URL() const
{
	BString url(kScheme);
	char ids[16];
	snprintf(ids, sizeof(ids), "%04x:%04x", vendorID, productID);
	url << ids << "/" << serial;
	return url;
}


BString
IPPUSBPrinter::Name() const
{
	BString name(manufacturer);
	if (name.Length() > 0 && product.Length() > 0
		&& product.IFindFirst(manufacturer) != 0)
		name << " ";
	else
		name = "";
	name << product;
	return name;
}


// #pragma mark - enumeration


bool
IPPUSB::IsUSBURL(const char* url)
{
	return url != NULL && strncasecmp(url, kScheme, strlen(kScheme)) == 0;
}


// Whether an interface (any of its alternates) speaks IPP-USB. Returns the
// alternate index, or -1.
static int32
FindIPPUSBAlternate(const BUSBInterface* interface)
{
	uint32 count = interface->CountAlternates();
	for (uint32 i = 0; i < count; i++) {
		const BUSBInterface* alternate = interface->AlternateAt(i);
		if (alternate == NULL)
			continue;
		if (alternate->Class() == kPrinterClass
			&& alternate->Subclass() == kPrinterSubclass
			&& alternate->Protocol() == kIPPUSBProtocol)
			return i;
	}
	return -1;
}


static bool
HasIPPUSB(BUSBDevice& device)
{
	const BUSBConfiguration* config = device.ActiveConfiguration();
	if (config == NULL)
		return false;
	uint32 count = config->CountInterfaces();
	for (uint32 i = 0; i < count; i++) {
		const BUSBInterface* interface = config->InterfaceAt(i);
		if (interface != NULL && FindIPPUSBAlternate(interface) >= 0)
			return true;
	}
	return false;
}


static void
ScanDirectory(const char* path, std::vector<IPPUSBPrinter>& printers)
{
	BDirectory directory(path);
	if (directory.InitCheck() != B_OK)
		return;
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		BPath entryPath;
		if (entry.GetPath(&entryPath) != B_OK)
			continue;
		if (entry.IsDirectory()) {
			ScanDirectory(entryPath.Path(), printers);
			continue;
		}
		BUSBDevice device(entryPath.Path());
		if (device.InitCheck() != B_OK || device.IsHub())
			continue;
		if (!HasIPPUSB(device))
			continue;

		IPPUSBPrinter printer;
		printer.devicePath = entryPath.Path();
		printer.manufacturer = device.ManufacturerString();
		printer.product = device.ProductString();
		printer.serial = device.SerialNumberString();
		printer.vendorID = device.VendorID();
		printer.productID = device.ProductID();
		printers.push_back(printer);
	}
}


void
IPPUSB::Enumerate(std::vector<IPPUSBPrinter>& printers)
{
	printers.clear();
	ScanDirectory(kDeviceRoot, printers);
}


bool
IPPUSB::Find(const char* url, IPPUSBPrinter& printer)
{
	if (!IsUSBURL(url))
		return false;
	unsigned int vendor = 0;
	unsigned int product = 0;
	const char* rest = url + strlen(kScheme);
	if (sscanf(rest, "%4x:%4x", &vendor, &product) != 2)
		return false;
	const char* slash = strchr(rest, '/');
	BString serial(slash != NULL ? slash + 1 : "");

	std::vector<IPPUSBPrinter> printers;
	Enumerate(printers);
	for (size_t i = 0; i < printers.size(); i++) {
		if (printers[i].vendorID != vendor || printers[i].productID != product)
			continue;
		if (serial.Length() > 0 && printers[i].serial != serial)
			continue;
		printer = printers[i];
		return true;
	}
	return false;
}


// #pragma mark - HTTP framing


static std::string
ToLower(const std::string& s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); i++)
		r[i] = tolower((unsigned char)r[i]);
	return r;
}


size_t
IPPUSB::CompleteMessageLength(const std::vector<uint8>& data)
{
	if (data.size() < 4)
		return 0;
	std::string text(data.begin(), data.end());
	size_t headerEnd = text.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return 0;
	size_t bodyStart = headerEnd + 4;
	std::string header = ToLower(text.substr(0, headerEnd));

	size_t p = header.find("content-length:");
	if (p != std::string::npos) {
		size_t length = strtoul(header.c_str() + p + 15, NULL, 10);
		return data.size() >= bodyStart + length ? bodyStart + length : 0;
	}

	if (header.find("transfer-encoding: chunked") != std::string::npos) {
		size_t pos = bodyStart;
		while (true) {
			size_t lineEnd = text.find("\r\n", pos);
			if (lineEnd == std::string::npos)
				return 0;
			long size = strtol(text.c_str() + pos, NULL, 16);
			pos = lineEnd + 2;
			if (size == 0) {
				// optional trailers, then an empty line
				size_t end = text.find("\r\n", pos);
				if (end == std::string::npos)
					return 0;
				if (end == pos)
					return pos + 2;
				end = text.find("\r\n\r\n", pos);
				return end == std::string::npos ? 0 : end + 4;
			}
			pos += size + 2;
			if (pos > data.size())
				return 0;
		}
	}

	// no body length given: the headers are all there is
	return bodyStart;
}


// #pragma mark - exchange


status_t
IPPUSB::Exchange(const char* url, const std::vector<uint8>& request,
	std::vector<uint8>& response, BString& error)
{
	response.clear();

	IPPUSBPrinter printer;
	if (!Find(url, printer)) {
		error = "USB printer not connected";
		return B_ENTRY_NOT_FOUND;
	}

	BUSBDevice device(printer.devicePath.String());
	if (device.InitCheck() != B_OK) {
		error = "Cannot open USB device";
		return B_ERROR;
	}
	const BUSBConfiguration* config = device.ActiveConfiguration();
	if (config == NULL) {
		error = "USB device has no active configuration";
		return B_ERROR;
	}

	// Pick an IPP-USB interface. Prefer one whose default alternate is not
	// a printer class interface: that one is claimed by the kernel's
	// usb_printer driver for the classic print channel.
	const BUSBInterface* chosen = NULL;
	int32 chosenAlternate = -1;
	uint32 count = config->CountInterfaces();
	for (uint32 i = 0; i < count; i++) {
		const BUSBInterface* interface = config->InterfaceAt(i);
		if (interface == NULL)
			continue;
		int32 alternate = FindIPPUSBAlternate(interface);
		if (alternate < 0)
			continue;
		const BUSBInterface* first = interface->AlternateAt(0);
		bool classic = first != NULL && first->Class() == kPrinterClass
			&& first->Protocol() != kIPPUSBProtocol;
		if (chosen == NULL || !classic) {
			chosen = interface;
			chosenAlternate = alternate;
			if (!classic)
				break;
		}
	}
	if (chosen == NULL) {
		error = "No IPP-USB interface";
		return B_ERROR;
	}

	BUSBInterface* interface = const_cast<BUSBInterface*>(chosen);
	if (interface->ActiveAlternateIndex() != (uint32)chosenAlternate) {
		if (interface->SetAlternate(chosenAlternate) != B_OK) {
			error = "Cannot select the IPP-USB interface";
			return B_ERROR;
		}
	}

	const BUSBEndpoint* in = NULL;
	const BUSBEndpoint* out = NULL;
	uint32 endpoints = interface->CountEndpoints();
	for (uint32 i = 0; i < endpoints; i++) {
		const BUSBEndpoint* endpoint = interface->EndpointAt(i);
		if (endpoint == NULL || !endpoint->IsBulk())
			continue;
		if (endpoint->IsInput() && in == NULL)
			in = endpoint;
		if (endpoint->IsOutput() && out == NULL)
			out = endpoint;
	}
	if (in == NULL || out == NULL) {
		error = "IPP-USB interface has no bulk endpoints";
		return B_ERROR;
	}

	// send
	size_t sent = 0;
	while (sent < request.size()) {
		size_t chunk = request.size() - sent;
		if (chunk > kTransferSize)
			chunk = kTransferSize;
		ssize_t n = out->BulkTransfer((void*)&request[sent], chunk);
		if (n < 0) {
			error = "USB write failed";
			return B_ERROR;
		}
		sent += n;
	}

	// receive until the HTTP message is complete
	std::vector<uint8> buffer(kTransferSize);
	int emptyReads = 0;
	while (true) {
		ssize_t n = in->BulkTransfer(&buffer[0], buffer.size());
		if (n < 0) {
			error = "USB read failed";
			return B_ERROR;
		}
		if (n == 0) {
			if (++emptyReads > 1000) {
				error = "No response from the USB printer";
				return B_TIMED_OUT;
			}
			snooze(10000);
			continue;
		}
		emptyReads = 0;
		response.insert(response.end(), buffer.begin(), buffer.begin() + n);
		if (CompleteMessageLength(response) > 0)
			break;
		if (response.size() > kMaxResponse) {
			error = "USB response too large";
			return B_ERROR;
		}
	}
	return B_OK;
}
