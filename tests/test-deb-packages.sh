#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
package_dir=${1:-$root/dist}

die()
{
	echo "test-deb-packages: $*" >&2
	exit 1
}

for command in dpkg-deb md5sum modinfo readelf; do
	command -v "$command" >/dev/null 2>&1 || \
		die "required command not found: $command"
done

[ -d "$package_dir" ] || die "package directory does not exist: $package_dir"
set -- "$package_dir"/*.deb
[ -e "$1" ] || die "no Debian packages found in $package_dir"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/sprd-vcom-deb-test.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT HUP INT TERM
package_count=0

for package_file in "$@"; do
	package_count=$((package_count + 1))
	package_name=$(dpkg-deb --field "$package_file" Package)
	package_version=$(dpkg-deb --field "$package_file" Version)
	package_arch=$(dpkg-deb --field "$package_file" Architecture)
	package_root="$work_dir/$package_count/root"
	control_root="$work_dir/$package_count/control"
	mkdir -p "$package_root" "$control_root"

	dpkg-deb --extract "$package_file" "$package_root"
	dpkg-deb --control "$package_file" "$control_root"
	dpkg-deb --contents "$package_file" |
		awk '$2 != "root/root" { bad = 1 } END { exit bad }' || \
		die "$package_file contains non-root ownership"
	dpkg-deb --contents "$package_file" |
		awk 'substr($1, 6, 1) == "w" || substr($1, 9, 1) == "w" { bad = 1 }
			END { exit bad }' || \
		die "$package_file contains group- or world-writable files"
	[ -n "$(dpkg-deb --field "$package_file" Installed-Size)" ] || \
		die "$package_file has no Installed-Size field"
	(
		cd "$package_root"
		md5sum --check "$control_root/md5sums" >/dev/null
	) || die "$package_file has invalid MD5 checksums"

	for maintainer_script in postinst postrm; do
		[ -x "$control_root/$maintainer_script" ] || \
			die "$package_name is missing executable $maintainer_script"
		sh -n "$control_root/$maintainer_script"
	done

	case "$package_arch" in
	amd64) machine_pattern='Advanced Micro Devices X86-64' ;;
	arm64) machine_pattern='AArch64' ;;
	*) die "$package_name has unsupported architecture $package_arch" ;;
	esac

	case "$package_name" in
	sprd-vcom-common)
		[ -x "$package_root/usr/bin/sprd-at-tty" ] || \
			die "$package_name is missing sprd-at-tty"
		readelf -h "$package_root/usr/bin/sprd-at-tty" |
			grep -F "$machine_pattern" >/dev/null || \
			die "$package_name contains the wrong AT client architecture"
		cmp "$root/packaging/modules-load.d/sprd_vcom.conf" \
			"$package_root/etc/modules-load.d/sprd_vcom.conf" >/dev/null || \
			die "$package_name has the wrong modules-load policy"
		cmp "$root/packaging/udev/manual/78-mm-sprd-vcom.rules" \
			"$package_root/etc/udev/rules.d/78-mm-sprd-vcom.rules" >/dev/null || \
			die "$package_name has the wrong manual udev profile"
		cmp "$root/packaging/udev/99-sprd-vcom.rules" \
			"$package_root/etc/udev/rules.d/99-sprd-vcom.rules" >/dev/null || \
			die "$package_name has the wrong stable-link udev rules"
		for conffile in \
			/etc/modules-load.d/sprd_vcom.conf \
			/etc/udev/rules.d/78-mm-sprd-vcom.rules \
			/etc/udev/rules.d/99-sprd-vcom.rules; do
			grep -Fx "$conffile" "$control_root/conffiles" >/dev/null || \
				die "$package_name does not mark $conffile as a conffile"
		done
		;;
	sprd-vcom-modules-*)
		kernel_release=$(dpkg-deb --field "$package_file" X-Kernel-Release)
		[ -n "$kernel_release" ] || die "$package_name lacks X-Kernel-Release"
		expected_name=$(printf 'sprd-vcom-modules-%s' "$kernel_release" |
			tr '[:upper:]_' '[:lower:]-')
		[ "$package_name" = "$expected_name" ] || \
			die "$package_name does not match kernel release $kernel_release"
		expected_common="$package_dir/sprd-vcom-common_${package_version}_${package_arch}.deb"
		[ -f "$expected_common" ] || \
			die "$package_name has no matching common package artifact"
		module_path="$package_root/lib/modules/$kernel_release/extra/sprd_vcom.ko"
		[ -f "$module_path" ] || \
			die "$package_name has no module at the exact kernel path"
		actual_vermagic=$(modinfo -F vermagic "$module_path" |
			awk 'NR == 1 { print $1 }')
		[ "$actual_vermagic" = "$kernel_release" ] || \
			die "$package_name vermagic is $actual_vermagic, expected $kernel_release"
		readelf -h "$module_path" | grep -F "$machine_pattern" >/dev/null || \
			die "$package_name contains the wrong module architecture"
		depends=$(dpkg-deb --field "$package_file" Depends)
		case "$depends" in
		*"sprd-vcom-common (= $package_version)"*) ;;
		*) die "$package_name lacks an exact common-package dependency" ;;
		esac
		;;
	*)
		die "unexpected package in output directory: $package_name"
		;;
	esac
done

if command -v lintian >/dev/null 2>&1; then
	lintian "$@"
fi

echo "Validated $package_count Debian package(s)."
