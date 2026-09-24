/*
 * IPPDiscovery.h
 * Finds IPP printers on the local network with a minimal mDNS / DNS-SD
 * browse for "_ipp._tcp.local". No external dependencies, plain sockets.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */
#ifndef IPP_DISCOVERY_H
#define IPP_DISCOVERY_H


#include <OS.h>
#include <String.h>

#include <vector>


struct DiscoveredPrinter {
	BString	name;		// service instance name, e.g. "EPSON EW-M630T Series"
	BString	model;		// "ty" TXT entry, may be empty
	BString	host;		// SRV target, e.g. "EPSONDB91.local"
	BString	address;	// IPv4 address as text, may be empty if not resolved
	uint16	port;		// SRV port, usually 631
	BString	path;		// "rp" TXT entry, e.g. "ipp/print"
	BString	formats;	// "pdl" TXT entry, comma separated MIME types

	BString	URL() const;
	bool	IsComplete() const;
};


class IPPDiscovery {
public:
	// Browses for about `timeout` microseconds and fills `printers`.
	// Returns B_OK even if nothing was found; an error only if the
	// sockets could not be set up.
	static	status_t	Browse(std::vector<DiscoveredPrinter>& printers,
							bigtime_t timeout = 3000000);
};


#endif // IPP_DISCOVERY_H
