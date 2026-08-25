# Supported devices

| Model | VID:PID | Firmware | AT interface | Endpoints | Profile | Evidence |
| --- | --- | --- | ---: | --- | --- | --- |
| D-Link DWR-910M | `1782:000c` | Original tested firmware | 2 | IN `0x81`, OUT `0x02` | `dwr910m_info` | Windows-driver USB capture and repeated Linux AT operation |

Interfaces 0/1 are RNDIS and interface 3 is diagnostic on the verified
DWR-910M layout. They are intentionally outside this driver's ID table.

This table contains supported products, not a list of possibly related UNISOC
IDs. Download-mode IDs such as `1782:4d00` must never be added.
