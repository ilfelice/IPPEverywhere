/*
 * AddIPPPrinterWindow.cpp
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "AddIPPPrinterWindow.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <UTF8.h>

#include "IPPClient.h"
#include "IPPDiscovery.h"
#include "IPPUSB.h"

#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AddIPPPrinterWindow"


static const uint32 kMsgOK = 'okok';
static const uint32 kMsgCancel = 'cncl';
static const uint32 kMsgSearch = 'srch';
static const uint32 kMsgResults = 'rslt';
static const uint32 kMsgSelected = 'selc';
static const uint32 kMsgURLChanged = 'urlc';
static const uint32 kMsgURLEntered = 'urle';
static const uint32 kMsgFormats = 'frmt';
static const uint32 kMsgFormatChosen = 'frmc';


class PrinterItem : public BStringItem {
public:
	PrinterItem(const char* label, const char* url)
		:
		BStringItem(label),
		fURL(url)
	{
	}

	const char* URL() const { return fURL.String(); }

private:
	BString fURL;
};


AddIPPPrinterWindow::AddIPPPrinterWindow(const char* currentURL,
	BString* resultURL, BString* resultFormat)
	:
	DialogWindow(BRect(100, 100, 500, 400), B_TRANSLATE("Add IPP printer"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_MINIMIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fResultURL(resultURL),
	fResultFormat(resultFormat),
	fSearchThread(-1),
	fQueryThread(-1),
	fURLFromList(false)
{
	SetResult(B_ERROR);

	fStatus = new BStringView("status",
		B_TRANSLATE("Searching for printers" B_UTF8_ELLIPSIS));
	fList = new BListView("printers");
	fList->SetSelectionMessage(new BMessage(kMsgSelected));
	fList->SetExplicitMinSize(BSize(320, 120));
	BScrollView* scrollView = new BScrollView("scroll", fList, 0, false,
		true);

	fSearchButton = new BButton("search", B_TRANSLATE("Search again"),
		new BMessage(kMsgSearch));
	fSearchButton->SetEnabled(false);

	fURL = new BTextControl("url", B_TRANSLATE("Printer URL:"),
		currentURL != NULL && currentURL[0] != '\0' ? currentURL : "ipp://",
		NULL);
	fURL->SetModificationMessage(new BMessage(kMsgURLChanged));
	fURL->SetMessage(new BMessage(kMsgURLEntered));	// Enter in the field

	// Raster format: filled in once the selected printer has answered
	BPopUpMenu* formatMenu = new BPopUpMenu("format");
	fFormatAuto = new BMenuItem(B_TRANSLATE("Automatic"),
		new BMessage(kMsgFormatChosen));
	fFormatPWG = new BMenuItem(B_TRANSLATE("PWG Raster"),
		new BMessage(kMsgFormatChosen));
	fFormatURF = new BMenuItem(B_TRANSLATE("URF (Apple raster)"),
		new BMessage(kMsgFormatChosen));
	formatMenu->AddItem(fFormatAuto);
	formatMenu->AddItem(fFormatPWG);
	formatMenu->AddItem(fFormatURF);
	fFormatAuto->SetMarked(true);
	fFormatPWG->SetEnabled(false);
	fFormatURF->SetEnabled(false);
	fFormat = new BMenuField("formatField", B_TRANSLATE("Raster format:"),
		formatMenu);

	fOKButton = new BButton("ok", B_TRANSLATE("Add printer"),
		new BMessage(kMsgOK));
	fOKButton->SetEnabled(false);
	BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancel));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fStatus)
		.Add(scrollView)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fSearchButton)
		.End()
		.Add(fURL)
		.Add(fFormat)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancelButton)
			.Add(fOKButton)
		.End();

	fOKButton->MakeDefault(true);
	_StartSearch();
}


void
AddIPPPrinterWindow::_StartSearch()
{
	if (fSearchThread >= 0)
		return;

	fList->MakeEmpty();
	// a URL that came from a list selection goes with the list; one the
	// user typed stays
	if (fURLFromList) {
		fURL->SetText("ipp://");
		fURLFromList = false;
	}
	_UpdateAddButton();
	fStatus->SetText(B_TRANSLATE("Searching for printers" B_UTF8_ELLIPSIS));
	fSearchButton->SetEnabled(false);

	BMessenger* messenger = new BMessenger(this);
	fSearchThread = spawn_thread(_SearchThread, "IPP discovery",
		B_NORMAL_PRIORITY, messenger);
	if (fSearchThread >= 0)
		resume_thread(fSearchThread);
	else {
		delete messenger;
		fStatus->SetText(B_TRANSLATE("Could not search for printers."));
		fSearchButton->SetEnabled(true);
	}
}


int32
AddIPPPrinterWindow::_SearchThread(void* data)
{
	BMessenger* messenger = (BMessenger*)data;

	BMessage results(kMsgResults);

	// USB first: plugged in printers with an IPP-USB interface
	std::vector<IPPUSBPrinter> usbPrinters;
	IPPUSB::Enumerate(usbPrinters);
	for (size_t i = 0; i < usbPrinters.size(); i++) {
		BString label = usbPrinters[i].Name();
		label << " (USB)";
		results.AddString("label", label);
		results.AddString("url", usbPrinters[i].URL());
	}

	std::vector<DiscoveredPrinter> printers;
	status_t status = IPPDiscovery::Browse(printers);
	results.AddInt32("status", status);
	for (size_t i = 0; i < printers.size(); i++) {
		BString label = printers[i].name;
		if (printers[i].model.Length() > 0 && printers[i].model != label)
			label << " (" << printers[i].model << ")";
		results.AddString("label", label);
		results.AddString("url", printers[i].URL());
	}

	messenger->SendMessage(&results);
	delete messenger;
	return 0;
}


void
AddIPPPrinterWindow::_ShowResults(BMessage* message)
{
	if (fSearchThread >= 0) {
		wait_for_thread(fSearchThread, NULL);
		fSearchThread = -1;
	}

	int32 status = message->GetInt32("status", B_ERROR);
	int32 count = 0;
	BString label;
	BString url;
	for (int32 i = 0; message->FindString("label", i, &label) == B_OK
			&& message->FindString("url", i, &url) == B_OK; i++) {
		fList->AddItem(new PrinterItem(label.String(), url.String()));
		count++;
	}

	BString text;
	if (status != B_OK && count == 0)
		text = B_TRANSLATE("Network search failed. Enter the printer URL below.");
	else if (count == 0)
		text = B_TRANSLATE("No IPP printers found. Enter the printer URL below.");
	else if (count == 1)
		text = B_TRANSLATE("Found 1 printer:");
	else {
		text = B_TRANSLATE("Found %count% printers:");
		text.ReplaceFirst("%count%", (BString() << count).String());
	}
	fStatus->SetText(text.String());

	if (count == 1) {
		fList->Select(0);
		// selection message fills in the URL
	}

	fSearchButton->SetEnabled(true);
}


// "Add printer" is enabled once a printer is selected in the list or a
// URL has been typed in (for printers the search did not find).
void
AddIPPPrinterWindow::_UpdateAddButton()
{
	BString url(fURL->Text());
	url.Trim();
	bool haveURL = url.Length() > 0 && url != "ipp://"
		&& url != "ipps://";
	fOKButton->SetEnabled(fList->CurrentSelection() >= 0 || haveURL);
}


void
AddIPPPrinterWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgResults:
			_ShowResults(message);
			break;

		case kMsgSearch:
			_StartSearch();
			break;

		case kMsgSelected:
		{
			int32 index = fList->CurrentSelection();
			PrinterItem* item = index < 0 ? NULL
				: dynamic_cast<PrinterItem*>(fList->ItemAt(index));
			if (item != NULL) {
				fURL->SetText(item->URL());
				fURLFromList = true;
				_QueryFormats();
			}
			_UpdateAddButton();
			break;
		}

		case kMsgURLEntered:
			_QueryFormats();
			break;

		case kMsgFormats:
			_ShowFormats(message);
			break;

		case kMsgFormatChosen:
			break;

		case kMsgURLChanged:
			// typing in the field makes it the user's own
			fURLFromList = false;
			_UpdateAddButton();
			break;

		case kMsgOK:
		{
			BString url(fURL->Text());
			url.Trim();
			if (url.Length() == 0 || url == "ipp://") {
				fStatus->SetText(B_TRANSLATE("Please select a printer or enter its URL."));
				break;
			}
			if (url.FindFirst("://") < 0)
				url.Prepend("ipp://");
			*fResultURL = url;
			if (fFormatPWG->IsMarked())
				*fResultFormat = "pwg-raster";
			else if (fFormatURF->IsMarked())
				*fResultFormat = "urf";
			else
				*fResultFormat = "auto";
			SetResult(B_OK);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case kMsgCancel:
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			DialogWindow::MessageReceived(message);
			break;
	}
}


// Asks the printer at the current URL which raster formats it accepts
// and enables the matching menu items when the answer arrives.
void
AddIPPPrinterWindow::_QueryFormats()
{
	BString url(fURL->Text());
	url.Trim();
	if (url.Length() == 0 || url == "ipp://" || url == fQueriedURL)
		return;
	if (fQueryThread >= 0) {
		wait_for_thread(fQueryThread, NULL);
		fQueryThread = -1;
	}
	fQueriedURL = url;
	fFormatAuto->SetMarked(true);
	fFormatPWG->SetEnabled(false);
	fFormatURF->SetEnabled(false);

	BMessage* request = new BMessage(kMsgFormats);
	request->AddString("url", url);
	request->AddMessenger("target", BMessenger(this));
	fQueryThread = spawn_thread(_QueryThread, "IPP query",
		B_NORMAL_PRIORITY, request);
	if (fQueryThread >= 0)
		resume_thread(fQueryThread);
	else
		delete request;
}


int32
AddIPPPrinterWindow::_QueryThread(void* data)
{
	BMessage* message = (BMessage*)data;
	BMessenger target;
	message->FindMessenger("target", &target);
	const char* url = message->GetString("url", "");

	IPPAttributes attributes;
	BString error;
	bool answered = IPPClient::GetPrinterAttributes(url, attributes, error)
		== B_OK;
	message->AddBool("answered", answered);
	message->AddBool("pwg", answered
		&& attributes.Contains("document-format-supported", "image/pwg-raster"));
	message->AddBool("urf", answered
		&& attributes.Contains("document-format-supported", "image/urf"));
	target.SendMessage(message);
	delete message;
	return 0;
}


void
AddIPPPrinterWindow::_ShowFormats(BMessage* message)
{
	if (fQueryThread >= 0) {
		wait_for_thread(fQueryThread, NULL);
		fQueryThread = -1;
	}
	// ignore answers for a URL that is no longer the current one
	if (fQueriedURL != message->GetString("url", ""))
		return;
	bool pwg = message->GetBool("pwg", false);
	bool urf = message->GetBool("urf", false);
	fFormatPWG->SetEnabled(pwg);
	fFormatURF->SetEnabled(urf);
	if (message->GetBool("answered", false) && !pwg && !urf) {
		fStatus->SetText(B_TRANSLATE("This printer accepts neither PWG "
			"Raster nor URF; the driver cannot print to it."));
	}
}


bool
AddIPPPrinterWindow::QuitRequested()
{
	// The add-on is unloaded after the window is gone; make sure no
	// thread is still running our code by then.
	if (fSearchThread >= 0) {
		wait_for_thread(fSearchThread, NULL);
		fSearchThread = -1;
	}
	if (fQueryThread >= 0) {
		wait_for_thread(fQueryThread, NULL);
		fQueryThread = -1;
	}
	return true;
}
