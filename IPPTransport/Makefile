## Haiku Generic Makefile v2.6 ##
## IPP transport add-on, 64-bit fixed build of Haiku's own IPP transport

NAME = IPP
TYPE = SHARED
APP_MIME_SIG = application/x-vnd.ipp-transport-fixed

SRCS = \
	Ipp.cpp \
	IppSetupDlg.cpp \
	IppTransport.cpp \
	HttpURLConnection.cpp \
	IppContent.cpp \
	IppURLConnection.cpp \
	Socket.cpp \
	SocketStream.cpp \
	DbgMsg.cpp \
	../IPPUSB.cpp

LIBS = be bnetapi device network $(STDCPPLIBS)

LOCAL_INCLUDE_PATHS = . ..

OPTIMIZE := FULL
SYMBOLS := TRUE
DEBUGGER := FALSE
WARNINGS = NONE
COMPILER_FLAGS = -Wno-format-overflow

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine

## Installed under a distinct name so it never clashes with the system IPP
install-transport: $(TARGET)
	mkdir -p "$(HOME)/config/non-packaged/add-ons/Print/transport"
	cp "$(TARGET)" "$(HOME)/config/non-packaged/add-ons/Print/transport/IPP (fixed)"
	@echo "Installed. Run 'hey print_server quit' or reboot."
