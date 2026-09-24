// IPP transport add-on entry points.
// Based on Haiku's IPP transport (Y.Takagi, 2000). The CUPS browsing
// roster of the original was removed; printers are entered by URL.

#include "IppTransport.h"
#include "DbgMsg.h"

#include "PrintTransportAddOn.h"
#include <Message.h>
#include <OS.h>


IppTransport *transport = NULL;

// Set transport_features so we stay loaded
uint32 transport_features = B_TRANSPORT_IS_HOTPLUG | B_TRANSPORT_IS_NETWORK;


extern "C" _EXPORT void
exit_transport()
{
	DBGMSG(("> exit_transport\n"));
	if (transport) {
		delete transport;
		transport = NULL;
	}
	DBGMSG(("< exit_transport\n"));
}


// Note: no list_transport_ports() here on purpose. Without it the Printers
// preferences show the transport as a plain menu entry; with it (and no
// discovered printers) it would show a "No printer found!" submenu.
// The URL is asked for on first use.


extern "C" _EXPORT BDataIO *
init_transport(BMessage *msg)
{
	DBGMSG(("> init_transport\n"));

	transport = new IppTransport(msg);

	if (transport->fail()) {
		exit_transport();
	}

	if (msg)
		msg->what = 'okok';

	DBGMSG(("< init_transport\n"));
	return transport;
}
