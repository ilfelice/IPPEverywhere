/*
 * IPPDiscovery.cpp
 * Minimal mDNS / DNS-SD browser for "_ipp._tcp.local" (RFC 6762, 6763).
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "IPPDiscovery.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>


BString
DiscoveredPrinter::URL() const
{
	BString url("ipp://");
	url << (address.Length() > 0 ? address : host) << ":" << port << "/"
		<< path;
	return url;
}


bool
DiscoveredPrinter::IsComplete() const
{
	return name.Length() > 0 && port > 0 && path.Length() > 0
		&& (address.Length() > 0 || host.Length() > 0);
}


namespace {

const char* kMulticastAddress = "224.0.0.251";
const uint16 kMDNSPort = 5353;
const char* kService = "_ipp._tcp.local";

enum {
	kTypeA = 1,
	kTypePTR = 12,
	kTypeTXT = 16,
	kTypeSRV = 33
};


struct Instance {
	std::string	name;		// display name (instance label)
	std::string	fullName;	// "<label>._ipp._tcp.local"
	std::string	host;
	uint16		port;
	bool		haveSRV;
	bool		haveTXT;
	std::map<std::string, std::string> txt;

	Instance() : port(0), haveSRV(false), haveTXT(false) {}
};


std::string
ToLower(const std::string& s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); i++)
		r[i] = tolower((unsigned char)r[i]);
	return r;
}


// Reads a possibly compressed DNS name at `pos`. Labels are joined with
// '.' (raw bytes, no escaping). Advances `pos` past the name in the
// message (not past a pointer target).
bool
ReadName(const uint8* msg, size_t length, size_t& pos, std::string& out,
	int depth = 0)
{
	if (depth > 16)
		return false;

	while (true) {
		if (pos >= length)
			return false;
		uint8 l = msg[pos];
		if (l == 0) {
			pos++;
			return true;
		}
		if ((l & 0xC0) == 0xC0) {
			if (pos + 1 >= length)
				return false;
			size_t target = ((l & 0x3F) << 8) | msg[pos + 1];
			pos += 2;
			if (target >= length)
				return false;
			return ReadName(msg, length, target, out, depth + 1);
		}
		pos++;
		if (pos + l > length)
			return false;
		if (!out.empty())
			out += '.';
		out.append((const char*)msg + pos, l);
		pos += l;
	}
}


void
AppendLabel(std::vector<uint8>& out, const std::string& label)
{
	size_t n = label.size();
	if (n > 63)
		n = 63;
	out.push_back((uint8)n);
	out.insert(out.end(), label.begin(), label.begin() + n);
}


// Appends a dotted name; `firstLabel`, if not empty, is taken as one
// label even if it contains dots (service instance names may).
void
AppendName(std::vector<uint8>& out, const std::string& firstLabel,
	const std::string& dotted)
{
	if (!firstLabel.empty())
		AppendLabel(out, firstLabel);
	size_t start = 0;
	while (start < dotted.size()) {
		size_t dot = dotted.find('.', start);
		if (dot == std::string::npos)
			dot = dotted.size();
		if (dot > start)
			AppendLabel(out, dotted.substr(start, dot - start));
		start = dot + 1;
	}
	out.push_back(0);
}


void
AppendU16(std::vector<uint8>& out, uint16 value)
{
	out.push_back((uint8)(value >> 8));
	out.push_back((uint8)value);
}


struct Question {
	std::string	firstLabel;
	std::string	dotted;
	uint16		type;
};


std::vector<uint8>
BuildQuery(const std::vector<Question>& questions, bool unicastResponse)
{
	std::vector<uint8> q;
	AppendU16(q, 0);		// id
	AppendU16(q, 0);		// flags: standard query
	AppendU16(q, questions.size());
	AppendU16(q, 0);
	AppendU16(q, 0);
	AppendU16(q, 0);
	for (size_t i = 0; i < questions.size(); i++) {
		AppendName(q, questions[i].firstLabel, questions[i].dotted);
		AppendU16(q, questions[i].type);
		AppendU16(q, unicastResponse ? 0x8001 : 0x0001);
	}
	return q;
}


class Browser {
public:
	Browser()
		:
		fSocket(-1),
		fUnicast(false)
	{
	}

	~Browser()
	{
		if (fSocket >= 0)
			close(fSocket);
	}

	status_t Open()
	{
		fSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (fSocket < 0)
			return B_ERROR;

		int one = 1;
		setsockopt(fSocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
		setsockopt(fSocket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif

		sockaddr_in local;
		memset(&local, 0, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		local.sin_port = htons(kMDNSPort);
		if (bind(fSocket, (sockaddr*)&local, sizeof(local)) != 0) {
			// Port taken (e.g. by mdnsd): use any port and ask the
			// responders to answer us directly.
			local.sin_port = 0;
			if (bind(fSocket, (sockaddr*)&local, sizeof(local)) != 0)
				return B_ERROR;
			fUnicast = true;
		}

		ip_mreq mreq;
		memset(&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr.s_addr = inet_addr(kMulticastAddress);
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		setsockopt(fSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
			sizeof(mreq));

		memset(&fGroup, 0, sizeof(fGroup));
		fGroup.sin_family = AF_INET;
		fGroup.sin_addr.s_addr = inet_addr(kMulticastAddress);
		fGroup.sin_port = htons(kMDNSPort);
		return B_OK;
	}

	void Send(const std::vector<Question>& questions)
	{
		if (questions.empty())
			return;
		std::vector<uint8> query = BuildQuery(questions, fUnicast);
		sendto(fSocket, &query[0], query.size(), 0, (sockaddr*)&fGroup,
			sizeof(fGroup));
	}

	bool Receive(bigtime_t wait)
	{
		fd_set set;
		FD_ZERO(&set);
		FD_SET(fSocket, &set);
		timeval tv;
		tv.tv_sec = wait / 1000000;
		tv.tv_usec = wait % 1000000;
		if (select(fSocket + 1, &set, NULL, NULL, &tv) <= 0)
			return false;

		uint8 buffer[9000];
		ssize_t n = recvfrom(fSocket, buffer, sizeof(buffer), 0, NULL, NULL);
		if (n <= 12)
			return true;
		_Parse(buffer, n);
		return true;
	}

	std::map<std::string, Instance>& Instances() { return fInstances; }
	std::map<std::string, std::string>& Hosts() { return fHosts; }

private:
	void _Parse(const uint8* msg, size_t length)
	{
		uint16 flags = (msg[2] << 8) | msg[3];
		if ((flags & 0x8000) == 0)
			return;		// a query, not a response
		uint16 qdCount = (msg[4] << 8) | msg[5];
		uint16 rrCount = ((msg[6] << 8) | msg[7]) + ((msg[8] << 8) | msg[9])
			+ ((msg[10] << 8) | msg[11]);
		size_t pos = 12;

		for (uint16 i = 0; i < qdCount; i++) {
			std::string name;
			if (!ReadName(msg, length, pos, name))
				return;
			pos += 4;
		}

		for (uint16 i = 0; i < rrCount; i++) {
			std::string name;
			if (!ReadName(msg, length, pos, name))
				return;
			if (pos + 10 > length)
				return;
			uint16 type = (msg[pos] << 8) | msg[pos + 1];
			pos += 8;	// type, class, ttl
			uint16 rdLength = (msg[pos] << 8) | msg[pos + 1];
			pos += 2;
			if (pos + rdLength > length)
				return;
			size_t rdPos = pos;
			_HandleRecord(msg, length, name, type, rdPos, rdLength);
			pos += rdLength;
		}
	}

	void _HandleRecord(const uint8* msg, size_t length, const std::string& name,
		uint16 type, size_t rdPos, uint16 rdLength)
	{
		std::string key = ToLower(name);
		std::string serviceSuffix = std::string(".") + kService;

		switch (type) {
			case kTypePTR:
			{
				if (key != kService)
					return;
				std::string target;
				size_t p = rdPos;
				if (!ReadName(msg, length, p, target))
					return;
				Instance& instance = fInstances[ToLower(target)];
				if (instance.fullName.empty()) {
					instance.fullName = target;
					instance.name = target;
					size_t suffix = ToLower(target).rfind(serviceSuffix);
					if (suffix != std::string::npos)
						instance.name = target.substr(0, suffix);
				}
				break;
			}
			case kTypeSRV:
			{
				if (rdLength < 7)
					return;
				std::map<std::string, Instance>::iterator it
					= fInstances.find(key);
				if (it == fInstances.end()) {
					// SRV may arrive before or without the PTR
					if (key.size() <= serviceSuffix.size()
						|| key.compare(key.size() - serviceSuffix.size(),
							serviceSuffix.size(), serviceSuffix) != 0)
						return;
					Instance& instance = fInstances[key];
					instance.fullName = name;
					instance.name = name.substr(0,
						name.size() - serviceSuffix.size());
					it = fInstances.find(key);
				}
				Instance& instance = it->second;
				instance.port = (msg[rdPos + 4] << 8) | msg[rdPos + 5];
				size_t p = rdPos + 6;
				std::string target;
				if (ReadName(msg, length, p, target)) {
					instance.host = target;
					instance.haveSRV = true;
				}
				break;
			}
			case kTypeTXT:
			{
				std::map<std::string, Instance>::iterator it
					= fInstances.find(key);
				if (it == fInstances.end())
					return;
				Instance& instance = it->second;
				size_t p = rdPos;
				size_t end = rdPos + rdLength;
				while (p < end) {
					uint8 l = msg[p++];
					if (p + l > end)
						break;
					std::string entry((const char*)msg + p, l);
					p += l;
					size_t eq = entry.find('=');
					if (eq == std::string::npos)
						instance.txt[ToLower(entry)] = "";
					else
						instance.txt[ToLower(entry.substr(0, eq))]
							= entry.substr(eq + 1);
				}
				instance.haveTXT = true;
				break;
			}
			case kTypeA:
			{
				if (rdLength != 4)
					return;
				char text[32];
				snprintf(text, sizeof(text), "%u.%u.%u.%u", msg[rdPos],
					msg[rdPos + 1], msg[rdPos + 2], msg[rdPos + 3]);
				fHosts[key] = text;
				break;
			}
			default:
				break;
		}
	}

	int								fSocket;
	bool							fUnicast;
	sockaddr_in						fGroup;
	std::map<std::string, Instance>	fInstances;
	std::map<std::string, std::string> fHosts;
};

}	// namespace


status_t
IPPDiscovery::Browse(std::vector<DiscoveredPrinter>& printers,
	bigtime_t timeout)
{
	printers.clear();

	Browser browser;
	status_t status = browser.Open();
	if (status != B_OK)
		return status;

	std::vector<Question> initial;
	Question ptr;
	ptr.dotted = kService;
	ptr.type = kTypePTR;
	initial.push_back(ptr);
	browser.Send(initial);

	bigtime_t start = system_time();
	bigtime_t followUpAt = start + timeout / 2;
	bool followUpSent = false;

	while (true) {
		bigtime_t now = system_time();
		if (now >= start + timeout)
			break;

		if (!followUpSent && now >= followUpAt) {
			// Ask explicitly for whatever the responders left out.
			followUpSent = true;
			std::vector<Question> questions;
			std::map<std::string, Instance>& instances = browser.Instances();
			std::map<std::string, Instance>::iterator it;
			for (it = instances.begin(); it != instances.end(); it++) {
				Instance& instance = it->second;
				if (!instance.haveSRV || !instance.haveTXT) {
					Question q;
					q.firstLabel = instance.name;
					q.dotted = kService;
					q.type = kTypeSRV;
					questions.push_back(q);
					q.type = kTypeTXT;
					questions.push_back(q);
				}
				if (!instance.host.empty()
					&& browser.Hosts().find(ToLower(instance.host))
						== browser.Hosts().end()) {
					Question q;
					q.dotted = instance.host;
					q.type = kTypeA;
					questions.push_back(q);
				}
			}
			if (!questions.empty()) {
				browser.Send(questions);
				// mDNS resends of the initial query are allowed too
				browser.Send(initial);
			}
		}

		bigtime_t wait = std::min((bigtime_t)200000, start + timeout - now);
		browser.Receive(wait);
	}

	std::map<std::string, Instance>& instances = browser.Instances();
	std::map<std::string, Instance>::iterator it;
	for (it = instances.begin(); it != instances.end(); it++) {
		Instance& instance = it->second;
		DiscoveredPrinter printer;
		printer.name = instance.name.c_str();
		printer.host = instance.host.c_str();
		printer.port = instance.port;
		std::map<std::string, std::string>::iterator host
			= browser.Hosts().find(ToLower(instance.host));
		if (host != browser.Hosts().end())
			printer.address = host->second.c_str();
		if (instance.txt.count("rp"))
			printer.path = instance.txt["rp"].c_str();
		if (instance.txt.count("ty"))
			printer.model = instance.txt["ty"].c_str();
		if (instance.txt.count("pdl"))
			printer.formats = instance.txt["pdl"].c_str();
		if (printer.IsComplete())
			printers.push_back(printer);
	}

	return B_OK;
}
