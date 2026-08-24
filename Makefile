ifneq ($(KERNELRELEASE),)
obj-m := sprd_vcom.o
else

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

.PHONY: all clean install uninstall

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	install -D -m 0644 sprd_vcom.ko \
		/lib/modules/$(shell uname -r)/extra/sprd_vcom.ko
	depmod -a
	install -D -m 0644 99-sprd-vcom.rules \
		/etc/udev/rules.d/99-sprd-vcom.rules
 	install -D -m 0644 99-unisoc-at-ignore.rules \
        /etc/udev/rules.d/99-unisoc-at-ignore.rules
	install -D -m 0644 sprd_vcom.conf \
		/etc/modules-load.d/sprd_vcom.conf
	udevadm control --reload-rules || true
	modprobe sprd_vcom

uninstall:
	modprobe -r sprd_vcom || true
	rm -f /lib/modules/$(shell uname -r)/extra/sprd_vcom.ko
	rm -f /etc/udev/rules.d/99-sprd-vcom.rules
	rm -f /etc/modules-load.d/sprd_vcom.conf
	depmod -a
	udevadm control --reload-rules || true

endif
