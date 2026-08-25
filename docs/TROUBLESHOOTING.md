# Troubleshooting

## No tty appears

Check `modinfo sprd_vcom`, `dmesg`, and `lsusb -t`. The module intentionally
refuses unexpected interfaces and endpoint layouts. Remove old rules or scripts
that bind the modem to `option` through `new_id`, then reconnect it.

## Module rejected

Secure Boot may reject unsigned external modules. Use the distribution's
DKMS/MOK signing workflow. Also verify that headers exactly matching
`uname -r` are installed.

## No device link

Run `udevadm control --reload-rules`, reconnect the modem, and inspect
`udevadm test /sys/class/tty/ttyUSB0`. The unique link appears below
`/dev/sprd/` and the legacy link is `/dev/sprd-at`.

## Permission denied

Use the distribution's serial-device access policy, commonly membership in
`dialout`. The project does not make modem devices world-writable.

## AT timeout

Stop ModemManager when using the manual client, confirm that the selected path
belongs to `sprd_vcom`, and inspect kernel USB errors. A modem terminal error is
reported separately from a transport timeout.

## RNDIS stopped working

Confirm the network interfaces are still owned by `rndis_host`. This module's
USB alias contains interface 2 and must not bind interfaces 0/1. Include
sanitized `lsusb -t` and `dmesg` output in a bug report.
