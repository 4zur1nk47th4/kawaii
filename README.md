# Kawaii ESP32-CAM — setup guide

```
ESP32-CAM ──HTTPS POST frames──▶ Vercel (/api/*) + Upstash Redis ◀──poll── your browser
          ◀─── light / camera settings in the reply ───
```

## 1. Deploy the website (Vercel)

Folder: `vercel-site/` (index.html, package.json, api/*.js)

1. Put the folder in a GitHub repo (or run `vercel` from inside it with the Vercel CLI).
2. Import it in Vercel. Framework preset: **Other**. No build command.
3. In the project: **Storage → Marketplace → Upstash Redis** (free plan) → create and connect it to the project.
   This adds `KV_REST_API_URL` and `KV_REST_API_TOKEN` automatically.
4. **Settings → Environment Variables**, add:
   - `DEVICE_KEY` = a long random secret (e.g. 24+ random characters). The camera must use the same value.
   - `ACCESS_KEY` = *(optional)* a password for viewing the page. If you skip it, anyone with the link can see the camera and switch the light.
5. **Settings → Deployment Protection**: turn off *Vercel Authentication* for Production, otherwise the camera gets a login page instead of the API.
6. Redeploy. Use your **production** domain (e.g. `my-cam.vercel.app`).

## 2. Flash the camera

File: `esp32cam_kawaii/esp32cam_kawaii.ino`

- Libraries: **WiFiManager** (tzapu), **ArduinoJson** v7. Board: **AI Thinker ESP32-CAM**, PSRAM **Enabled**, Partition **Huge APP**.
- Upload (GPIO0 to GND while uploading, then remove it and reset).

## 3. First-time setup on the camera

1. Connect your phone/PC to Wi-Fi **tka86**, password **113333555555**.
2. The setup page opens by itself (if not, go to `192.168.4.1`).
3. Choose your Wi-Fi + password, type your website (`my-cam.vercel.app`) and the `DEVICE_KEY`, then Save.
4. The camera reboots, joins your Wi-Fi and the page shows the live feed.

To change Wi-Fi/website later: hold the **BOOT (GPIO0)** button for 3 seconds (it also reopens by itself after 60 s without Wi-Fi).

## Things to know

- **Frame rate:** expect roughly 2–6 fps (QVGA is fastest). Vercel can't hold open video streams, so this is frame-by-frame.
- **Upstash free tier limits:** each streamed frame costs several Redis commands. The camera only uploads while the page is open, but long viewing sessions can use up the free monthly quota. If that happens, upgrade or move the relay to a small always-on server using WebSockets.
- **Security:** the sketch uses `setInsecure()` (encrypted, but doesn't verify the server certificate). Fine for a hobby project; pin the certificate for anything sensitive.
- I removed the Wi-Fi password from the page header — the site is public, so don't publish it.
- Resolution numbers in the old page were wrong for the ESP32 library; the correct values are QVGA = 5, VGA = 8, SVGA = 9.
