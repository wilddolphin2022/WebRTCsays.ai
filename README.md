
Here's the prettified content saved as README.md:

markdown
# Building WebRTCsays.ai

## Setup Environment

```bash
# Add [depot_tools](https://x.com/i/grok?text=depot_tools) to your PATH
export PATH=~/depot_tools:$PATH

# Configure and sync [gclient](https://x.com/i/grok?text=gclient)
gclient config https://github.com/wilddolphin2022/webrtcsays.ai.git
gclient sync

Build Scripts
bash
# Make build scripts executable and run them
chmod +x scripts/build.sh
./scripts/build.sh
./scripts/build-whisper.sh # Options: -d for debug, -r for release, -c to clean

# Navigate to the source directory
cd src

Configuration Checks
Speech Audio Devices
markdown
Ensure the following flag is set in `webrtc.gni`:

```gn
# For WebRTCsays.ai project, by default, we use "speech" enabled audio.
# Set to false to disable.
rtc_use_speech_audio_devices = true

macOS Deployment Target
bash
# Check and set macOS deployment target for compatibility with Whisper and LLaMA
# which demand macOS 14.0 minimum
grep mac_deployment_target src/build/config/mac/mac_sdk.gni

# Update deployment target if necessary
perl -i -pe's/mac_deployment_target = "11.0"/mac_deployment_target = "14.0"/g' build/config/mac/mac_sdk.gni

Audio Device Module
bash
# Modify audio device module for macOS
perl -i -pe's/Master/Main/g' modules/audio_device/mac/audio_device_mac.cc

Compile
Debug Build
bash
gn gen out/debug --args="is_debug=true rtc_include_opus = true rtc_build_examples = true"
ninja -C out/debug direct

Release Build
bash
gn gen out/release --args="is_debug=false rtc_include_opus = true rtc_build_examples = true"
ninja -C out/release direct

Testing
Non-Whisper Test
bash
# Run direct communication test
./out/debug/direct callee 192.168.8.179:4455
./out/debug/direct caller 192.168.8.179:4455

Whisper Test
Run the Whisper test by invoking the appropriate command or script here. 
```