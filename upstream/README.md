# Linux-tree integration fragments

Copy `sprd_vcom.c` to `drivers/usb/serial/` in a current Linux mainline tree.
Insert the Kconfig entry beside the other USB-serial drivers, append the
Makefile line to `drivers/usb/serial/Makefile`, and add the supplied MAINTAINERS
record in alphabetical order. Keep the resulting kernel patch focused on the
driver and its integration records.

These fragments are maintained as review aids rather than a generated patch so
the driver source is not duplicated and the submission can be rebased cleanly
onto current mainline.
