# 6GGW Router — Android APK build sources

This folder is a complete **Capacitor** project that wraps the 6GGW app into a native Android app.
The whole 6GGW app is bundled inside (`www/`, copied into `android/app/src/main/assets/public`), so
the APK runs **fully offline** — it is a real self-contained app, not a "open a website" wrapper.

- App name: **6GGW Router**
- App ID: **com.ai2orbit.ggw6** (change in `capacitor.config.json` if you want your own)
- min SDK 22 (Android 5.1+), target SDK 34

I already built a working **debug APK** from exactly these sources and sent it alongside this — you
can install that on any phone right now to test. To rebuild it, or to make a signed **release** APK
for Play, follow the steps below.

## What you need

- **Node 18+** and **npm** (to run Capacitor).
- **Android SDK** — easiest via **Android Studio**. Set `local.properties` in `android/` to your SDK
  path, e.g. `sdk.dir=/Users/you/Library/Android/sdk` (or `ANDROID_HOME`).
- **JDK 17+** (Android Studio bundles one).

## Rebuild the debug APK (what I did)

```bash
cd ggw6-app
npm install
npx cap sync android              # copies the latest www/ into the android project
cd android
./gradlew assembleDebug
# -> app/build/outputs/apk/debug/app-debug.apk
```

If you regenerate the native project from scratch, it's just `npx cap add android` first (the
`android/` folder here is already generated, so you can skip that).

## Open it in Android Studio instead

```bash
cd ggw6-app
npm install
npx cap open android              # opens the android/ project in Android Studio
```

Then **Build → Build Bundle(s) / APK(s) → Build APK(s)**.

## Signed release APK / AAB for Play

1. Create a keystore (one time):
   ```bash
   keytool -genkey -v -keystore 6ggw.keystore -alias 6ggw -keyalg RSA -keysize 2048 -validity 10000
   ```
2. In Android Studio: **Build → Generate Signed Bundle / APK**, pick your keystore, choose
   **release**. For Play, produce an **.aab**; for direct install, an **.apk**.
   (CLI equivalent: `./gradlew assembleRelease` after wiring the signing config in
   `android/app/build.gradle`.)
3. Upload the `.aab` to the **Play Console** (one-time \$25 developer account — that part needs your
   own Google account; I can't sign or upload under your account).

## Updating the app later

The web app is the source of truth. When `index.html` / `sw.js` change, just:

```bash
# copy the new files into www/, then:
npx cap sync android
cd android && ./gradlew assembleDebug
```

That's it — no native code to touch.
