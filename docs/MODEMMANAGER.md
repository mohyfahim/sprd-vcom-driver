# ModemManager integration

Manual access is the default because ModemManager and a terminal program must
not control the same AT port concurrently.

Install or switch to the opt-in profile:

```sh
sudo ./install-dkms.sh --mode=modemmanager
```

Reconnect the modem, then inspect:

```sh
udevadm info /dev/ttyUSB0
mmcli -L
mmcli -m 0
```

The rule sets `ID_MM_DEVICE_PROCESS=1` and
`ID_MM_PORT_TYPE_AT_PRIMARY=1` only for verified AT interfaces. ModemManager's
generic AT support should provide discovery and standard control operations
implemented by the firmware.

To return to exclusive manual debugging:

```sh
sudo ./install-dkms.sh --mode=manual
```

The DWR-910M exposes data through a separate RNDIS function. Tagging the AT
port does not define the vendor-specific commands needed to create a managed
RNDIS bearer, so NetworkManager connection activation is not promised.
