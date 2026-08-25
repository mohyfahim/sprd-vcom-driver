ifneq ($(KERNELRELEASE),)
obj-m := sprd_vcom.o
else

KERNEL_RELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_RELEASE)/build
BUILD_DIR ?= $(CURDIR)/build
HOSTCC ?= cc
HOSTCFLAGS ?= -O2 -g
WARN_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
TOOL := $(BUILD_DIR)/sprd-at-tty

DESTDIR ?=
PREFIX ?= /usr/local
MODULE_DIR ?= /lib/modules/$(KERNEL_RELEASE)/extra
UDEV_RULES_DIR ?= /etc/udev/rules.d
MODULES_LOAD_DIR ?= /etc/modules-load.d
MODE ?= manual
DEB_OUTPUT_DIR ?= $(CURDIR)/dist
DEB_REVISION ?= 1
ARM64_CROSS_COMPILE ?= aarch64-linux-gnu-

.PHONY: all module tools check clean install uninstall
.PHONY: dkms-install dkms-uninstall
.PHONY: deb deb-all check-debs

all: module tools

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

tools: $(TOOL)

$(TOOL): tools/sprd-at-tty.c
	mkdir -p $(BUILD_DIR)
	$(HOSTCC) $(HOSTCFLAGS) $(WARN_CFLAGS) $< -o $@

check:
	$(MAKE) -C $(KDIR) M=$(CURDIR) W=1 modules
	$(MAKE) -B HOSTCFLAGS="$(HOSTCFLAGS) -Werror" tools
	./tests/run-tests.sh

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f $(TOOL)
	rmdir $(BUILD_DIR) 2>/dev/null || true

install: all
	@case "$(MODE)" in manual|modemmanager) ;; *) \
		echo "MODE must be manual or modemmanager" >&2; exit 2;; esac
	install -D -m 0644 sprd_vcom.ko \
		$(DESTDIR)$(MODULE_DIR)/sprd_vcom.ko
	install -D -m 0755 $(TOOL) \
		$(DESTDIR)$(PREFIX)/bin/sprd-at-tty
	install -D -m 0644 packaging/udev/99-sprd-vcom.rules \
		$(DESTDIR)$(UDEV_RULES_DIR)/99-sprd-vcom.rules
	install -D -m 0644 packaging/udev/$(MODE)/78-mm-sprd-vcom.rules \
		$(DESTDIR)$(UDEV_RULES_DIR)/78-mm-sprd-vcom.rules
	install -D -m 0644 packaging/modules-load.d/sprd_vcom.conf \
		$(DESTDIR)$(MODULES_LOAD_DIR)/sprd_vcom.conf
	@if [ -z "$(DESTDIR)" ]; then \
		rm -f /etc/udev/rules.d/99-unisoc-at-ignore.rules; \
		depmod -a "$(KERNEL_RELEASE)"; \
		udevadm control --reload-rules || true; \
		modprobe usbserial; \
		modprobe sprd_vcom; \
	fi

uninstall:
	@if [ -n "$(DESTDIR)" ]; then \
		echo "uninstall does not support DESTDIR" >&2; exit 2; \
	fi
	-modprobe -r sprd_vcom
	rm -f $(MODULE_DIR)/sprd_vcom.ko
	rm -f $(PREFIX)/bin/sprd-at-tty
	rm -f $(UDEV_RULES_DIR)/99-sprd-vcom.rules
	rm -f $(UDEV_RULES_DIR)/78-mm-sprd-vcom.rules
	rm -f /etc/udev/rules.d/99-unisoc-at-ignore.rules
	rm -f /etc/modules-load.d/sprd_vcom.conf
	depmod -a "$(KERNEL_RELEASE)"
	-udevadm control --reload-rules

dkms-install:
	./install-dkms.sh --mode=$(MODE)

dkms-uninstall:
	./uninstall-dkms.sh

deb:
	KDIR="$(KDIR)" ARCH="$(ARCH)" CROSS_COMPILE="$(CROSS_COMPILE)" \
		DEB_OUTPUT_DIR="$(DEB_OUTPUT_DIR)" DEB_REVISION="$(DEB_REVISION)" \
		./packaging/build-deb.sh

deb-all:
	@test -n "$(ARM64_KDIR)" || { \
		echo "ARM64_KDIR is required" >&2; exit 2; \
	}
	NATIVE_KDIR="$(KDIR)" ARM64_KDIR="$(ARM64_KDIR)" \
		ARM64_CROSS_COMPILE="$(ARM64_CROSS_COMPILE)" \
		DEB_OUTPUT_DIR="$(DEB_OUTPUT_DIR)" DEB_REVISION="$(DEB_REVISION)" \
		./packaging/build-deb-all.sh

check-debs:
	./tests/test-deb-packages.sh "$(DEB_OUTPUT_DIR)"

endif
