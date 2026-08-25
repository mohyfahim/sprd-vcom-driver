#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$root/build"

mkdir -p "$build_dir"

cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
	-Wno-unused-function -pthread "$root/tests/test-response.c" \
	-o "$build_dir/test-response"
"$build_dir/test-response"
python3 "$root/tests/test-at-client.py"

for script in "$root/install-dkms.sh" "$root/uninstall-dkms.sh" \
	"$root/examples/sprd-cell-poll.sh" \
	"$root/packaging/build-deb.sh" \
	"$root/packaging/build-deb-all.sh" \
	"$root/tests/test-deb-packages.sh"; do
	sh -n "$script"
done

if command -v udevadm >/dev/null 2>&1 && \
	udevadm verify --help >/dev/null 2>&1; then
	udevadm verify \
		"$root/packaging/udev/99-sprd-vcom.rules" \
		"$root/packaging/udev/manual/78-mm-sprd-vcom.rules" \
		"$root/packaging/udev/modemmanager/78-mm-sprd-vcom.rules"
fi

if command -v systemd-analyze >/dev/null 2>&1; then
	verify_log="$build_dir/systemd-verify.log"
	if ! systemd-analyze verify "$root/examples/sprd-cell-poll.service" \
		"$root/examples/sprd-cell-poll.timer" 2>"$verify_log"; then
		if grep -q "Operation not permitted" "$verify_log"; then
			echo "systemd verification skipped by sandbox restrictions"
		else
			cat "$verify_log" >&2
			exit 1
		fi
	fi
fi

if command -v shellcheck >/dev/null 2>&1; then
	shellcheck "$root/install-dkms.sh" "$root/uninstall-dkms.sh" \
		"$root/examples/sprd-cell-poll.sh" "$root/tests/run-tests.sh" \
		"$root/packaging/build-deb.sh" \
		"$root/packaging/build-deb-all.sh" \
		"$root/tests/test-deb-packages.sh"
fi

echo "All local tests passed."
