#!/bin/sh
set -eu

NAME="sprd-vcom"
VERSION="1.0.0"

modprobe -r sprd_vcom >/dev/null 2>&1 || true
dkms remove -m "$NAME" -v "$VERSION" --all || true
rm -rf "/usr/src/${NAME}-${VERSION}"
rm -f /etc/udev/rules.d/99-sprd-vcom.rules
rm -f /etc/modules-load.d/sprd_vcom.conf
udevadm control --reload-rules || true
depmod -a

echo "sprd_vcom removed."
