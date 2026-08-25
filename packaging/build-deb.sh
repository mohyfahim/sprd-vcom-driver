#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
umask 022

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

kdir=${KDIR:-}
kernel_arch=${ARCH:-}
cross_compile=${CROSS_COMPILE:-}
output_dir=${DEB_OUTPUT_DIR:-$project_dir/dist}
debian_revision=${DEB_REVISION:-1}

die()
{
	echo "build-deb: $*" >&2
	exit 1
}

require_command()
{
	command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

write_copyright()
{
	destination=$1
	cat >"$destination" <<'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: sprd-vcom
Source: https://github.com/mohyfahim/sprd-vcom-driver

Files: *
Copyright: m.fahim <fahimohy@gmail.com>
License: GPL-2.0-only
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 2 as
 published by the Free Software Foundation.
 .
 On Debian systems, the complete text of the GNU General Public License
 version 2 can be found in /usr/share/common-licenses/GPL-2.
EOF
}

write_changelog()
{
	destination=$1
	date_value=$(date --date="@$SOURCE_DATE_EPOCH" -R)
	cat >"$destination" <<EOF
sprd-vcom ($package_version) unstable; urgency=medium

  * Build exact-kernel sprd_vcom binary module packages.
  * Install the manual udev profile, stable links, AT client, and explicit
    usbserial/sprd_vcom boot loading policy.

 -- m.fahim <fahimohy@gmail.com>  $date_value
EOF
}

write_common_scripts()
{
	common_root=$1
	cat >"$common_root/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

if command -v udevadm >/dev/null 2>&1; then
	udevadm control --reload-rules || true
	udevadm trigger --action=change --subsystem-match=tty || true
	udevadm settle || true
fi

exit 0
EOF
	cat >"$common_root/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e

case "$1" in
remove|purge|abort-install|disappear)
	if command -v udevadm >/dev/null 2>&1; then
		udevadm control --reload-rules || true
		udevadm trigger --action=change --subsystem-match=tty || true
		udevadm settle || true
	fi
	;;
esac

exit 0
EOF
	chmod 0755 "$common_root/DEBIAN/postinst" "$common_root/DEBIAN/postrm"
}

write_module_scripts()
{
	module_root=$1
	cat >"$module_root/DEBIAN/postinst" <<EOF
#!/bin/sh
set -e

kernel_release='$kernel_release'

if command -v depmod >/dev/null 2>&1; then
	depmod -a "\$kernel_release" || true
fi

if [ "\$(uname -r)" = "\$kernel_release" ] && command -v modprobe >/dev/null 2>&1; then
	modprobe usbserial || echo "sprd-vcom: unable to load usbserial; it will be retried at boot" >&2
	modprobe sprd_vcom || echo "sprd-vcom: unable to load sprd_vcom; it will be retried at boot" >&2
fi

if command -v udevadm >/dev/null 2>&1; then
	udevadm control --reload-rules || true
	udevadm trigger --action=change --subsystem-match=tty || true
	udevadm settle || true
fi

exit 0
EOF
	cat >"$module_root/DEBIAN/postrm" <<EOF
#!/bin/sh
set -e

kernel_release='$kernel_release'

case "\$1" in
remove|purge|abort-install|disappear)
	if command -v depmod >/dev/null 2>&1; then
		depmod -a "\$kernel_release" || true
	fi
	if command -v udevadm >/dev/null 2>&1; then
		udevadm control --reload-rules || true
		udevadm trigger --action=change --subsystem-match=tty || true
		udevadm settle || true
	fi
	;;
esac

exit 0
EOF
	chmod 0755 "$module_root/DEBIAN/postinst" "$module_root/DEBIAN/postrm"
}

write_md5sums()
{
	package_root=$1
	(
		cd "$package_root"
		find . -type f ! -path './DEBIAN/*' -print0 |
			sort -z |
			xargs -0 md5sum |
			sed 's#  \./#  #'
	) >"$package_root/DEBIAN/md5sums"
}

normalize_tree()
{
	find "$1" -exec touch -h --date="@$SOURCE_DATE_EPOCH" {} +
}

build_package()
{
	package_root=$1
	package_file=$2

	write_md5sums "$package_root"
	normalize_tree "$package_root"
	dpkg-deb --root-owner-group -Zxz -z9 --build \
		"$package_root" "$package_file"
}

[ -n "$kdir" ] || die "KDIR is required"
[ -d "$kdir" ] || die "kernel tree does not exist: $kdir"
kdir=$(CDPATH= cd -- "$kdir" && pwd)
[ -f "$kdir/.config" ] || die "$kdir/.config is missing; configure the kernel tree first"
[ -s "$kdir/Module.symvers" ] || die "$kdir/Module.symvers is missing or empty; build the target kernel first"

for command in make dpkg dpkg-deb dpkg-architecture modinfo install md5sum \
	readelf sed awk find sort xargs gzip date touch du grep; do
	require_command "$command"
done

case "$kernel_arch" in
"")
	deb_arch=$(dpkg --print-architecture)
	case "$deb_arch" in
	amd64)
		target_cc=${CC:-cc}
		target_strip=${STRIP:-strip}
		target_readelf=${READELF:-readelf}
		machine_pattern='Advanced Micro Devices X86-64'
		;;
	*)
		die "native Debian architecture '$deb_arch' is unsupported; use ARCH=arm64 for the cross target"
		;;
	esac
	kernel_release=$(make -s -C "$kdir" kernelrelease)
	;;
arm64)
	deb_arch=arm64
	[ -n "$cross_compile" ] || die "CROSS_COMPILE is required when ARCH=arm64"
	target_cc=${CC:-${cross_compile}gcc}
	target_strip=${STRIP:-${cross_compile}strip}
	target_readelf=${READELF:-${cross_compile}readelf}
	machine_pattern='AArch64'
	kernel_release=$(make -s -C "$kdir" ARCH=arm64 \
		CROSS_COMPILE="$cross_compile" kernelrelease)
	;;
x86_64)
	deb_arch=amd64
	target_cc=${CC:-cc}
	target_strip=${STRIP:-strip}
	target_readelf=${READELF:-readelf}
	machine_pattern='Advanced Micro Devices X86-64'
	kernel_release=$(make -s -C "$kdir" ARCH=x86_64 kernelrelease)
	;;
*)
	die "unsupported kernel ARCH: $kernel_arch"
	;;
esac

for command in "$target_cc" "$target_strip" "$target_readelf"; do
	require_command "$command"
done

case "$kernel_release" in
""|*[!A-Za-z0-9.+_~-]*)
	die "unsafe or empty kernel release returned by KDIR: $kernel_release"
	;;
esac

upstream_version=$(sed -n 's/^PACKAGE_VERSION="\([^"]*\)"/\1/p' \
	"$project_dir/dkms.conf")
[ -n "$upstream_version" ] || die "unable to read PACKAGE_VERSION from dkms.conf"
package_version="$upstream_version-$debian_revision"
dpkg --validate-version "$package_version" >/dev/null 2>&1 || \
	die "invalid Debian package version: $package_version"

kernel_package_release=$(printf '%s' "$kernel_release" |
	tr '[:upper:]_' '[:lower:]-')
module_package="sprd-vcom-modules-$kernel_package_release"
common_package=sprd-vcom-common

mkdir -p "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)

SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-}
if [ -z "$SOURCE_DATE_EPOCH" ]; then
	SOURCE_DATE_EPOCH=$(git -C "$project_dir" log -1 --format=%ct 2>/dev/null || true)
fi
[ -n "$SOURCE_DATE_EPOCH" ] || SOURCE_DATE_EPOCH=0
case "$SOURCE_DATE_EPOCH" in
*[!0-9]*) die "SOURCE_DATE_EPOCH must be a non-negative integer" ;;
esac
export SOURCE_DATE_EPOCH

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/sprd-vcom-deb.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT HUP INT TERM
module_build_dir="$work_dir/module-build"
common_root="$work_dir/common"
module_root="$work_dir/module"

install -d -m 0755 "$module_build_dir"
install -m 0644 "$project_dir/sprd_vcom.c" \
	"$module_build_dir/sprd_vcom.c"
printf 'obj-m := sprd_vcom.o\n' >"$module_build_dir/Makefile"

echo "Building sprd_vcom for $kernel_release ($deb_arch)..."
case "$kernel_arch" in
arm64)
	make -C "$kdir" M="$module_build_dir" ARCH=arm64 \
		CROSS_COMPILE="$cross_compile" \
		KCFLAGS="-ffile-prefix-map=$module_build_dir=." W=1 modules
	;;
x86_64)
	make -C "$kdir" M="$module_build_dir" ARCH=x86_64 \
		KCFLAGS="-ffile-prefix-map=$module_build_dir=." W=1 modules
	;;
*)
	make -C "$kdir" M="$module_build_dir" \
		KCFLAGS="-ffile-prefix-map=$module_build_dir=." W=1 modules
	;;
esac

"$target_cc" -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion \
	-Wshadow "$project_dir/tools/sprd-at-tty.c" \
	-o "$work_dir/sprd-at-tty"

"$target_readelf" -h "$module_build_dir/sprd_vcom.ko" |
	grep -F "$machine_pattern" >/dev/null || \
	die "module architecture does not match Debian architecture $deb_arch"
"$target_readelf" -h "$work_dir/sprd-at-tty" |
	grep -F "$machine_pattern" >/dev/null || \
	die "AT client architecture does not match Debian architecture $deb_arch"

actual_vermagic=$(modinfo -F vermagic "$module_build_dir/sprd_vcom.ko" |
	awk 'NR == 1 { print $1 }')
[ "$actual_vermagic" = "$kernel_release" ] || \
	die "module vermagic '$actual_vermagic' does not match '$kernel_release'"

"$target_strip" --strip-debug "$module_build_dir/sprd_vcom.ko"
"$target_strip" --strip-unneeded "$work_dir/sprd-at-tty"
actual_vermagic=$(modinfo -F vermagic "$module_build_dir/sprd_vcom.ko" |
	awk 'NR == 1 { print $1 }')
[ "$actual_vermagic" = "$kernel_release" ] || \
	die "stripping damaged module metadata"

install -d -m 0755 "$common_root/DEBIAN" \
	"$common_root/usr/bin" \
	"$common_root/etc/udev/rules.d" \
	"$common_root/etc/modules-load.d" \
	"$common_root/usr/share/doc/$common_package"
install -m 0755 "$work_dir/sprd-at-tty" \
	"$common_root/usr/bin/sprd-at-tty"
install -m 0644 "$project_dir/packaging/udev/manual/78-mm-sprd-vcom.rules" \
	"$common_root/etc/udev/rules.d/78-mm-sprd-vcom.rules"
install -m 0644 "$project_dir/packaging/udev/99-sprd-vcom.rules" \
	"$common_root/etc/udev/rules.d/99-sprd-vcom.rules"
install -m 0644 "$project_dir/packaging/modules-load.d/sprd_vcom.conf" \
	"$common_root/etc/modules-load.d/sprd_vcom.conf"
install -m 0644 "$project_dir/README.md" \
	"$common_root/usr/share/doc/$common_package/README.md"
install -m 0644 "$project_dir/docs/DEBIAN_PACKAGING.md" \
	"$common_root/usr/share/doc/$common_package/README.Debian"
write_copyright "$common_root/usr/share/doc/$common_package/copyright"
write_changelog "$work_dir/changelog.Debian"
gzip -9n -c "$work_dir/changelog.Debian" > \
	"$common_root/usr/share/doc/$common_package/changelog.Debian.gz"
common_installed_size=$(du -sk "$common_root/etc" "$common_root/usr" |
	awk '{ total += $1 } END { print total }')

cat >"$common_root/DEBIAN/control" <<EOF
Package: $common_package
Version: $package_version
Section: kernel
Priority: optional
Architecture: $deb_arch
Installed-Size: $common_installed_size
Maintainer: m.fahim <fahimohy@gmail.com>
Depends: libc6, kmod, udev
Homepage: https://github.com/mohyfahim/sprd-vcom-driver
Description: userspace support for the UNISOC SPRD VCOM AT driver
 Installs the architecture-native AT client, manual ModemManager policy,
 stable udev links, and module boot-loading configuration shared by
 exact-kernel sprd-vcom module packages.
EOF
cat >"$common_root/DEBIAN/conffiles" <<'EOF'
/etc/modules-load.d/sprd_vcom.conf
/etc/udev/rules.d/78-mm-sprd-vcom.rules
/etc/udev/rules.d/99-sprd-vcom.rules
EOF
write_common_scripts "$common_root"

module_doc_dir="$module_root/usr/share/doc/$module_package"
install -d -m 0755 "$module_root/DEBIAN" \
	"$module_root/lib/modules/$kernel_release/extra" "$module_doc_dir"
install -m 0644 "$module_build_dir/sprd_vcom.ko" \
	"$module_root/lib/modules/$kernel_release/extra/sprd_vcom.ko"
write_copyright "$module_doc_dir/copyright"
install -m 0644 "$common_root/usr/share/doc/$common_package/changelog.Debian.gz" \
	"$module_doc_dir/changelog.Debian.gz"
cat >"$module_doc_dir/README.Debian" <<EOF
This binary module is built only for Linux $kernel_release.

Install it together with $common_package (= $package_version). A new module
package is required after any kernel ABI or release change. The kernel's own
usbserial module is not bundled.
EOF
module_installed_size=$(du -sk "$module_root/lib" "$module_root/usr" |
	awk '{ total += $1 } END { print total }')
cat >"$module_root/DEBIAN/control" <<EOF
Package: $module_package
Version: $package_version
Section: kernel
Priority: optional
Architecture: $deb_arch
Installed-Size: $module_installed_size
Maintainer: m.fahim <fahimohy@gmail.com>
Depends: $common_package (= $package_version), kmod
Homepage: https://github.com/mohyfahim/sprd-vcom-driver
X-Kernel-Release: $kernel_release
Description: UNISOC SPRD VCOM module for Linux $kernel_release
 Provides the sprd_vcom USB-serial kernel module built for the exact Linux
 release named by this package. It exposes verified UNISOC modem AT ports
 without claiming RNDIS or diagnostic interfaces.
EOF
write_module_scripts "$module_root"

common_deb="$output_dir/${common_package}_${package_version}_${deb_arch}.deb"
module_deb="$output_dir/${module_package}_${package_version}_${deb_arch}.deb"
build_package "$common_root" "$common_deb"
build_package "$module_root" "$module_deb"

for package_file in "$common_deb" "$module_deb"; do
	[ "$(dpkg-deb --field "$package_file" Version)" = "$package_version" ] || \
		die "package version validation failed: $package_file"
	[ "$(dpkg-deb --field "$package_file" Architecture)" = "$deb_arch" ] || \
		die "package architecture validation failed: $package_file"
	dpkg-deb --contents "$package_file" >/dev/null
done

echo "Created $common_deb"
echo "Created $module_deb"
