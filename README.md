# sprd_vcom Linux driver for D-Link DWR-910M / UNISOC 1782:000c

This is a Linux USB-serial kernel module implementing the working startup
sequence reverse-engineered from the official Windows `sprdvcom.sys` driver.

## What it binds

Only:

    USB 1782:000c MI_02  = SPRD AT
    bulk OUT 0x02
    bulk IN  0x81

It deliberately does NOT bind:

    MI_00/MI_01 = RNDIS
    MI_03       = SPRD DIAG

Therefore the normal RNDIS network connection remains managed by
`rndis_host`.

## Proven initialization

Each tty open performs:

    CLEAR_FEATURE(ENDPOINT_HALT) OUT 0x02
    CLEAR_FEATURE(ENDPOINT_HALT) IN  0x81

    21 22 0000 0002 0000
    21 22 0201 0002 0000

Then Linux's standard usb-serial generic bulk transport is used.

## Build requirements

The running kernel must have USB serial support:

    CONFIG_USB_SERIAL=y

or:

    CONFIG_USB_SERIAL=m

Debian/Ubuntu:

    sudo apt install build-essential linux-headers-$(uname -r)

For DKMS:

    sudo apt install dkms build-essential linux-headers-$(uname -r)

## Recommended installation: DKMS

DKMS automatically rebuilds the module when the distribution kernel is
upgraded.

From this directory:

    sudo ./install-dkms.sh

Reconnect the modem and verify:

    dmesg | grep -i sprd
    lsmod | grep sprd_vcom
    ls -l /dev/sprd-at

Expected:

    /dev/sprd-at -> ttyUSB0

The exact ttyUSB number can vary; always prefer `/dev/sprd-at`.

Test:

    sudo ./sprd-at-tty AT

or after installing the helper:

    /usr/local/bin/sprd-at-tty AT

## Manual build/install

Build:

    make

Load without installing:

    sudo modprobe usbserial
    sudo insmod ./sprd_vcom.ko

Reconnect the modem and test `/dev/sprd-at`.

Permanent installation for the current kernel only:

    sudo make install

This copies:

    /lib/modules/$(uname -r)/extra/sprd_vcom.ko
    /etc/modules-load.d/sprd_vcom.conf
    /etc/udev/rules.d/99-sprd-vcom.rules

and runs `depmod` + `modprobe`.

Important: a manually installed external module normally needs to be rebuilt
after every kernel upgrade. DKMS avoids that.

## Automatic loading at boot

There are two mechanisms:

1. The module contains a USB MODULE_DEVICE_TABLE alias for
   `1782:000c MI_02`, so normal udev/kmod hotplug can automatically load it
   when the modem appears.

2. `sprd_vcom.conf` installs:

       sprd_vcom

   under `/etc/modules-load.d/`, causing systemd-modules-load to load it
   during boot even before the modem is attached.

Using both is intentional and harmless.

## IMPORTANT: remove the old forced option binding

Do not continue dynamically adding this device to the Linux `option` driver:

    echo 1782 000c > /sys/bus/usb-serial/drivers/option1/new_id

Remove any boot script or udev rule that does this.

If `option` currently owns MI_02, either reboot/reconnect after installing
`sprd_vcom`, or unbind it first.

Example, using your previous USB topology:

    echo '1-10.1:1.2' | sudo tee /sys/bus/usb/drivers/option/unbind

Then:

    sudo modprobe sprd_vcom

The simplest clean test is to unplug/replug the modem.

## Stable device path

The included udev rule creates:

    /dev/sprd-at

for the tty owned by this driver.

Use `/dev/sprd-at` instead of assuming `/dev/ttyUSB0`.

## Test application

Build:

    gcc -O2 -Wall -Wextra sprd-at-tty.c -o sprd-at-tty

Install:

    sudo install -m 0755 sprd-at-tty /usr/local/bin/sprd-at-tty

Examples:

    sprd-at-tty AT
    sprd-at-tty ATI
    sprd-at-tty 'AT+COPS?' 'AT+CSQ'
    sprd-at-tty 'AT+CEREG=2' 'AT+CEREG?'

The tool opens `/dev/sprd-at`, which triggers the driver initialization,
then sends all supplied commands during that one tty session.

## Periodic AT queries

Install the example poller:

    sudo install -m 0755 sprd-cell-poll.sh /usr/local/sbin/sprd-cell-poll

Optional systemd timer:

    sudo cp sprd-cell-poll.service /etc/systemd/system/
    sudo cp sprd-cell-poll.timer /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable --now sprd-cell-poll.timer

Default interval is 60 seconds.

Check:

    systemctl list-timers sprd-cell-poll.timer
    journalctl -u sprd-cell-poll.service

Change `OnUnitActiveSec=60s` in the timer if needed, then:

    sudo systemctl daemon-reload
    sudo systemctl restart sprd-cell-poll.timer

## Useful LTE queries

Standard commands:

    AT+COPS?
    AT+CSQ
    AT+CEREG=2
    AT+CEREG?
    AT+CREG=2
    AT+CREG?
    AT+CGREG=2
    AT+CGREG?

For LTE, `AT+CEREG?` may provide TAC and E-UTRAN Cell ID.

## Diagnostics

Module loaded:

    lsmod | grep sprd_vcom

USB binding:

    readlink -f /sys/bus/usb/devices/1-10.1:1.2/driver

Expected driver path contains:

    sprd_vcom

Kernel messages:

    dmesg | tail -100

tty:

    ls -l /dev/ttyUSB* /dev/sprd-at

USB descriptor:

    lsusb -t

## Secure Boot

On PCs with UEFI Secure Boot, the kernel can reject an unsigned external
module with errors such as "Key was rejected by service".

Use your distribution's DKMS/MOK module-signing workflow, or sign the module
with a key trusted by the running kernel. Do not disable Secure Boot unless
that is appropriate for your environment.

## OpenWrt note

Do not copy a `.ko` built on Debian into OpenWrt. OpenWrt kernel modules must
be built against the exact OpenWrt kernel build tree/config and matching
vermagic.

For OpenWrt production integration, add `sprd_vcom.c` as a kmod package or
patch it into `drivers/usb/serial/` and select the package in your firmware
build.

## Scope

This driver only exposes the confirmed runtime AT interface. It has no
Spreadtrum BSL/FDL firmware commands and does not bind the runtime DIAG
interface.
