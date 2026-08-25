# Changelog

## 1.0.0 - Unreleased

- Refactor the driver around verified per-device quirk profiles.
- Harden interface and endpoint validation and reject unprofiled dynamic IDs.
- Add unique device links while retaining `/dev/sprd-at` compatibility.
- Add explicit manual and ModemManager udev profiles.
- Harden and test the AT client.
- Unify DKMS/manual packaging and remove forced boot loading.
- Add contribution, security, testing, CI, and upstreaming documentation.
