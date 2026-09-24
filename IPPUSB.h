/*
 * IPPUSB.h
 * IPP over USB (USB-IF "IPP-USB" 1.0): the same HTTP/IPP exchange as over
 * the network, carried over a USB printer interface with protocol 4.
 * Uses Haiku's USB Kit (libdevice) directly, no kernel driver involved.
 * Shared by the driver and the IPP transport.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */
#ifndef IPP_USB_H
#define IPP_USB_H


#include <OS.h>
#include <String.h>

#include <vector>


struct IPPUSBPrinter {
	BString	devicePath;		// /dev/bus/usb/...
	BString	manufacturer;
	BString	product;
	BString	serial;
	uint16	vendorID;
	uint16	productID;

	BString	URL() const;	// ipp-usb://<vendor>:<product>/<serial>
	BString	Name() const;	// "<manufacturer> <product>"
};


class IPPUSB {
public:
	static	bool		IsUSBURL(const char* url);
	// all connected printers with an IPP-USB interface
	static	void		Enumerate(std::vector<IPPUSBPrinter>& printers);
	// the printer a URL refers to; false if it is not connected
	static	bool		Find(const char* url, IPPUSBPrinter& printer);

	// One HTTP request/response over IPP-USB. `request` is the complete
	// HTTP request (headers and body); `response` receives the complete
	// HTTP response. Returns B_OK, or an error with `error` filled in.
	static	status_t	Exchange(const char* url,
							const std::vector<uint8>& request,
							std::vector<uint8>& response, BString& error);

	// Length of a complete HTTP message in `data`, 0 if more is needed
	// (Content-Length and chunked transfer encoding are understood).
	static	size_t		CompleteMessageLength(const std::vector<uint8>& data);
};


#endif // IPP_USB_H
