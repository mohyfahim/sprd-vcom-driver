#!/bin/sh
set -eu

NAME="sprd-vcom"
VERSION="1.0.0"
SRC="/usr/src/${NAME}-${VERSION}"

if ! command -v dkms >/dev/null 2>&1; then
	echo "dkms is not installed." >&2
	echo "Debian/Ubuntu: sudo apt install dkms build-essential linux-headers-\$(uname -r)" >&2
	exit 1
fi

mkdir -p "$SRC"
cp sprd_vcom.c Makefile dkms.conf "$SRC/"

# Allow re-running the installer.
dkms remove -m "$NAME" -v "$VERSION" --all >/dev/null 2>&1 || true
dkms add -m "$NAME" -v "$VERSION"
dkms build -m "$NAME" -v "$VERSION"
dkms install -m "$NAME" -v "$VERSION"

install -D -m 0644 99-sprd-vcom.rules /etc/udev/rules.d/99-sprd-vcom.rules
install -D -m 0644 sprd_vcom.conf /etc/modules-load.d/sprd_vcom.conf

udevadm control --reload-rules || true
depmod -a
modprobe sprd_vcom

echo
echo "Installed sprd_vcom via DKMS."
echo "Reconnect the modem, then check: ls -l /dev/sprd-at"
