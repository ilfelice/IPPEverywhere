## Haiku Generic Makefile v2.6 ##
## IPP transport add-on, 64-bit fixed build of Haiku's own IPP transport

NAME = IPP
TYPE = SHARED
APP_MIME_SIG = application/x-vnd.ipp-transport-fixed

SRCS = \
	Ipp.cpp \
	IppSetupDlg.cpp \
	IppTransport.cpp \
	IppContent.cpp \
	DbgMsg.cpp \
	../IPPClient.cpp \
	../IPPUSB.cpp

LIBS = be device network $(STDCPPLIBS)

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

## IPPUSB.cpp and IPPClient.cpp live in the parent directory. Listing them as ../x.cpp
## makes the engine add ".." to VPATH, and then make finds the *driver's*
## object files (../objects.*/DbgMsg.o, IPPUSB.o) instead of building ours.
## So VPATH is cleared again and the parent file gets an explicit rule.
VPATH :=
$(OBJ_DIR)/IPPUSB.o : ../IPPUSB.cpp ../IPPUSB.h
	$(CC) -c $< $(INCLUDES) $(CFLAGS) -o "$@"
$(OBJ_DIR)/IPPClient.o : ../IPPClient.cpp ../IPPClient.h ../IPPUSB.h
	$(CC) -c $< $(INCLUDES) $(CFLAGS) -o "$@"

## Installed under a distinct name so it never clashes with the system IPP
install-transport: $(TARGET)
	mkdir -p "$(HOME)/config/non-packaged/add-ons/Print/transport"
	cp "$(TARGET)" "$(HOME)/config/non-packaged/add-ons/Print/transport/IPP (network or USB)"
	@echo "Installed. Run 'hey print_server quit' or reboot."
