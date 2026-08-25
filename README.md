# UNISOC SPRD VCOM Linux driver

`sprd_vcom` is a focused USB-serial driver for verified UNISOC/Spreadtrum
modem AT interfaces. It enables debugging and standard modem control without
claiming the modem's RNDIS, diagnostic, or firmware-download functions.

The initial supported device is the D-Link DWR-910M (`1782:000c`). Its port
activation sequence was reverse-engineered from the official Windows
`sprdvcom.sys` driver and verified using USB captures.

## Supported hardware

| Device | USB ID | AT interface | Endpoints | Status |
| --- | --- | ---: | --- | --- |
| D-Link DWR-910M | `1782:000c` | 2 | IN `0x81`, OUT `0x02` | Hardware verified |

Only explicitly listed interfaces bind. For the DWR-910M, interfaces 0/1
remain available to `rndis_host`, interface 2 becomes a `ttyUSB` AT port, and
interface 3 remains unclaimed by this driver.

## Install with DKMS

DKMS rebuilds the module after kernel upgrades. On Debian/Ubuntu:

```sh
sudo apt install dkms build-essential linux-headers-$(uname -r)
sudo ./install-dkms.sh
```

The default `manual` profile prevents ModemManager from taking the AT port.
To opt into ModemManager detection and standard AT control:

```sh
sudo ./install-dkms.sh --mode=modemmanager
```

Reconnect the modem after changing modes. The installer is idempotent and may
be run from any directory.

Remove everything installed by the DKMS installer with:

```sh
sudo ./uninstall-dkms.sh
```

## Build and test

```sh
make
make check
```

Useful targets include `module`, `tools`, `clean`, `install`, `deb`,
`deb-all`, `dkms-install`, and `dkms-uninstall`. Override `KDIR` to build
against another prepared kernel tree. `make install MODE=manual` installs for
the current kernel only; use `MODE=modemmanager` for the opt-in profile.
`DESTDIR` is supported for staged installation.

Exact-kernel Debian packages can be built for native amd64 and cross-compiled
Orange Pi arm64 targets. See
[Debian binary packages](docs/DEBIAN_PACKAGING.md) for prerequisites, build
commands, validation, and pairwise installation instructions.

The maintained standalone compatibility baseline is Linux 5.10 and newer.

## Use the AT port

The udev rules provide:

- `/dev/sprd-at` as a compatibility link; it is ambiguous with multiple
  modems.
- `/dev/sprd/at-*` as a per-device link derived from USB serial or physical
  path.

The client automatically selects a single unique port:

```sh
sprd-at-tty AT
sprd-at-tty ATI 'AT+CSQ' 'AT+CEREG?'
sprd-at-tty -l
sprd-at-tty -d /dev/sprd/at-example 'AT+COPS?'
```

It exits nonzero on timeout, transport failure, disconnect, or a terminal
modem error. Do not run it concurrently with ModemManager on the same AT port.

## ModemManager scope

The opt-in profile tags this interface as the primary AT control port.
Expected support includes device identity, signal, registration, and
standard SIM/SMS operations implemented by the modem firmware.

This does **not** promise that ModemManager can create or tear down a data
bearer through the DWR-910M's vendor RNDIS implementation. RNDIS remains a
separate network interface and managed bearer support requires additional
hardware research. See [docs/MODEMMANAGER.md](docs/MODEMMANAGER.md).

## Contributing device support

Do not use the usb-serial `new_id` interface with arbitrary UNISOC devices.
Each accepted ID must have a verified device profile describing the exact AT
interface, endpoints, activation requests, timing, and ZLP behavior.

Read [CONTRIBUTING.md](CONTRIBUTING.md) and
[docs/ADDING_DEVICES.md](docs/ADDING_DEVICES.md) before submitting an ID.
Remove IMEI, ICCID, phone numbers, subscriber data, APNs, and credentials from
all public logs.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Supported-device evidence](docs/SUPPORTED_DEVICES.md)
- [Hardware acceptance tests](docs/HARDWARE_TESTING.md)
- [Debian binary packages](docs/DEBIAN_PACKAGING.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Upstreaming](docs/UPSTREAMING.md)

OpenWrt modules must be built in the matching OpenWrt build tree; never copy a
Debian-built `.ko` into OpenWrt. Secure Boot systems must sign the DKMS module
with a key trusted by the running kernel.

## License

The project is licensed under GPL-2.0-only. See [LICENSE](LICENSE).
