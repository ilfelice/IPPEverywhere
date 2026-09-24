/*
 * AddIPPPrinterWindow.cpp
 * Copyright 2026. Distributed under the terms of the MIT License.
 */


#include "AddIPPPrinterWindow.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <UTF8.h>

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
	BString* resultURL)
	:
	DialogWindow(BRect(100, 100, 500, 400), B_TRANSLATE("Add IPP printer"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_MINIMIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fResultURL(resultURL),
	fSearchThread(-1)
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

	fOKButton = new BButton("ok", B_TRANSLATE("OK"), new BMessage(kMsgOK));
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
			if (item != NULL)
				fURL->SetText(item->URL());
			break;
		}

		case kMsgURLChanged:
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


bool
AddIPPPrinterWindow::QuitRequested()
{
	// The add-on is unloaded after the window is gone; make sure the
	// search thread is not still running our code by then.
	if (fSearchThread >= 0) {
		wait_for_thread(fSearchThread, NULL);
		fSearchThread = -1;
	}
	return true;
}
