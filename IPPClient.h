/*
 * IPPClient.h
 * Minimal IPP client: sends Get-Printer-Attributes and parses the reply.
 * Plain sockets, no dependencies.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */
#ifndef IPP_CLIENT_H
#define IPP_CLIENT_H


#include <OS.h>
#include <String.h>

#include <map>
#include <string>
#include <vector>


struct IPPValue {
	uint8		tag;		// IPP value tag (0x21 integer, 0x44 keyword...)
	int32		integer;	// integer, enum, boolean; xres for resolution
	int32		integer2;	// yres for resolution; upper for ranges
	std::string	text;		// strings, keywords, URIs, mimeMediaTypes

	IPPValue() : tag(0), integer(0), integer2(0) {}
};


class IPPAttributes {
public:
			bool				Has(const char* name) const;
			int					Count(const char* name) const;
			const IPPValue*		Value(const char* name, int index = 0) const;
			// first value as text ("" if missing); integers are formatted
			std::string			String(const char* name) const;
			int32				Integer(const char* name, int32 fallback) const;
			bool				Contains(const char* name, const char* keyword)
									const;
			std::vector<std::string>	Strings(const char* name) const;

			void				Add(const std::string& name,
									const IPPValue& value);
			// human readable dump, one attribute per line
			std::string			Dump() const;

private:
	typedef std::map<std::string, std::vector<IPPValue> > Map;
			Map					fAttributes;
			std::vector<std::string>	fOrder;
};


class IPPClient {
public:
	// url: ipp://host[:port]/path. timeout applies to connect and to each
	// read. Returns B_OK and fills attributes, or an error; errorText
	// gets a short explanation.
	static	status_t			GetPrinterAttributes(const char* url,
									IPPAttributes& attributes,
									BString& errorText,
									bigtime_t timeout = 4000000);

	static	bool				ParseURL(const char* url, std::string& host,
									uint16& port, std::string& path);

	// how a printer is addressed over IPP-USB
	static	const char*			kUSBRequestURI;
	static	const char*			kUSBRequestPath;
	static	const char*			kUSBRequestHost;
};


#endif // IPP_CLIENT_H
