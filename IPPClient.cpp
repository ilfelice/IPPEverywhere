/*
 * IPPClient.cpp
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "IPPClient.h"
#include "IPPUSB.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// #pragma mark - IPPAttributes


bool
IPPAttributes::Has(const char* name) const
{
	return fAttributes.find(name) != fAttributes.end();
}


int
IPPAttributes::Count(const char* name) const
{
	Map::const_iterator it = fAttributes.find(name);
	return it == fAttributes.end() ? 0 : (int)it->second.size();
}


const IPPValue*
IPPAttributes::Value(const char* name, int index) const
{
	Map::const_iterator it = fAttributes.find(name);
	if (it == fAttributes.end() || index < 0
		|| index >= (int)it->second.size())
		return NULL;
	return &it->second[index];
}


static std::string
ValueToString(const IPPValue& value)
{
	char buffer[64];
	switch (value.tag) {
		case 0x21:		// integer
		case 0x23:		// enum
			snprintf(buffer, sizeof(buffer), "%d", (int)value.integer);
			return buffer;
		case 0x22:		// boolean
			return value.integer ? "true" : "false";
		case 0x32:		// resolution
			snprintf(buffer, sizeof(buffer), "%dx%d%s", (int)value.integer,
				(int)value.integer2, value.text.c_str());
			return buffer;
		case 0x33:		// rangeOfInteger
			snprintf(buffer, sizeof(buffer), "%d-%d", (int)value.integer,
				(int)value.integer2);
			return buffer;
		default:
			return value.text;
	}
}


std::string
IPPAttributes::String(const char* name) const
{
	const IPPValue* value = Value(name);
	return value == NULL ? std::string() : ValueToString(*value);
}


int32
IPPAttributes::Integer(const char* name, int32 fallback) const
{
	const IPPValue* value = Value(name);
	if (value == NULL)
		return fallback;
	if (value->tag == 0x21 || value->tag == 0x23 || value->tag == 0x22)
		return value->integer;
	return fallback;
}


bool
IPPAttributes::Contains(const char* name, const char* keyword) const
{
	Map::const_iterator it = fAttributes.find(name);
	if (it == fAttributes.end())
		return false;
	for (size_t i = 0; i < it->second.size(); i++) {
		if (it->second[i].text == keyword)
			return true;
	}
	return false;
}


std::vector<std::string>
IPPAttributes::Strings(const char* name) const
{
	std::vector<std::string> result;
	Map::const_iterator it = fAttributes.find(name);
	if (it == fAttributes.end())
		return result;
	for (size_t i = 0; i < it->second.size(); i++)
		result.push_back(ValueToString(it->second[i]));
	return result;
}


void
IPPAttributes::Add(const std::string& name, const IPPValue& value)
{
	if (fAttributes.find(name) == fAttributes.end())
		fOrder.push_back(name);
	fAttributes[name].push_back(value);
}


std::string
IPPAttributes::Dump() const
{
	std::string out;
	for (size_t i = 0; i < fOrder.size(); i++) {
		out += fOrder[i];
		out += ": ";
		std::vector<std::string> values = Strings(fOrder[i].c_str());
		for (size_t j = 0; j < values.size(); j++) {
			if (j > 0)
				out += ", ";
			out += values[j];
		}
		out += "\n";
	}
	return out;
}


// #pragma mark - IPPClient


bool
IPPClient::ParseURL(const char* url, std::string& host, uint16& port,
	std::string& path)
{
	std::string s(url);
	size_t p = s.find("://");
	if (p == std::string::npos)
		return false;
	std::string scheme = s.substr(0, p);
	s = s.substr(p + 3);
	port = scheme == "ipps" || scheme == "https" ? 443 : 631;
	if (scheme == "http")
		port = 80;

	size_t slash = s.find('/');
	std::string hostPort = slash == std::string::npos ? s : s.substr(0, slash);
	path = slash == std::string::npos ? "/" : s.substr(slash);

	size_t colon = hostPort.rfind(':');
	if (colon != std::string::npos && hostPort.find(']') == std::string::npos) {
		host = hostPort.substr(0, colon);
		port = (uint16)atoi(hostPort.c_str() + colon + 1);
	} else
		host = hostPort;

	return !host.empty();
}


namespace {

void
PutU16(std::vector<uint8>& out, uint16 value)
{
	out.push_back((uint8)(value >> 8));
	out.push_back((uint8)value);
}


void
PutAttribute(std::vector<uint8>& out, uint8 tag, const char* name,
	const std::string& value)
{
	out.push_back(tag);
	PutU16(out, strlen(name));
	out.insert(out.end(), name, name + strlen(name));
	PutU16(out, value.size());
	out.insert(out.end(), value.begin(), value.end());
}


std::vector<uint8>
BuildRequest(const std::string& uri)
{
	std::vector<uint8> r;
	r.push_back(2);				// IPP 2.0
	r.push_back(0);
	PutU16(r, 0x000B);			// Get-Printer-Attributes
	r.push_back(0); r.push_back(0); r.push_back(0); r.push_back(1);
	r.push_back(0x01);			// operation attributes
	PutAttribute(r, 0x47, "attributes-charset", "utf-8");
	PutAttribute(r, 0x48, "attributes-natural-language", "en");
	PutAttribute(r, 0x45, "printer-uri", uri);
	r.push_back(0x03);			// end of attributes
	return r;
}


class Connection {
public:
	Connection() : fSocket(-1) {}
	~Connection() { if (fSocket >= 0) close(fSocket); }

	status_t Connect(const std::string& host, uint16 port, bigtime_t timeout,
		BString& error)
	{
		addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		char portText[16];
		snprintf(portText, sizeof(portText), "%u", port);
		addrinfo* result = NULL;
		if (getaddrinfo(host.c_str(), portText, &hints, &result) != 0
			|| result == NULL) {
			error << "Cannot resolve " << host.c_str();
			return B_ERROR;
		}

		fSocket = socket(result->ai_family, result->ai_socktype, 0);
		if (fSocket < 0) {
			freeaddrinfo(result);
			error = "Cannot create socket";
			return B_ERROR;
		}

		int flags = fcntl(fSocket, F_GETFL, 0);
		fcntl(fSocket, F_SETFL, flags | O_NONBLOCK);
		int rc = connect(fSocket, result->ai_addr, result->ai_addrlen);
		freeaddrinfo(result);
		if (rc != 0 && errno != EINPROGRESS && errno != EWOULDBLOCK
			&& errno != EINTR) {
			error << "Cannot connect to " << host.c_str();
			return B_ERROR;
		}
		if (rc != 0) {
			fd_set set;
			FD_ZERO(&set);
			FD_SET(fSocket, &set);
			timeval tv;
			tv.tv_sec = timeout / 1000000;
			tv.tv_usec = timeout % 1000000;
			if (select(fSocket + 1, NULL, &set, NULL, &tv) <= 0) {
				error << "Timeout connecting to " << host.c_str();
				return B_TIMED_OUT;
			}
			int soError = 0;
			socklen_t length = sizeof(soError);
			getsockopt(fSocket, SOL_SOCKET, SO_ERROR, &soError, &length);
			if (soError != 0) {
				error << "Cannot connect to " << host.c_str();
				return B_ERROR;
			}
		}
		fcntl(fSocket, F_SETFL, flags);

		timeval tv;
		tv.tv_sec = timeout / 1000000;
		tv.tv_usec = timeout % 1000000;
		setsockopt(fSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(fSocket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		return B_OK;
	}

	bool Send(const std::vector<uint8>& data)
	{
		size_t sent = 0;
		while (sent < data.size()) {
			ssize_t n = send(fSocket, &data[sent], data.size() - sent, 0);
			if (n <= 0)
				return false;
			sent += n;
		}
		return true;
	}

	// reads until the peer closes the connection
	std::vector<uint8> ReceiveAll()
	{
		std::vector<uint8> data;
		uint8 buffer[16384];
		while (true) {
			ssize_t n = recv(fSocket, buffer, sizeof(buffer), 0);
			if (n <= 0)
				break;
			data.insert(data.end(), buffer, buffer + n);
			if (data.size() > 4 * 1024 * 1024)
				break;
		}
		return data;
	}

private:
	int fSocket;
};


std::string
ToLower(const std::string& s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); i++)
		r[i] = tolower((unsigned char)r[i]);
	return r;
}


// Splits an HTTP response into status code and body, decoding chunked
// transfer encoding if needed.
bool
ParseHTTP(const std::vector<uint8>& data, int& status,
	std::vector<uint8>& body)
{
	std::string text(data.begin(), data.end());
	size_t headerEnd = text.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return false;
	std::string header = text.substr(0, headerEnd);
	size_t bodyStart = headerEnd + 4;

	status = 0;
	size_t space = header.find(' ');
	if (space != std::string::npos)
		status = atoi(header.c_str() + space + 1);

	bool chunked = ToLower(header).find("transfer-encoding: chunked")
		!= std::string::npos;

	if (!chunked) {
		body.assign(data.begin() + bodyStart, data.end());
		return true;
	}

	size_t pos = bodyStart;
	while (pos < data.size()) {
		size_t lineEnd = text.find("\r\n", pos);
		if (lineEnd == std::string::npos)
			break;
		long size = strtol(text.c_str() + pos, NULL, 16);
		pos = lineEnd + 2;
		if (size <= 0)
			break;
		if (pos + size > data.size())
			size = data.size() - pos;
		body.insert(body.end(), data.begin() + pos, data.begin() + pos + size);
		pos += size + 2;
	}
	return true;
}


uint16
GetU16(const std::vector<uint8>& d, size_t pos)
{
	return (d[pos] << 8) | d[pos + 1];
}


int32
GetI32(const std::vector<uint8>& d, size_t pos)
{
	return (int32)(((uint32)d[pos] << 24) | ((uint32)d[pos + 1] << 16)
		| ((uint32)d[pos + 2] << 8) | d[pos + 3]);
}


// Parses the attribute groups of an IPP response. Collections
// (begCollection ... endCollection) are skipped.
bool
ParseIPP(const std::vector<uint8>& body, uint16& ippStatus,
	IPPAttributes& attributes)
{
	if (body.size() < 9)
		return false;
	ippStatus = GetU16(body, 2);
	size_t pos = 8;
	std::string name;
	int depth = 0;

	while (pos < body.size()) {
		uint8 tag = body[pos++];
		if (tag == 0x03)
			break;
		if (tag < 0x10)
			continue;		// group delimiter
		if (pos + 2 > body.size())
			return false;
		uint16 nameLength = GetU16(body, pos);
		pos += 2;
		if (pos + nameLength > body.size())
			return false;
		std::string thisName((const char*)&body[pos], nameLength);
		pos += nameLength;
		if (pos + 2 > body.size())
			return false;
		uint16 valueLength = GetU16(body, pos);
		pos += 2;
		if (pos + valueLength > body.size())
			return false;
		size_t valuePos = pos;
		pos += valueLength;

		if (tag == 0x34) {			// begCollection
			depth++;
			continue;
		}
		if (tag == 0x37) {			// endCollection
			depth--;
			continue;
		}
		if (depth > 0)
			continue;
		if (nameLength > 0)
			name = thisName;

		IPPValue value;
		value.tag = tag;
		switch (tag) {
			case 0x21:
			case 0x23:
				if (valueLength == 4)
					value.integer = GetI32(body, valuePos);
				break;
			case 0x22:
				if (valueLength >= 1)
					value.integer = body[valuePos] != 0;
				break;
			case 0x32:
				if (valueLength == 9) {
					value.integer = GetI32(body, valuePos);
					value.integer2 = GetI32(body, valuePos + 4);
					value.text = body[valuePos + 8] == 3 ? "dpi" : "dpcm";
				}
				break;
			case 0x33:
				if (valueLength == 8) {
					value.integer = GetI32(body, valuePos);
					value.integer2 = GetI32(body, valuePos + 4);
				}
				break;
			case 0x35:		// textWithLanguage / nameWithLanguage
			case 0x36:
				if (valueLength >= 4) {
					uint16 langLength = GetU16(body, valuePos);
					size_t p = valuePos + 2 + langLength;
					if (p + 2 <= valuePos + valueLength) {
						uint16 textLength = GetU16(body, p);
						p += 2;
						if (p + textLength <= valuePos + valueLength)
							value.text.assign((const char*)&body[p],
								textLength);
					}
				}
				break;
			default:
				value.text.assign((const char*)&body[valuePos], valueLength);
				break;
		}
		attributes.Add(name, value);
	}
	return true;
}

}	// namespace


// Over IPP-USB the printer is addressed as if it were on localhost; the
// resource path is the one IPP-USB printers use.
const char* IPPClient::kUSBRequestURI = "ipp://localhost:60000/ipp/print";
const char* IPPClient::kUSBRequestPath = "/ipp/print";
const char* IPPClient::kUSBRequestHost = "localhost:60000";


status_t
IPPClient::GetPrinterAttributes(const char* url, IPPAttributes& attributes,
	BString& errorText, bigtime_t timeout)
{
	if (IPPUSB::IsUSBURL(url)) {
		std::vector<uint8> body = BuildRequest(kUSBRequestURI);
		char header[512];
		snprintf(header, sizeof(header),
			"POST %s HTTP/1.1\r\nHost: %s\r\n"
			"Content-Type: application/ipp\r\nContent-Length: %zu\r\n\r\n",
			kUSBRequestPath, kUSBRequestHost, body.size());
		std::vector<uint8> request(header, header + strlen(header));
		request.insert(request.end(), body.begin(), body.end());

		std::vector<uint8> response;
		status_t status = IPPUSB::Exchange(url, request, response, errorText);
		if (status != B_OK)
			return status;

		int httpStatus = 0;
		std::vector<uint8> ippBody;
		if (!ParseHTTP(response, httpStatus, ippBody)) {
			errorText = "No HTTP response";
			return B_ERROR;
		}
		if (httpStatus != 200) {
			errorText << "HTTP error " << httpStatus;
			return B_ERROR;
		}
		uint16 ippStatus = 0;
		if (!ParseIPP(ippBody, ippStatus, attributes)) {
			errorText = "Malformed IPP response";
			return B_ERROR;
		}
		if (ippStatus >= 0x0400) {
			errorText << "IPP error 0x"
				<< BString().SetToFormat("%04x", ippStatus);
			return B_ERROR;
		}
		return B_OK;
	}

	std::string host;
	std::string path;
	uint16 port;
	if (!ParseURL(url, host, port, path)) {
		errorText = "Invalid printer URL";
		return B_BAD_VALUE;
	}

	Connection connection;
	status_t status = connection.Connect(host, port, timeout, errorText);
	if (status != B_OK)
		return status;

	std::vector<uint8> body = BuildRequest(url);
	char header[512];
	snprintf(header, sizeof(header),
		"POST %s HTTP/1.1\r\nHost: %s:%u\r\n"
		"Content-Type: application/ipp\r\nContent-Length: %zu\r\n"
		"Connection: close\r\n\r\n", path.c_str(), host.c_str(), port,
		body.size());
	std::vector<uint8> request(header, header + strlen(header));
	request.insert(request.end(), body.begin(), body.end());

	if (!connection.Send(request)) {
		errorText = "Cannot send request";
		return B_ERROR;
	}

	std::vector<uint8> response = connection.ReceiveAll();
	int httpStatus = 0;
	std::vector<uint8> ippBody;
	if (!ParseHTTP(response, httpStatus, ippBody)) {
		errorText = "No HTTP response";
		return B_ERROR;
	}
	if (httpStatus != 200) {
		errorText << "HTTP error " << httpStatus;
		return B_ERROR;
	}

	uint16 ippStatus = 0;
	if (!ParseIPP(ippBody, ippStatus, attributes)) {
		errorText = "Malformed IPP response";
		return B_ERROR;
	}
	if (ippStatus >= 0x0400) {
		errorText << "IPP error 0x" << BString().SetToFormat("%04x", ippStatus);
		return B_ERROR;
	}
	return B_OK;
}
