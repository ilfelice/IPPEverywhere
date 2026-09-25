#!/bin/sh
# Builds the driver and the transport and wraps both in a Haiku package.
# Usage: ./make-package.sh [version]   (default 0.1)
# The packager name/email is read from $PACKAGER or asked for.
set -e
cd "$(dirname "$0")"

VERSION="${1:-0.1}"
ARCH="$(getarch)"
if [ -z "$PACKAGER" ]; then
	printf 'Packager (Name <email>): '
	read PACKAGER
fi

echo "== building the driver"
make -j4
make bindcatalogs 2>/dev/null || true
echo "== building the transport"
( cd IPPTransport && make -j4 )

DRIVER="$(ls objects.*/IPP_Everywhere | head -1)"
TRANSPORT="$(ls IPPTransport/objects.*/IPP | head -1)"
[ -f "$DRIVER" ] || { echo "driver not built"; exit 1; }
[ -f "$TRANSPORT" ] || { echo "transport not built"; exit 1; }

echo "== assembling"
STAGE="package/ipp_everywhere"
rm -rf package
mkdir -p "$STAGE/add-ons/Print/transport"
mkdir -p "$STAGE/documentation/packages/ipp_everywhere"
cp "$DRIVER" "$STAGE/add-ons/Print/IPP Everywhere"
cp "$TRANSPORT" "$STAGE/add-ons/Print/transport/IPP (fixed)"
cp packaging/README-testing.md "$STAGE/documentation/packages/ipp_everywhere/README.md"
[ -f LICENSE ] && cp LICENSE "$STAGE/documentation/packages/ipp_everywhere/"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@ARCH@/$ARCH/g" \
	-e "s|@PACKAGER@|$PACKAGER|g" packaging/PackageInfo > "$STAGE/.PackageInfo"

OUT="ipp_everywhere-$VERSION-1-$ARCH.hpkg"
rm -f "$OUT"
package create -C "$STAGE" "$OUT"
echo "== $OUT"
