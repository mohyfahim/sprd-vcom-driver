# Adding a device

Device IDs are accepted only from a contributor or tester with the hardware.
Open the device-support issue form and attach sanitized evidence.

## Required evidence

- Manufacturer, exact model, hardware revision, and firmware revision.
- `lsusb` summary and full configuration/interface/endpoint descriptors.
- `lsusb -t` or equivalent driver-binding topology.
- The interface that responds to `AT` and a sanitized `AT` → `OK` transcript.
- Any required control transfers, delays, halt clearing, and ZLP behavior,
  backed by a USB capture or repeatable experiment.
- At least 100 open/query/close cycles, 20 unplug/replug cycles, and a
  suspend/resume test.
- Confirmation that networking remains bound to its original driver and that
  diagnostic/download interfaces remain untouched.

Redact IMEI, ICCID, IMSI, phone numbers, APNs, credentials, SMS content,
subscriber data, and unique serials that you do not want made public.

## Code change

Add a named `sprd_vcom_device_info` profile, including the exact USB ID, when
any layout or activation parameter differs. Add one
`USB_DEVICE_INTERFACE_NUMBER` entry for the exact
AT interface and point `driver_info` at the verified profile. Update the
supported-device table and both ModemManager profiles with the same scoped
VID/PID/interface match.

Never add a vendor-wide match, an unverified product ID, or a BSL/FDL/download
interface. Do not recommend `new_id` as a shortcut.

Run `make check` and complete [HARDWARE_TESTING.md](HARDWARE_TESTING.md).
