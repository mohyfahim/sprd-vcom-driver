// SPDX-License-Identifier: GPL-2.0
/*
 * USB serial driver for UNISOC SPRD VCOM AT ports
 *
 * Copyright (C) 2026 m.fahim <fahimohy@gmail.com>
 *
 * The D-Link DWR-910M startup sequence was reverse-engineered from the
 * official Windows sprdvcom.sys driver and verified with USB captures.
 * This driver only claims explicitly listed AT interfaces. It must never
 * claim RNDIS, diagnostic, or firmware-download interfaces.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_port.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define UNISOC_VENDOR_ID		0x1782
#define DWR910M_PRODUCT_ID		0x000c

#define SPRD_CTRL_TIMEOUT_MS		5000
#define SPRD_BULK_IN_SIZE		4096

#define SPRD_VCOM_ZLP			BIT(0)

#define DRIVER_AUTHOR	"m.fahim <fahimohy@gmail.com>"
#define DRIVER_DESC	"UNISOC SPRD VCOM AT USB serial driver"

/**
 * struct sprd_vcom_device_info - verified device-specific VCOM parameters
 * @name: human-readable device name used in diagnostics
 * @vendor: verified USB vendor ID
 * @product: verified USB product ID
 * @interface_number: USB interface containing the AT port
 * @bulk_in: expected bulk-IN endpoint address
 * @bulk_out: expected bulk-OUT endpoint address
 * @request_type: bmRequestType for port activation requests
 * @request: bRequest for port activation requests
 * @prepare_value: wValue sent before activating the port
 * @activate_value: wValue that activates the port
 * @prepare_delay_ms: delay after the prepare request
 * @activate_delay_ms: delay after the activate request
 * @flags: device quirks, including %SPRD_VCOM_ZLP
 *
 * Every device ID must reference a profile validated on real hardware.
 * Keeping all interface assumptions here makes additions reviewable and
 * prevents a new ID from accidentally claiming another modem function.
 */
struct sprd_vcom_device_info {
	const char *name;
	u16 vendor;
	u16 product;
	u8 interface_number;
	u8 bulk_in;
	u8 bulk_out;
	u8 request_type;
	u8 request;
	u16 prepare_value;
	u16 activate_value;
	u16 prepare_delay_ms;
	u16 activate_delay_ms;
	unsigned long flags;
};

static const struct sprd_vcom_device_info dwr910m_info = {
	.name = "D-Link DWR-910M",
	.vendor = UNISOC_VENDOR_ID,
	.product = DWR910M_PRODUCT_ID,
	.interface_number = 2,
	.bulk_in = 0x81,
	.bulk_out = 0x02,
	.request_type = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
	.request = 0x22,
	.prepare_value = 0x0000,
	.activate_value = 0x0201,
	.prepare_delay_ms = 50,
	.activate_delay_ms = 100,
	.flags = SPRD_VCOM_ZLP,
};

static const struct usb_device_id sprd_vcom_id_table[] = {
	{
		USB_DEVICE_INTERFACE_NUMBER(UNISOC_VENDOR_ID,
					    DWR910M_PRODUCT_ID, 2),
		.driver_info = (kernel_ulong_t)&dwr910m_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(usb, sprd_vcom_id_table);

static const struct sprd_vcom_device_info *
sprd_vcom_info(struct usb_serial *serial)
{
	return usb_get_serial_data(serial);
}

static int sprd_vcom_probe(struct usb_serial *serial,
			   const struct usb_device_id *id)
{
	const struct sprd_vcom_device_info *info;
	u8 ifnum;

	if (!id || !id->driver_info) {
		dev_err(&serial->interface->dev,
			"refusing device without a verified quirk profile\n");
		return -ENODEV;
	}

	info = (const void *)id->driver_info;
	if (le16_to_cpu(serial->dev->descriptor.idVendor) != info->vendor ||
	    le16_to_cpu(serial->dev->descriptor.idProduct) != info->product) {
		dev_err(&serial->interface->dev,
			"%s: refusing unverified USB ID %04x:%04x\n",
			info->name,
			le16_to_cpu(serial->dev->descriptor.idVendor),
			le16_to_cpu(serial->dev->descriptor.idProduct));
		return -ENODEV;
	}

	ifnum = serial->interface->cur_altsetting->desc.bInterfaceNumber;
	if (ifnum != info->interface_number) {
		dev_err(&serial->interface->dev,
			"%s: refusing interface %u, expected %u\n",
			info->name, ifnum, info->interface_number);
		return -ENODEV;
	}

	usb_set_serial_data(serial, (void *)info);

	return 0;
}

static int sprd_vcom_control(struct usb_serial *serial, u16 value)
{
	const struct sprd_vcom_device_info *info = sprd_vcom_info(serial);
	u16 ifnum;
	int ret;

	if (!info)
		return -ENODEV;

	ifnum = serial->interface->cur_altsetting->desc.bInterfaceNumber;
	ret = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			      info->request, info->request_type, value, ifnum,
			      NULL, 0, SPRD_CTRL_TIMEOUT_MS);
	if (ret < 0) {
		dev_err(&serial->interface->dev,
			"%s: control request %02x value %04x failed: %d\n",
			info->name, info->request, value, ret);
		return ret;
	}

	if (ret != 0) {
		dev_err(&serial->interface->dev,
			"%s: control request returned unexpected length %d\n",
			info->name, ret);
		return -EIO;
	}

	dev_dbg(&serial->interface->dev,
		"%s: control request %02x value %04x succeeded\n",
		info->name, info->request, value);

	return 0;
}

static int sprd_vcom_clear_pipes(struct usb_serial_port *port)
{
	struct usb_device *udev = port->serial->dev;
	unsigned int pipe;
	int ret;

	if (!port->bulk_out_endpointAddress ||
	    !port->bulk_in_endpointAddress)
		return -ENODEV;

	pipe = usb_sndbulkpipe(udev, port->bulk_out_endpointAddress);
	ret = usb_clear_halt(udev, pipe);
	if (ret) {
		dev_err(&port->dev,
			"failed to clear bulk-OUT endpoint 0x%02x: %d\n",
			port->bulk_out_endpointAddress, ret);
		return ret;
	}

	pipe = usb_rcvbulkpipe(udev, port->bulk_in_endpointAddress);
	ret = usb_clear_halt(udev, pipe);
	if (ret) {
		dev_err(&port->dev,
			"failed to clear bulk-IN endpoint 0x%02x: %d\n",
			port->bulk_in_endpointAddress, ret);
		return ret;
	}

	return 0;
}

static int sprd_vcom_attach(struct usb_serial *serial)
{
	const struct sprd_vcom_device_info *info = sprd_vcom_info(serial);
	struct usb_serial_port *port;

	if (!info)
		return -ENODEV;

	if (serial->num_ports != 1 || serial->num_bulk_in < 1 ||
	    serial->num_bulk_out < 1) {
		dev_err(&serial->interface->dev,
			"%s: unexpected layout: ports=%d bulk-in=%d bulk-out=%d\n",
			info->name, serial->num_ports, serial->num_bulk_in,
			serial->num_bulk_out);
		return -ENODEV;
	}

	port = serial->port[0];
	if (!port)
		return -ENODEV;

	if (port->bulk_in_endpointAddress != info->bulk_in ||
	    port->bulk_out_endpointAddress != info->bulk_out) {
		dev_err(&serial->interface->dev,
			"%s: unexpected AT endpoints: IN=0x%02x OUT=0x%02x\n",
			info->name, port->bulk_in_endpointAddress,
			port->bulk_out_endpointAddress);
		return -ENODEV;
	}

	dev_info(&serial->interface->dev,
		 "%s AT port (interface %u, IN=0x%02x, OUT=0x%02x)\n",
		 info->name, info->interface_number, info->bulk_in,
		 info->bulk_out);

	return 0;
}

static int sprd_vcom_port_probe(struct usb_serial_port *port)
{
	const struct sprd_vcom_device_info *info;
	unsigned int i;

	info = sprd_vcom_info(port->serial);
	if (!info)
		return -ENODEV;

	if (!(info->flags & SPRD_VCOM_ZLP))
		return 0;

	for (i = 0; i < ARRAY_SIZE(port->write_urbs); i++) {
		if (port->write_urbs[i])
			port->write_urbs[i]->transfer_flags |= URB_ZERO_PACKET;
	}

	return 0;
}

static int sprd_vcom_open(struct tty_struct *tty,
			  struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	const struct sprd_vcom_device_info *info = sprd_vcom_info(serial);
	int ret;

	if (!info)
		return -ENODEV;

	ret = sprd_vcom_clear_pipes(port);
	if (ret)
		return ret;

	ret = sprd_vcom_control(serial, info->prepare_value);
	if (ret)
		return ret;

	if (info->prepare_delay_ms)
		msleep(info->prepare_delay_ms);

	ret = sprd_vcom_control(serial, info->activate_value);
	if (ret)
		return ret;

	if (info->activate_delay_ms)
		msleep(info->activate_delay_ms);

	ret = usb_serial_generic_open(tty, port);
	if (ret)
		dev_err(&port->dev, "%s: generic open failed: %d\n",
			info->name, ret);
	else
		dev_dbg(&port->dev, "%s: AT port opened\n", info->name);

	return ret;
}

static void sprd_vcom_close(struct usb_serial_port *port)
{
	const struct sprd_vcom_device_info *info;

	info = sprd_vcom_info(port->serial);
	usb_serial_generic_close(port);

	if (info)
		dev_dbg(&port->dev, "%s: AT port closed\n", info->name);
}

static struct usb_serial_driver sprd_vcom_device = {
	.driver = {
		.name = "sprd_vcom",
	},
	.description = DRIVER_DESC,
	.id_table = sprd_vcom_id_table,
	.num_ports = 1,
	.num_bulk_in = 1,
	.num_bulk_out = 1,
	.bulk_in_size = SPRD_BULK_IN_SIZE,
	.probe = sprd_vcom_probe,
	.attach = sprd_vcom_attach,
	.port_probe = sprd_vcom_port_probe,
	.open = sprd_vcom_open,
	.close = sprd_vcom_close,
};

static struct usb_serial_driver * const sprd_vcom_drivers[] = {
	&sprd_vcom_device,
	NULL
};

module_usb_serial_driver(sprd_vcom_drivers, sprd_vcom_id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
