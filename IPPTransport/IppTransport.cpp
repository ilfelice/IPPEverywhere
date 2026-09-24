// Sun, 18 Jun 2000
// Y.Takagi

#include <Alert.h>
#include <DataIO.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <fs_attr.h>

// written by the IPP Everywhere driver, see PWGDriver::StartDocument()
static const char *kJobAttributesAttribute = "ipp-everywhere:job-attributes";

// how a printer is addressed over IPP-USB
static const char *kUSBRequestURI = "ipp://localhost:60000/ipp/print";
static const char *kUSBRequestPath = "/ipp/print";
static const char *kUSBRequestHost = "localhost:60000";
#include <Path.h>
#include <Message.h>
#include <OS.h>
#include <Url.h>

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "IppContent.h"
#include "IppURLConnection.h"
#include "IppSetupDlg.h"
#include "IppTransport.h"
#include "IppDefs.h"
#include "DbgMsg.h"
#include "IPPUSB.h"

#include <sstream>
#include <vector>

#if (!__MWERKS__)
using namespace std;
#else 
#define std
#endif

IppTransport::IppTransport(BMessage *msg)
	: BDataIO()
{
	__url[0]  = '\0';
	__user[0] = '\0';
	__file[0] = '\0';
	__jobid   = 0;
	__error   = false;

	DUMP_BMESSAGE(msg);

	const char *spool_path = msg->FindString(SPOOL_PATH);
	__spool_path = spool_path ? spool_path : "";
	if (spool_path && *spool_path) {
		BDirectory dir(spool_path);
		DUMP_BDIRECTORY(&dir);

		dir.ReadAttr(IPP_URL, B_STRING_TYPE, 0, __url, sizeof(__url));
		if (__url[0] == '\0') {
			IppSetupDlg *dlg = new IppSetupDlg(&dir);
			if (dlg->Go() == B_ERROR) {
				__error = true;
				return;
			}
		}

		dir.ReadAttr(IPP_URL,    B_STRING_TYPE, 0, __url,    sizeof(__url));
		dir.ReadAttr(IPP_JOB_ID, B_INT32_TYPE,  0, &__jobid, sizeof(__jobid));
		__jobid++;
		if (__jobid > 255) {
			__jobid = 1;
		}
		dir.WriteAttr(IPP_JOB_ID, B_INT32_TYPE, 0, &__jobid, sizeof(__jobid));

		struct passwd *pwd = getpwuid(geteuid());
		if (pwd != NULL && pwd->pw_name != NULL && pwd->pw_name[0])
			strcpy(__user, pwd->pw_name);
		else
			strcpy(__user, "baron");

		// Keep the temporary copy of the job out of the spool folder, the
		// Printers preferences would otherwise list it as an unknown job.
		BPath tempPath;
		if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &tempPath) != B_OK)
			tempPath.SetTo(spool_path);
		sprintf(__file, "%s/%s@ipp.%" B_PRId32, tempPath.Path(), __user,
			__jobid);

		__fs.open(__file, ios::in | ios::out | ios::binary | ios::trunc);
		if (__fs.good()) {
			DBGMSG(("spool_file: %s\n", __file));
			return;
		}
	}
	__error = true;
}

IppTransport::~IppTransport()
{
	string error_msg;

	if (!__error && __fs.good()) {
		long fssize = __fs.tellg();

		// The printer may still be busy with a previous job and answer
		// server-error-busy. Retry for a while before giving up.
		const int kRetryDelay = 3;			// seconds
		const int kMaxRetries = 200;		// ~10 minutes

		// Job attributes (copies, sides, media...) the printer driver may
		// have left for us, already IPP encoded, as an attribute of the
		// printer folder. They are sent as the job attributes group; if the
		// printer rejects them the job is retried without.
		string jobAttributes;
		{
			BDirectory dir(__spool_path.c_str());
			attr_info info;
			if (dir.InitCheck() == B_OK
				&& dir.GetAttrInfo(kJobAttributesAttribute, &info) == B_OK
				&& info.size > 0) {
				jobAttributes.resize(info.size);
				ssize_t read = dir.ReadAttr(kJobAttributesAttribute, B_RAW_TYPE,
					0, &jobAttributes[0], info.size);
				if (read != info.size)
					jobAttributes.clear();
				dir.RemoveAttr(kJobAttributesAttribute);
			}
		}
		bool sendJobAttributes = !jobAttributes.empty();
		bool usb = IPPUSB::IsUSBURL(__url);

		for (int attempt = 0; attempt <= kMaxRetries; attempt++) {
			bool busy = false;
			bool retryWithoutAttributes = false;
			__error = false;
			error_msg = "";

			DBGMSG(("create IppContent\n"));
			IppContent *request = new IppContent;
			request->setOperationId(IPP_PRINT_JOB);
			request->setDelimiter(IPP_OPERATION_ATTRIBUTES_TAG);
			request->setCharset("attributes-charset", "utf-8");
			request->setNaturalLanguage("attributes-natural-language", "en-us");
			request->setURI("printer-uri", usb ? kUSBRequestURI : __url);
			request->setMimeMediaType("document-format", "application/octet-stream");
			request->setNameWithoutLanguage("requesting-user-name", __user);
			if (sendJobAttributes) {
				request->setDelimiter(IPP_JOB_ATTRIBUTES_TAG);
				request->setRaw(jobAttributes.data(), jobAttributes.size());
			}
			request->setDelimiter(IPP_END_OF_ATTRIBUTES_TAG);

			__fs.clear();
			__fs.seekg(0, ios::beg);
			request->setRawData(__fs, fssize);

			// A response handler shared by the two ways of talking to the
			// printer: sets __error, error_msg, busy, retryWithoutAttributes.
			struct Outcome {
				bool* error; string* message; bool* busy; bool* retry;
				bool* sendAttrs;
				void Set(const IppContent* response)
				{
					if (!response->fail())
						return;
					*error = true;
					*message = response->getStatusMessage();
					*busy = response->getStatusCode() == IPP_SERVER_ERROR_BUSY;
					// the printer did not like our job attributes: try once
					// more with the bare job
					if (*sendAttrs && !*busy
						&& response->getStatusCode() >= 0x0400
						&& response->getStatusCode() < 0x0500) {
						*sendAttrs = false;
						*retry = true;
					}
				}
			} outcome = { &__error, &error_msg, &busy, &retryWithoutAttributes,
				&sendJobAttributes };

			if (usb) {
				// IPP-USB: the same HTTP exchange over the USB interface
				ostringstream body;
				body << *request;
				delete request;
				string bodyText = body.str();
				char header[512];
				snprintf(header, sizeof(header),
					"POST %s HTTP/1.1\r\nHost: %s\r\n"
					"Content-Type: application/ipp\r\nContent-Length: %zu\r\n\r\n",
					kUSBRequestPath, kUSBRequestHost, bodyText.size());
				std::vector<uint8> data(header, header + strlen(header));
				data.insert(data.end(), bodyText.begin(), bodyText.end());

				std::vector<uint8> reply;
				BString usbError;
				if (IPPUSB::Exchange(__url, data, reply, usbError) != B_OK) {
					__error = true;
					error_msg = usbError.String();
				} else {
					string text(reply.begin(), reply.end());
					size_t headerEnd = text.find("\r\n\r\n");
					int httpStatus = 0;
					size_t space = text.find(' ');
					if (space != string::npos)
						httpStatus = atoi(text.c_str() + space + 1);
					if (headerEnd == string::npos || httpStatus != 200) {
						__error = true;
						error_msg = "cannot get a IPP response.";
					} else {
						// chunked responses are unusual here, but handle them
						std::vector<uint8> ippBody;
						string lower(text.substr(0, headerEnd));
						for (size_t i = 0; i < lower.size(); i++)
							lower[i] = tolower(lower[i]);
						size_t pos = headerEnd + 4;
						if (lower.find("transfer-encoding: chunked") != string::npos) {
							while (pos < reply.size()) {
								size_t lineEnd = text.find("\r\n", pos);
								if (lineEnd == string::npos)
									break;
								long size = strtol(text.c_str() + pos, NULL, 16);
								pos = lineEnd + 2;
								if (size <= 0)
									break;
								ippBody.insert(ippBody.end(), reply.begin() + pos,
									reply.begin() + pos + size);
								pos += size + 2;
							}
						} else
							ippBody.assign(reply.begin() + pos, reply.end());
						istringstream is(string(ippBody.begin(), ippBody.end()));
						IppContent response;
						is >> response;
						outcome.Set(&response);
					}
				}
			} else {
				BUrl url(__url, true);
				IppURLConnection conn(url);
				conn.setIppRequest(request);
				conn.setRequestProperty("Connection", "close");

				DBGMSG(("do connect\n"));

				HTTP_RESPONSECODE response_code = conn.getResponseCode();
				if (response_code == HTTP_OK) {
					const char *content_type = conn.getContentType();
					if (content_type == NULL || strncasecmp(content_type, "application/ipp", 15) == 0) {
						outcome.Set(conn.getIppResponse());
					} else {
						__error = true;
						error_msg = "cannot get a IPP response.";
					}
				} else if (response_code != HTTP_UNKNOWN) {
					__error = true;
					error_msg = conn.getResponseMessage();
				} else {
					__error = true;
					error_msg = "cannot connect to the IPP server.";
				}
			}

			if (retryWithoutAttributes)
				continue;
			if (!busy)
				break;

			snooze(kRetryDelay * 1000000LL);
		}
	}

	unlink(__file);

	if (__error) {
		BAlert *alert = new BAlert("", error_msg.c_str(), "OK");
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}
}

ssize_t IppTransport::Read(void *, size_t)
{
	return 0;
}

ssize_t IppTransport::Write(const void *buffer, size_t size)
{
//	DBGMSG(("write: %d\n", size));

	if (!__fs.write((const char *)buffer, size)) {
		__error = true;
		return 0;
	}
//	return __fs.pcount();
	return size;
}
