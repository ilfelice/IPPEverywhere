## Haiku Generic Makefile v2.6 ##
## IPP Everywhere (PWG Raster) printer driver

NAME = IPP_Everywhere
TYPE = SHARED
APP_MIME_SIG = application/x-vnd.ipp-everywhere

SRCS = \
	AddIPPPrinterWindow.cpp \
	IPPCapabilities.cpp \
	IPPClient.cpp \
	IPPDiscovery.cpp \
	IPPUSB.cpp \
	PWGEntry.cpp \
	PWGCap.cpp \
	PWGDriver.cpp \
	PWGWriter.cpp \
	libprint/AboutBox.cpp \
	libprint/AddPrinterDlg.cpp \
	libprint/BlockingWindow.cpp \
	libprint/DbgMsg.cpp \
	libprint/DialogWindow.cpp \
	libprint/GraphicsDriver.cpp \
	libprint/Halftone.cpp \
	libprint/HalftoneView.cpp \
	libprint/JSDSlider.cpp \
	libprint/JobData.cpp \
	libprint/JobSetupDlg.cpp \
	libprint/MarginView.cpp \
	libprint/PackBits.cpp \
	libprint/PageSetupDlg.cpp \
	libprint/PagesView.cpp \
	libprint/Preview.cpp \
	libprint/PrintJobReader.cpp \
	libprint/PrintProcess.cpp \
	libprint/PrintUtils.cpp \
	libprint/PrinterCap.cpp \
	libprint/PrinterData.cpp \
	libprint/PrinterDriver.cpp \
	libprint/SpoolMetaData.cpp \
	libprint/StatusWindow.cpp \
	libprint/Transport.cpp \
	libprint/UIDriver.cpp \
	libprint/ValidRect.cpp

RDEFS = IPPEverywhere.rdef

## Languages. `make catkeys` extracts the English strings to
## locales/en.catkeys; copy that file to locales/<lang>.catkeys and fill in
## the last column to translate. `make install-driver` binds the catalogs
## into the add-on when they exist.
LOCALES = en

LIBS = be device localestub network $(STDCPPLIBS)

LOCAL_INCLUDE_PATHS = libprint
SYSTEM_INCLUDE_PATHS = /boot/system/develop/headers/private/shared

OPTIMIZE := FULL
SYMBOLS := TRUE
DEBUGGER := FALSE
WARNINGS = NONE

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine

## Install the driver where print_server looks for it
install-driver: $(TARGET)
	-$(MAKE) bindcatalogs
	mkdir -p "$(HOME)/config/non-packaged/add-ons/Print"
	cp "$(TARGET)" "$(HOME)/config/non-packaged/add-ons/Print/IPP Everywhere"
	@echo "Installed. Restart print_server or reboot, then add the printer."
