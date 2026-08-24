// SPDX-License-Identifier: GPL-2.0
/*
 * sprd_vcom.c - USB serial driver for D-Link DWR-910M / UNISOC 1782:000c
 *
 * Reverse-engineered from the official Windows sprdvcom.sys driver and a
 * USBPcap capture of a successful Hercules "AT" -> "OK" exchange.
 *
 * Confirmed USB layout:
 *
 *   1782:000c
 *     MI_00/MI_01 : RNDIS (must remain owned by rndis_host)
 *     MI_02       : SPRD AT
 *                   bulk OUT 0x02, bulk IN 0x81
 *     MI_03       : SPRD DIAG (not bound by this module)
 *
 * This driver intentionally binds ONLY MI_02.
 *
 * Required MI_02 startup sequence established experimentally:
 *
 *   CLEAR_FEATURE(ENDPOINT_HALT) for bulk OUT and IN
 *   21 22 0000 0002 0000
 *   21 22 0201 0002 0000
 *
 * After that, the interface behaves as an ordinary USB serial byte stream.
 *
 * The Windows INF also enables USB ZLP handling.  Setting URB_ZERO_PACKET on
 * the generic usb-serial write URBs reproduces the usual USB semantics:
 * an extra zero-length packet is sent only when a write transfer length is an
 * exact multiple of the endpoint max-packet size (512 bytes here).  This does
 * not affect normal short AT commands.
 *
 * This module contains NO BSL/FDL/flash/read/write/erase/execute commands.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_port.h>

#include <linux/usb.h>
#include <linux/usb/serial.h>

#define SPRD_VENDOR_ID 0x1782
#define SPRD_DWR910M_PRODUCT_ID 0x000c
#define SPRD_AT_INTERFACE 2

#define SPRD_REQ_TYPE (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)
#define SPRD_REQ 0x22
#define SPRD_PREPARE_VALUE 0x0000
#define SPRD_OPEN_VALUE 0x0201
#define SPRD_CTRL_TIMEOUT_MS 5000

#define SPRD_BULK_IN_EXPECTED 0x81
#define SPRD_BULK_OUT_EXPECTED 0x02

#define DRIVER_DESC "UNISOC SPRD VCOM AT driver for D-Link DWR-910M"

static const struct usb_device_id sprd_vcom_id_table[] = {
    {USB_DEVICE_INTERFACE_NUMBER(SPRD_VENDOR_ID, SPRD_DWR910M_PRODUCT_ID,
                                 SPRD_AT_INTERFACE)},
    {}};
MODULE_DEVICE_TABLE(usb, sprd_vcom_id_table);

static int sprd_vcom_control(struct usb_serial *serial, u16 value) {
  struct usb_host_interface *alt;
  u16 ifnum;
  int ret;

  alt = serial->interface->cur_altsetting;
  ifnum = alt->desc.bInterfaceNumber;

  ret = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0), SPRD_REQ,
                        SPRD_REQ_TYPE, value, ifnum, NULL, 0,
                        SPRD_CTRL_TIMEOUT_MS);
  if (ret < 0) {
    dev_err(&serial->interface->dev,
            "SPRD control 21/22 value=%04x if=%u failed: %d\n", value, ifnum,
            ret);
    return ret;
  }

  dev_dbg(&serial->interface->dev,
          "SPRD control 21/22 value=%04x if=%u succeeded\n", value, ifnum);

  return 0;
}

static int sprd_vcom_clear_pipes(struct usb_serial_port *port) {
  struct usb_device *udev = port->serial->dev;
  unsigned int pipe;
  int ret;

  if (!port->bulk_out_endpointAddress || !port->bulk_in_endpointAddress)
    return -ENODEV;

  pipe = usb_sndbulkpipe(udev, port->bulk_out_endpointAddress);
  ret = usb_clear_halt(udev, pipe);
  if (ret) {
    dev_err(&port->dev, "failed to clear bulk OUT 0x%02x halt: %d\n",
            port->bulk_out_endpointAddress, ret);
    return ret;
  }

  pipe = usb_rcvbulkpipe(udev, port->bulk_in_endpointAddress);
  ret = usb_clear_halt(udev, pipe);
  if (ret) {
    dev_err(&port->dev, "failed to clear bulk IN 0x%02x halt: %d\n",
            port->bulk_in_endpointAddress, ret);
    return ret;
  }

  return 0;
}

/*
 * Verify that we bound the interface/endpoints we reverse-engineered.
 * Failing rather than guessing protects the RNDIS/DIAG interfaces if a future
 * firmware revision changes its USB layout.
 */
static int sprd_vcom_attach(struct usb_serial *serial) {
  struct usb_serial_port *port;
  u8 ifnum;

  ifnum = serial->interface->cur_altsetting->desc.bInterfaceNumber;

  if (ifnum != SPRD_AT_INTERFACE) {
    dev_err(&serial->interface->dev, "refusing unexpected interface %u\n",
            ifnum);
    return -ENODEV;
  }

  if (serial->num_ports != 1 || serial->num_bulk_in < 1 ||
      serial->num_bulk_out < 1) {
    dev_err(&serial->interface->dev,
            "unexpected endpoint layout: ports=%u bulk-in=%u bulk-out=%u\n",
            serial->num_ports, serial->num_bulk_in, serial->num_bulk_out);
    return -ENODEV;
  }

  port = serial->port[0];
  if (!port)
    return -ENODEV;

  if (port->bulk_in_endpointAddress != SPRD_BULK_IN_EXPECTED ||
      port->bulk_out_endpointAddress != SPRD_BULK_OUT_EXPECTED) {
    dev_err(&serial->interface->dev,
            "unexpected AT endpoints: IN=0x%02x OUT=0x%02x\n",
            port->bulk_in_endpointAddress, port->bulk_out_endpointAddress);
    return -ENODEV;
  }

  dev_info(&serial->interface->dev,
           "DWR-910M SPRD AT interface detected (IN=0x%02x OUT=0x%02x)\n",
           port->bulk_in_endpointAddress, port->bulk_out_endpointAddress);

  return 0;
}

/*
 * Windows sprdvcom.inf has UsbEnableZLP=1.  URB_ZERO_PACKET means the USB core
 * appends a ZLP only when the submitted transfer is an exact multiple of the
 * endpoint max packet size.  Normal AT commands (a few bytes) are unchanged.
 */
static int sprd_vcom_port_probe(struct usb_serial_port *port) {
  unsigned int i;

  for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i) {
    if (port->write_urbs[i])
      port->write_urbs[i]->transfer_flags |= URB_ZERO_PACKET;
  }

  return 0;
}

static int sprd_vcom_open(struct tty_struct *tty,
                          struct usb_serial_port *port) {
  struct usb_serial *serial = port->serial;
  int ret;

  /*
   * This step is required on Linux for 1782:000c.  The device's endpoint
   * GET_STATUS behavior is unusual (the halt bit can remain reported), but
   * usb_clear_halt() was experimentally proven to make the pipes usable.
   */
  ret = sprd_vcom_clear_pipes(port);
  if (ret)
    return ret;

  /*
   * Exact Windows sprdvcom sequence captured while opening SPRD AT:
   *
   *   21 22 0000 0002 0000
   *   21 22 0201 0002 0000
   *
   * Re-running the prepare request on every tty open makes repeated
   * periodic open/query/close cycles deterministic.
   */
  ret = sprd_vcom_control(serial, SPRD_PREPARE_VALUE);
  if (ret)
    return ret;

  msleep(50);

  ret = sprd_vcom_control(serial, SPRD_OPEN_VALUE);
  if (ret)
    return ret;

  msleep(100);

  /* Start the standard usb-serial receive URBs. */
  ret = usb_serial_generic_open(tty, port);
  if (ret)
    dev_err(&port->dev, "generic open failed: %d\n", ret);

  return ret;
}

static void sprd_vcom_close(struct usb_serial_port *port) {
  /*
   * Do not send the normal CDC DTR/RTS request (0003/0000).  The Windows
   * driver uses its own SPRD port activation semantics.  Stopping generic
   * RX/TX URBs is sufficient; the next open fully reinitializes the port.
   */
  usb_serial_generic_close(port);
}

static struct usb_serial_driver sprd_vcom_device = {
    .driver =
        {
            .name = "sprd_vcom",
        },
    .description = DRIVER_DESC,
    .id_table = sprd_vcom_id_table,

    .num_ports = 1,
    .num_bulk_in = 1,
    .num_bulk_out = 1,

    /* Windows INF uses a 4 KiB BulkInBufSize. */
    .bulk_in_size = 4096,

    .attach = sprd_vcom_attach,
    .port_probe = sprd_vcom_port_probe,
    .open = sprd_vcom_open,
    .close = sprd_vcom_close,

    /*
     * Unspecified tty operations automatically use the usb-serial generic
     * implementations, including normal bulk read/write buffering.
     */
};

static struct usb_serial_driver *const sprd_vcom_drivers[] = {&sprd_vcom_device,
                                                              NULL};

module_usb_serial_driver(sprd_vcom_drivers, sprd_vcom_id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Reverse-engineered from UNISOC sprdvcom.sys / USBPcap");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
