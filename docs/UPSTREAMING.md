# Upstreaming

The standalone `sprd_vcom.c` is kept suitable for copying to
`drivers/usb/serial/sprd_vcom.c` in a current Linux tree. The companion
Kconfig and Makefile fragments are under `upstream/`.

Prepare changes against current mainline, not a distribution headers tree:

1. Copy the driver and add the Kconfig, Kbuild, and MAINTAINERS entries.
2. Build as both module and built-in where practical.
3. Run `scripts/checkpatch.pl --strict`, sparse, and relevant kernel builds.
4. Run the full DWR-910M hardware checklist.
5. Use `scripts/get_maintainer.pl` to select recipients.
6. Create a focused `usb: serial: ...` patch with the hardware evidence and a
   `Signed-off-by` line.

Follow the Linux kernel submission and DCO rules. The GitHub release and DKMS
packaging are not part of the kernel patch.
