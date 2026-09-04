# O27 Controller PWA — UI v1.5

Matched firmware: **FW v1.13**  
Project release: **Proto V1.0**

The app is a single-file React-via-CDN Web Bluetooth PWA. There is no build
step.

## UI v1.5
- Entire 0–100 speed dial is visible.
- Major labels: 0, 25, 50, 75, 100.
- Slider display moves continuously while dragging.
- One throttle BLE write is sent on slider release.
- Horn is press-and-hold.
- Reverse is a momentary tap with visible pressed feedback.

## BLE transport
Throttle, Horn, and Reverse have independent characteristics and state. Their
writes pass through one promise queue only because Android/Chrome Web Bluetooth
does not allow overlapping GATT operations.

## PWA updates
Service-worker cache: `o27-v1.5`. Navigation is network-first to reduce stale
GitHub Pages installs after deployment.

## Browser
Use a Web Bluetooth-capable Chromium browser.
