# Building WebRTCsays.ai

## Setup Environment

```bash
# Add depot_tools to your PATH
export PATH=~/depot_tools:$PATH

# Configure and sync [gclient](https://x.com/i/grok?text=gclient)
# .gclient in original folder should be like this
# solutions = [
#  {
#    "name": "src",
#    "url": "https://github.com/wilddolphin2022/WebRTCsays.ai",
#    "deps_file": "DEPS",
#    "managed": False,
#    "custom_deps": {},
#  },
#]
#target_os = ["ios", "mac", "linux"]

gclient config https://github.com/wilddolphin2022/webrtcsays.ai.git
gclient sync

# Build Scripts

# Navigate to the source directory. Original directory can be cleaned up leave "src"
# Yes, I know, WebRTC can be obtuse. 
cd src

# Refresh pull link
git pull https://github.com/wilddolphin2022/WebRTCsays.ai main

# Make build scripts executable and run them
chmod +x ./build-whisper.sh
./build-whisper.sh # Options: -d for debug, -r for release, -c to clean

# For WebRTCsays.ai project, by default, we use "speech" enabled audio.
# Set to false to disable in file webrtc.gni
rtc_use_speech_audio_devices = true

# macOS Deployment Target

# Check and set macOS deployment target for compatibility with Whisper and LLaMA
# which demand macOS 14.0 minimum
grep mac_deployment_target src/build/config/mac/mac_sdk.gni

# Update deployment target if necessary
perl -i -pe's/mac_deployment_target = "11.0"/mac_deployment_target = "14.0"/g' build/config/mac/mac_sdk.gni
# For Mac Mx machines
perl -i -pe's/mac_deployment_target = "11.0"/mac_deployment_target = "15.0"/g' build/config/mac/mac_sdk.gni

# Audio Device Module

# Modify audio device module for macOS if not yet
perl -i -pe's/Master/Main/g' modules/audio_device/mac/audio_device_mac.cc

# Generate WebRTC example "direct"
gn gen out/debug --args="is_debug=true rtc_include_opus = true rtc_build_examples = true"

# Debug build
ninja -C out/debug direct

# Release build
gn gen out/release --args="is_debug=false rtc_include_opus = true rtc_build_examples = true"
ninja -C out/release direct

## Testing

# Help with options
./out/debug/direct --help

# Run direct communication test
./out/debug/direct --mode=callee 127.0.0.1:3456 --encryption
./out/debug/direct --mode=caller 127.0.0.1:3456 --encryption

# Whisper Test
./out/debug/direct --mode=callee 127.0.0.1:3456 --whisper --encryption
./out/debug/direct --mode=caller 127.0.0.1:3456 --whisper --encryption


```
