# Spooler Controller

## Introduction

This software is for controlling the cable driven parallel robot I built for my house. It uses DARTT over UDP to communicate with the actuators, which are using ESP32 bridges to provide networked communication to each of the actuators.

## Comms Structure

The software uses UDP sockets to communicate with the actuators. This communication model should work well for distributed DARTT actuators - this software serves as a proof of concept for this architecture. 

## Building

### Prerequisites (all platforms)

Clone with submodules:

```bash
git clone --recurse-submodules <repo-url>
cd spooler-controller
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

### Linux

Install dependencies:

```bash
sudo apt install build-essential cmake libsdl2-dev libgl-dev
```

Build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Run:

```bash
./build/spooler_controller
```

### Windows

Uses the vendored SDL2 SDK in `external/SDL/`. Open in Visual Studio or build with CMake:

```
cmake -B build
cmake --build build --config Release
```

### Android

Building for Android requires some one-time setup. You don't need Android Studio installed, but you do need the SDK and NDK.

#### 1. Install Android command-line tools

Download the "Command line tools only" package from https://developer.android.com/studio#command-line-tools-only

Create a directory for the Android SDK and unpack the tools into it:

```bash
mkdir -p ~/android-sdk/cmdline-tools
unzip commandlinetools-linux-*.zip -d ~/android-sdk/cmdline-tools
mv ~/android-sdk/cmdline-tools/cmdline-tools ~/android-sdk/cmdline-tools/latest
```

Add to your `~/.bashrc` or `~/.zshrc`:

```bash
export ANDROID_HOME=~/android-sdk
export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools
```

Then reload your shell (`source ~/.bashrc`).

#### 2. Install SDK packages

```bash
sdkmanager --install "platform-tools" "platforms;android-34" "ndk;26.1.10909125" "build-tools;34.0.0" "cmake;3.22.1"
```

Accept the licenses when prompted:

```bash
sdkmanager --licenses
```

#### 3. Create local.properties

The Gradle build needs to know where your SDK lives. Create `android/local.properties`:

```bash
echo "sdk.dir=$ANDROID_HOME" > android/local.properties
```

This file is gitignored since the path is machine-specific.

#### 4. Build the APK

Create the gradlew script if not present using:

```bash
cd android
gradle wrapper
```

Then from within the android directory, build using:

```bash
./gradlew assembleDebug
```

The first build will take a while because Gradle downloads its own dependencies and compiles SDL2 + the entire project from source for each target architecture (arm64, x86_64).

The APK will be at:

```
android/app/build/outputs/apk/debug/app-debug.apk
```

#### 5. Install on a device

Connect your phone via USB with USB debugging enabled (Settings > Developer options > USB debugging), then:

```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```

To see logs from the app:

```bash
adb logcat -s SDL/APP:V
```

#### Troubleshooting

- **"SDK location not found"**: You forgot step 3. Create `android/local.properties`.
- **NDK not found**: Make sure the NDK version installed by `sdkmanager` matches what Gradle expects. Run `sdkmanager --list --installed` to check.
- **Build fails with `linux/if_packet.h` error**: The CMake Android block should define `TCS_MISSING_AF_PACKET` automatically. If not, clean and rebuild: `./gradlew clean assembleDebug`
- **App crashes on launch**: Check `adb logcat` for the backtrace. Make sure your device supports OpenGL ES 3.0 (virtually all devices from 2015+).