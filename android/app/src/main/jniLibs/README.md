# jniLibs (generated, NOT committed)

Prebuilt native payloads packaged into the APK:

- `libSDL3.so` - SDL3 built from the pinned `release-3.4.16` sources
  with the NDK toolchain (same pin as every other platform job).
- `libmemory_game.so` - the game+engine shared lib from our cmake
  android cross build; exports `SDL_main` (SDL activity entry).

Regenerate with `tools/android_apk_libs.sh [NDK_DIR] [SDL_SRC] [JNI_OUT]
The `android-cross` CI job compiles the same sources from scratch on
every push; these blobs are only the packaging input for gradle.

## ABIs
tools/android_apk_libs.sh builds EVERY APK ABI in one pass
(AME_ANDROID_ABIS, default "arm64-v8a armeabi-v7a x86_64"): per-ABI
SDL3 prefix (built from the pinned SDL source when missing) + per-ABI
ame cross build, then copies both .so files into <abi>/ here.
tools/android_prebuilt.sh [ABI] is a single-ABI wrapper around it.
The CI job android-apk runs the same script and uploads the universal
debug APK as an artifact.
