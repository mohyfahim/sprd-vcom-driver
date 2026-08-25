#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

arm64_kdir=${ARM64_KDIR:-}
native_kdir=${NATIVE_KDIR:-/lib/modules/$(uname -r)/build}
arm64_cross_compile=${ARM64_CROSS_COMPILE:-aarch64-linux-gnu-}
output_dir=${DEB_OUTPUT_DIR:-$project_dir/dist}
debian_revision=${DEB_REVISION:-1}

if [ -z "$arm64_kdir" ]; then
	echo "build-deb-all: ARM64_KDIR is required" >&2
	exit 2
fi

KDIR=$native_kdir ARCH= CROSS_COMPILE= \
	DEB_OUTPUT_DIR=$output_dir DEB_REVISION=$debian_revision \
	"$script_dir/build-deb.sh"

KDIR=$arm64_kdir ARCH=arm64 CROSS_COMPILE=$arm64_cross_compile \
	DEB_OUTPUT_DIR=$output_dir DEB_REVISION=$debian_revision \
	"$script_dir/build-deb.sh"

"$project_dir/tests/test-deb-packages.sh" "$output_dir"
