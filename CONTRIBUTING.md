# Contributing

Contributions are welcome for verified UNISOC modem AT interfaces, portability
fixes, tests, documentation, and integration improvements.

## Development workflow

1. Keep each commit focused and buildable.
2. Run `make check`.
3. Test kernel changes on real hardware when they affect device behavior.
4. Add or update tests and documentation.
5. Sign off every commit with `git commit -s` to certify the
   [Developer Certificate of Origin](DCO).

Kernel code must follow Linux style and pass `scripts/checkpatch.pl`. Use
clear commit subjects such as `usb: serial: sprd_vcom: ...`.

## Adding a device

Follow [docs/ADDING_DEVICES.md](docs/ADDING_DEVICES.md). A VID/PID is accepted
only with sanitized USB descriptors, interface and endpoint evidence, a
successful AT transcript, repeated reconnect/open tests, and confirmation from
someone with the hardware. Similar descriptors alone are not enough.

Never publish IMEI, ICCID, IMSI, phone numbers, APNs, credentials, SMS content,
subscriber identifiers, or unredacted modem logs.

## Pull requests

Explain the user-visible problem, the hardware and firmware tested, and the
exact commands used for verification. Keep generated kernel objects and
prebuilt `.ko` files out of commits.

By participating, contributors agree to follow [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
