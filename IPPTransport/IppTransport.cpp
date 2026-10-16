// Sun, 18 Jun 2000
// Y.Takagi

#include <Alert.h>
#include <DataIO.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <fs_attr.h>

// written by the IPP Everywhere driver, see PWGDriver::StartDocument()
static const char *kJobAttributesAttribute = "ipp-everywhere:job-attributes";

#include <Path.h>
#include <Message.h>
#include <OS.h>

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "IppContent.h"
#include "IppSetupDlg.h"
#include "IppTransport.h"
#include "IppDefs.h"
#include "DbgMsg.h"
#include "IPPClient.h"
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
			request->setURI("printer-uri", usb ? IPPClient::kUSBRequestURI : __url);
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

			// The HTTP exchange is the driver's (IPPClient), the same code
			// for network and USB: HTTP/1.1 request, reply read until it
			// is complete, chunked encoding decoded.
			ostringstream bodyStream;
			bodyStream << *request;
			delete request;
			string bodyText = bodyStream.str();
			std::vector<uint8> ippBody(bodyText.begin(), bodyText.end());
			std::vector<uint8> httpRequest;
			std::vector<uint8> httpResponse;
			BString exchangeError;

			if (!IPPClient::BuildHTTPRequest(__url, ippBody, httpRequest)) {
				__error = true;
				error_msg = "invalid printer URL.";
			} else if (IPPClient::Exchange(__url, httpRequest, httpResponse,
					exchangeError) != B_OK) {
				__error = true;
				error_msg = exchangeError.String();
			} else {
				int httpStatus = 0;
				std::vector<uint8> replyBody;
				if (!IPPClient::ParseHTTPResponse(httpResponse, httpStatus,
						replyBody)) {
					__error = true;
					error_msg = "cannot get a IPP response.";
				} else if (httpStatus != 200) {
					__error = true;
					// the status line, e.g. "HTTP/1.1 505 HTTP Version Not Supported"
					string text(httpResponse.begin(), httpResponse.end());
					error_msg = text.substr(0, text.find("\r\n"));
				} else if (replyBody.size() < 8) {
					__error = true;
					error_msg = "cannot get a IPP response.";
				} else {
					istringstream is(string(replyBody.begin(), replyBody.end()));
					IppContent response;
					is >> response;
					if (response.fail()) {
						__error = true;
						error_msg = response.getStatusMessage();
						busy = response.getStatusCode() == IPP_SERVER_ERROR_BUSY;
						// the printer did not like our job attributes: try
						// once more with the bare job
						if (sendJobAttributes && !busy
							&& response.getStatusCode() >= 0x0400
							&& response.getStatusCode() < 0x0500) {
							sendJobAttributes = false;
							retryWithoutAttributes = true;
						}
					}
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
