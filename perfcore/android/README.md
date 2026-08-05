# perfcore on Android (NDK)

The native engine (`../ggw_perfcore.cpp`) runs on Android via JNI. One `.cpp` file is
the whole engine; `perfcore_jni.cpp` includes it with `PERFCORE_NO_MAIN` and exposes a
small JNI surface. The identical engine file is added to the iOS Xcode target as
Objective-C++ — so both platforms run one core and report comparable numbers.

## Files
- `perfcore_jni.cpp` — JNI bridge (Java_com_ai2orbit_perfcore_PerfCore_*)
- `CMakeLists.txt`    — NDK build (externalNativeBuild)
- `PerfCore.kt`       — Kotlin surface (`System.loadLibrary("perfcore")`)

## Verified
Cross-compiled with NDK r27 clang for both ABIs, valid Android ELF, JNI symbols exported:
- `libperfcore.so` arm64-v8a  (ELF 64-bit ARM aarch64)
- `libperfcore.so` armeabi-v7a (ELF 32-bit ARM)

## Drop into an app
1. Copy the `perfcore/` folder under `app/src/main/cpp/`.
2. In `app/build.gradle`:
   ```
   android {
     defaultConfig { externalNativeBuild { cmake { cppFlags "-std=c++17 -O2" } } }
     externalNativeBuild { cmake { path "src/main/cpp/perfcore/android/CMakeLists.txt" } }
   }
   ```
3. Put `PerfCore.kt` in the app and call it (see its header).

Manual ABI build (what CI/Studio does under the hood):
```
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang++ \
    -std=c++17 -O2 -fPIC -shared -o libperfcore.so perfcore_jni.cpp
```
