/*
 * AddIPPPrinterWindow.h
 * Dialog shown when the printer is added: lists IPP printers found on the
 * network and lets the user pick one or type a URL.
 * Copyright 2026. Distributed under the terms of the MIT License.
 */
#ifndef ADD_IPP_PRINTER_WINDOW_H
#define ADD_IPP_PRINTER_WINDOW_H


#include "DialogWindow.h"

#include <String.h>


class BButton;
class BListView;
class BMenuField;
class BMenuItem;
class BStringView;
class BTextControl;


class AddIPPPrinterWindow : public DialogWindow {
public:
	// resultFormat receives "auto", "pwg-raster" or "urf"
								AddIPPPrinterWindow(const char* currentURL,
									BString* resultURL, BString* resultFormat);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_StartSearch();
			void				_ShowResults(BMessage* message);
			void				_UpdateAddButton();
			void				_QueryFormats();
			void				_ShowFormats(BMessage* message);
	static	int32				_SearchThread(void* data);
	static	int32				_QueryThread(void* data);

			BString*			fResultURL;
			BString*			fResultFormat;
			BStringView*		fStatus;
			BListView*			fList;
			BButton*			fSearchButton;
			BTextControl*		fURL;
			BMenuField*			fFormat;
			BMenuItem*			fFormatAuto;
			BMenuItem*			fFormatPWG;
			BMenuItem*			fFormatURF;
			BButton*			fOKButton;
			thread_id			fSearchThread;
			thread_id			fQueryThread;
			BString				fQueriedURL;
			bool				fURLFromList;
};


#endif // ADD_IPP_PRINTER_WINDOW_H
