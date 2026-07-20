# 6GGW Router — native iOS & Android app (App Store / Play Store ready)

This folder turns the 6GGW web app into a **real native app** you can put up for sale.
It uses [Capacitor](https://capacitorjs.com): the whole 6GGW app is bundled **inside** the
native app (in `www/`), so it is a genuine self-contained app that runs **fully offline** —
not a thin "open a website" wrapper (Apple rejects those; a bundled app passes review).

- App name: **6GGW Router**
- Bundle / App ID: **com.ai2orbit.ggw6**  (change to your own if you prefer)
- One project builds **both** iOS (.ipa for the App Store) and Android (.aab/.apk for Play).

---

## What you need (only you can do these — they need YOUR accounts)

**For iOS / App Store:**
1. A **Mac** with **Xcode** (free, from the Mac App Store).
2. An **Apple Developer Program** account — **$99/year** at developer.apple.com. Required to sell on the App Store.
3. Cocoapods: `sudo gem install cocoapods` (one time).

**For Android / Play Store:**
1. **Android Studio** (any OS).
2. A **Google Play Developer** account — **one-time $25**.

I cannot log into Apple or Google for you or sign the build — signing must be done under
your own developer account. Everything up to that point is done for you here.

---

## Build the iOS app (on a Mac)

```bash
cd ggw6-app
npm install                 # installs Capacitor
npx cap add ios             # creates the native Xcode project
npx cap sync                # copies the 6GGW app into it
npx cap open ios            # opens the project in Xcode
```

In Xcode:
1. Select the **App** target → **Signing & Capabilities** → pick your **Team** (your Apple Developer account).
2. Set the **Display Name** to `6GGW Router` and the **Bundle Identifier** to your own (e.g. `com.yourname.ggw6`).
3. Drop in the app icon (a 1024×1024 PNG — `www/icon-512.png` upscaled, or send me your logo and I'll cut the full icon set).
4. **Product → Archive**, then **Distribute App → App Store Connect** to upload.
5. On **App Store Connect** (appstoreconnect.apple.com): create the app listing, set the **price**, add screenshots, and submit for review.

## Build the Android app

```bash
npx cap add android
npx cap sync
npx cap open android        # opens Android Studio
# Build → Generate Signed Bundle/APK → upload the .aab to Play Console
```

---

## Updating the app later

Whenever I ship a new 6GGW version, you just replace the files in `www/`
(or I send you the updated `www/`), then:

```bash
npx cap sync
```

…and re-archive. No rewrite needed — the native shell stays the same.

---

## Zero-code alternative (no Mac needed to start)

If you don't have a Mac yet, https://www.pwabuilder.com can generate an iOS package
straight from the live URL:

    https://anirudhatalmale6-alt.github.io/netswitch-switch-benchmark-view/

Paste that, choose iOS, download the package — but you will **still** need the Apple
Developer account + a Mac (or a Mac cloud build service) to sign and upload. There is no
way around Apple's signing requirement for selling on the App Store.

---

*6GGW Router — AI2ORBIT Co. Built on the NetSwitch engine.*
