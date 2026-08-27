# O27 Controller — Web App

**UI Version 1.2** · Matched firmware: **FW v1.8** · 2026-08-26 · AC1803000 · Arduino Nano ESP32 · BLE

The supported phone/Chrome OS client is the installable PWA in this folder.
There is no build step: `index.html` contains the UI and BLE client.

## BLE API

Must match `firmware/o27_controller/include/config.h`:

| Characteristic | Properties | Payload |
|---|---|---|
| Throttle | Write | 1 binary byte, 0-100 |
| Horn | Write | 1 binary byte: nonzero = held, 0 = released |
| Direction | Write | any payload; each write requests one reverse pulse |
| Status | Read + Notify | JSON `{"throttle":0-100,"mode":"local"|"remote"}` |

## PWA update behavior

v1.2 uses network-first navigation and registers the service worker with
`updateViaCache: 'none'`. This fixes the earlier failure mode where an installed
PWA could continue displaying an old GitHub Pages copy after deployment.

## Browser support

Use Chrome or Edge with Web Bluetooth support (Android, Chrome OS, or desktop).
Safari/iOS does not provide the required Web Bluetooth API; Firefox does not
enable it by default.
