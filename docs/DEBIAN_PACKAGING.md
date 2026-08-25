# Debian binary packages

The Debian package builder produces a matched pair for one architecture and
one exact kernel release:

- `sprd-vcom-common` contains the native AT client, manual ModemManager-ignore
  policy, stable udev links, documentation, and the modules-load policy.
- `sprd-vcom-modules-<kernel-release>` contains only `sprd_vcom.ko` for that
  exact kernel. It depends on the identical common-package version.

These are binary kernel packages, not DKMS packages. Build and install a new
module package whenever the target kernel release or ABI changes.

## Prerequisites

Install `make`, `gcc`, `binutils`, `dpkg-dev`, `kmod`, and the build files for
the target kernel. The kernel tree must be configured and prepared, and it
must contain a nonempty `Module.symvers`. For arm64, install the
`gcc-aarch64-linux-gnu` and `binutils-aarch64-linux-gnu` cross tools.

## Build one target

Build for the running Debian amd64 kernel:

```sh
make deb
```

`KDIR` defaults to `/lib/modules/$(uname -r)/build`. It can be set explicitly:

```sh
make deb KDIR=/path/to/prepared/amd64-kernel
```

Build for the Orange Pi arm64 kernel:

```sh
make deb ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  KDIR=/path/to/linux-orangepi-orange-pi-6.1-sun50iw9
```

Build both pairs in one command:

```sh
make deb-all \
  ARM64_KDIR=/path/to/linux-orangepi-orange-pi-6.1-sun50iw9
```

`ARM64_CROSS_COMPILE` defaults to `aarch64-linux-gnu-`. Set `DEB_REVISION` to
increment the Debian revision and `DEB_OUTPUT_DIR` to change the output
directory. Packages default to `dist/`.

The builder uses an isolated temporary external-module directory. It checks
the ELF machine type of the module and AT client and requires the first
vermagic field to equal the kernel tree's reported `kernelrelease` exactly.
`SOURCE_DATE_EPOCH` may be set explicitly; otherwise the latest Git commit
timestamp is used.

## Install

Install the architecture-matching pair together so APT can resolve the exact
dependency. For example:

```sh
sudo apt install \
  ./sprd-vcom-common_1.0.0-1_arm64.deb \
  ./sprd-vcom-modules-6.1.31-sun50iw9_1.0.0-1_arm64.deb
```

The module package runs `depmod` for its packaged release. When that release
is currently running, its configuration script tries to load `usbserial` and
then `sprd_vcom`; failures are reported but do not break package
configuration. The modules-load file is the reboot fallback. Removal does not
forcibly unload an active driver.

The packages are unsigned. A system enforcing Secure Boot or module
signatures must sign `sprd_vcom.ko` using a key trusted by the target kernel.

## Validate

```sh
make check-debs
lintian dist/*.deb  # optional
```

The package test extracts every `.deb`, verifies control scripts, conffiles,
checksums, root ownership, exact destinations, architectures, dependencies,
and module vermagic.
