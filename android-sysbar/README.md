# GGW SysBar — always-on RAM / ROM / CPU top bar (Android)

From your request: *"an app to display memory RAM ROM, CPU% on top bar with small font size. It
displays all the time regardless of any app running on screen. All versions ok. App install-time
RAM estimate as possible."*

## What it does
- A tiny **always-on-top bar** at the top of the screen showing **RAM used/total + %**,
  **ROM (internal storage) used/total**, and **CPU %**, refreshed once a second.
- It stays visible **over every other app** — it's drawn as a system overlay from a foreground
  service, so it doesn't matter what's on screen.
- **Small font** (9sp) with a semi-transparent background so it's readable on any wallpaper.
- The main screen shows an **install-time RAM estimate** — this app's own memory footprint (PSS)
  in MB, which is what it costs in RAM once installed and running.

## Versions
- `minSdk 23` (Android 6) → runs on essentially every phone in use.
- On Android 8+ it uses the modern `TYPE_APPLICATION_OVERLAY`; on 6–7 it falls back to `TYPE_PHONE`.

## How to use (2 taps)
1. Open the app → **Grant "display over other apps"** (Android requires this for any overlay).
2. **Start top bar.** The bar appears at the top and stays there. "Stop top bar" removes it.

## Honest note on CPU %
Android 8+ restricts apps from reading the system-wide `/proc/stat`. The bar tries the system value
first and shows `CPU n%(sys)` when the OS allows it; otherwise it falls back to this app's own CPU
time and shows `CPU n%(app)`. The `(sys)`/`(app)` tag tells you which you're seeing — nothing is
faked. A true device-wide CPU% on locked-down phones needs a privileged/root reader, which I can
add if you want it.

## Getting the APK — built automatically in the cloud
You don't need Android Studio. Every push to `android-sysbar/` triggers a GitHub Actions build that
produces `app-debug.apk` as a downloadable artifact (Actions tab → latest "Build SysBar APK" run →
Artifacts → `ggw-sysbar-debug-apk`). I'll send you the direct link once the build is green.

## Building it yourself (optional)
Open `android-sysbar/` in Android Studio and Run, or from a machine with the Android SDK:
```
cd android-sysbar
gradle assembleDebug        # -> app/build/outputs/apk/debug/app-debug.apk
```

## Structure
```
android-sysbar/
  app/src/main/java/com/ggw/sysbar/
    MainActivity.kt     # grant permission, start/stop, install-RAM estimate
    OverlayService.kt   # the always-on top bar (foreground service + overlay)
    SysStats.kt         # RAM / ROM / CPU readers, no external libraries
  app/src/main/res/     # layout, strings, theme, icon
  app/src/main/AndroidManifest.xml
```
