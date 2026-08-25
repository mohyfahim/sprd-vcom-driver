#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$#" -ne 0 ]; then
	echo "Usage: $0" >&2
	exit 2
fi
if [ "$(id -u)" -ne 0 ]; then
	echo "This uninstaller must run as root." >&2
	exit 1
fi

for command in depmod udevadm; do
	if ! command -v "$command" >/dev/null 2>&1; then
		echo "Required command not found: $command" >&2
		exit 1
	fi
done

name=$(sed -n 's/^PACKAGE_NAME="\([^"]*\)"/\1/p' "$script_dir/dkms.conf")
version=$(sed -n 's/^PACKAGE_VERSION="\([^"]*\)"/\1/p' "$script_dir/dkms.conf")
if [ -z "$name" ] || [ -z "$version" ]; then
	echo "Unable to read package identity from dkms.conf." >&2
	exit 1
fi

source_dir="/usr/src/$name-$version"
case "$source_dir" in
/usr/src/sprd-vcom-[0-9]*) ;;
*)
	echo "Refusing unsafe DKMS source path: $source_dir" >&2
	exit 1
	;;
esac

modprobe -r sprd_vcom >/dev/null 2>&1 || true
if command -v dkms >/dev/null 2>&1; then
	dkms remove -m "$name" -v "$version" --all || true
fi
rm -rf -- "$source_dir"
rm -f /usr/local/bin/sprd-at-tty
rm -f /etc/udev/rules.d/99-sprd-vcom.rules
rm -f /etc/udev/rules.d/78-mm-sprd-vcom.rules
rm -f /etc/udev/rules.d/99-unisoc-at-ignore.rules
rm -f /etc/modules-load.d/sprd_vcom.conf

udevadm control --reload-rules || true
depmod -a

echo "Removed $name $version."
