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
class BStringView;
class BTextControl;


class AddIPPPrinterWindow : public DialogWindow {
public:
								AddIPPPrinterWindow(const char* currentURL,
									BString* resultURL);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_StartSearch();
			void				_ShowResults(BMessage* message);
	static	int32				_SearchThread(void* data);

			BString*			fResultURL;
			BStringView*		fStatus;
			BListView*			fList;
			BButton*			fSearchButton;
			BTextControl*		fURL;
			BButton*			fOKButton;
			thread_id			fSearchThread;
};


#endif // ADD_IPP_PRINTER_WINDOW_H
