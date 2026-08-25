# Architecture

The project deliberately separates three responsibilities:

1. `sprd_vcom.c` binds only verified USB AT interfaces, performs the
   vendor-specific activation sequence, and delegates transport to the generic
   USB-serial core.
2. The udev profiles create names and decide whether ModemManager may probe the
   AT port.
3. `sprd-at-tty` is an optional userspace diagnostic client. It is not part of
   the kernel ABI.

## Device profiles

Every ID table entry carries a pointer to `struct sprd_vcom_device_info`. The
profile is the source of truth for USB ID, interface number, bulk endpoints,
control request, activation values, delays, and zero-length-packet behavior.

The probe callback rejects dynamic IDs and entries without a profile. Attach
then verifies the live interface and endpoints before a tty is registered. A
new device may share an existing profile only after real-hardware verification.

## Open sequence

For the DWR-910M, the first tty open clears both endpoint halts, sends class
request `0x22` with values `0x0000` and `0x0201` to interface 2, observes the
verified delays, and starts generic receive URBs. Closing stops the generic
transport; the next first-open repeats initialization.

The module never implements flash, BSL/FDL, diagnostic, erase, or firmware
operations.

## Stable interfaces

The stable kernel-facing interfaces are module `sprd_vcom` and normal
`ttyUSB*` devices. Udev adds unique convenience links and the legacy
`/dev/sprd-at` link. The latter is intentionally documented as ambiguous with
more than one modem.
