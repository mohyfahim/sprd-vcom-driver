#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mode=manual
load_module=yes

usage()
{
	echo "Usage: $0 [--mode=manual|modemmanager] [--no-load]" >&2
}

for argument in "$@"; do
	case "$argument" in
	--mode=manual)
		mode=manual
		;;
	--mode=modemmanager)
		mode=modemmanager
		;;
	--no-load)
		load_module=no
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		usage
		exit 2
		;;
	esac
done

if [ "$(id -u)" -ne 0 ]; then
	echo "This installer must run as root." >&2
	exit 1
fi

for command in dkms make install depmod udevadm modprobe; do
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

make -C "$script_dir" tools

if dkms status -m "$name" -v "$version" 2>/dev/null | grep -q .; then
	dkms remove -m "$name" -v "$version" --all
fi

rm -rf -- "$source_dir"
install -d -m 0755 "$source_dir"
install -m 0644 "$script_dir/sprd_vcom.c" "$source_dir/sprd_vcom.c"
install -m 0644 "$script_dir/Makefile" "$source_dir/Makefile"
install -m 0644 "$script_dir/dkms.conf" "$source_dir/dkms.conf"

dkms add -m "$name" -v "$version"
dkms build -m "$name" -v "$version"
dkms install -m "$name" -v "$version"

install -D -m 0755 "$script_dir/build/sprd-at-tty" \
	/usr/local/bin/sprd-at-tty
install -D -m 0644 "$script_dir/packaging/udev/99-sprd-vcom.rules" \
	/etc/udev/rules.d/99-sprd-vcom.rules
install -D -m 0644 \
	"$script_dir/packaging/udev/$mode/78-mm-sprd-vcom.rules" \
	/etc/udev/rules.d/78-mm-sprd-vcom.rules

# Remove files installed by pre-1.0 development snapshots.
rm -f /etc/udev/rules.d/99-unisoc-at-ignore.rules
rm -f /etc/modules-load.d/sprd_vcom.conf

udevadm control --reload-rules || true
depmod -a
if [ "$load_module" = yes ]; then
	modprobe sprd_vcom
fi

echo "Installed $name $version in '$mode' mode."
echo "Reconnect the modem, then run: sprd-at-tty -l"
