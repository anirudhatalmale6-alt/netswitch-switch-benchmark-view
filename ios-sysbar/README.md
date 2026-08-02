# GGW SysBar — iOS

The iOS counterpart to the Android SysBar: RAM / ROM / CPU on an always-visible
bar, small font, over every app. Built the App-Store-legal way for iOS.

## Two honest platform facts (read first)

1. **iOS has no floating overlay over other apps.** Apple's sandbox has no
   equivalent to Android's `SYSTEM_ALERT_WINDOW`. The legitimate always-visible
   surface is a **Live Activity** — it shows on the **Lock Screen** and in the
   **Dynamic Island** regardless of which app is on screen. That is what this app
   uses for the "always-on bar." A free-floating window is not possible on stock
   iOS (only jailbroken devices can, which no App Store build may do).
2. **iOS restricts device-wide CPU%** exactly like Android 8+. The app reads its
   own CPU across its threads and tags it `CPU n%(app)` — honest, nothing faked.
   Device RAM total, app-available RAM, install-time footprint, and ROM total/free
   are all real, public-API values.

## What it does
- **In-app dashboard:** device RAM total, memory available to the app,
  install-time footprint (PSS-equivalent), ROM total/free, this app's CPU%.
- **Always-on bar:** tap *Start bar* → a Live Activity with a small monospaced
  RAM/ROM/CPU line on the Lock Screen + Dynamic Island, refreshed each second
  while the app is foreground.

## Versions
- App target: **iOS 15+** (runs on essentially every supported iPhone).
- Live Activity bar: **iOS 16.1+** (Apple added Live Activities then). On iOS 15
  the in-app dashboard still works; the always-on bar needs 16.1.

## Files
```
ios-sysbar/
  App/SysBarApp.swift        # SwiftUI app + dashboard + start/stop the bar
  Shared/SysStats.swift      # RAM/ROM/CPU readers (public APIs only)
  Shared/SysBarAttributes.swift  # Live Activity data model
  Widget/SysBarWidget.swift  # renders the bar (Lock Screen + Dynamic Island)
  project.yml                # XcodeGen spec -> GGWSysBar.xcodeproj
```

## Build / run
On a Mac with Xcode:
```
cd ios-sysbar
brew install xcodegen
xcodegen generate
open GGWSysBar.xcodeproj      # set your team, Run on your iPhone
```
CI (`.github/workflows/ios.yml`) compiles it on a macOS runner against the
Simulator SDK with signing off, so the source is verified to build.

## Shipping to your phone
Installing on your device needs an Apple signing identity (your Apple ID is
enough for personal install via Xcode). I can cloud-compile and verify the code,
but I can't sign for your device — either you Run it from Xcode (2 taps once your
Apple ID is set as the team), or you provide a signing identity and I produce a
signed build.
