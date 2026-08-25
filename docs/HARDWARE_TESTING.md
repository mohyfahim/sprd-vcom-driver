# Hardware acceptance testing

Record the kernel, distribution, module commit, modem firmware, and USB
topology with every result.

## Binding safety

1. Reconnect the modem and inspect `lsusb -t`.
2. Confirm only the verified AT interface uses `sprd_vcom`.
3. Confirm RNDIS remains bound to `rndis_host`.
4. Confirm diagnostic and download interfaces are not claimed.

## Reliability

- Run `sprd-at-tty AT ATI 'AT+CSQ'` successfully.
- Complete 100 open/query/close cycles without errors or leaked URBs.
- Complete 20 unplug/replug cycles.
- Unplug during a pending read; the client must fail cleanly and the kernel
  must not warn or crash.
- Exercise suspend/resume, module unload/reload, and a kernel reboot.
- Review `dmesg` for warnings, lockdep reports, stalls, or USB errors.

## Device naming and integration

- Confirm `/dev/sprd-at` and exactly one `/dev/sprd/at-*` link for one modem.
- With multiple modems, confirm unique links and that automatic client
  selection refuses ambiguity.
- In manual mode, confirm `ID_MM_PORT_IGNORE=1` with `udevadm info`.
- In ModemManager mode, confirm the primary-AT tags, `mmcli -L` discovery,
  identity, signal, registration, and supported SIM/SMS operations.

Managed RNDIS bearer creation is not a release acceptance criterion.
