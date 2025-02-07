# WebRTC now speaks AI language
![alt text](webrtcsaysai.jpg "Logo")

## Building WebRTCsays.ai

### Setup Environment and get code
```bash
# Add depot_tools to your PATH
export PATH=~/depot_tools:$PATH

# Configure and sync gclient
# .gclient in original folder should be like this
 solutions = [
  {
    "name": "src",
    "url": "https://github.com/wilddolphin2022/webrtcsays.ai",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
  },
]
target_os = ["ios", "mac", "linux"]
# eof .gclient

gclient config https://github.com/wilddolphin2022/webrtcsays.ai.git
gclient sync

# Navigate to the source directory. Original directory can be cleaned up leave "src"
# Yes, I know, WebRTC can be obtuse. 
cd src

# Refresh pull link
git pull https://github.com/wilddolphin2022/WebRTCsays.ai main

```
### Build Scripts
```bash

# Make build scripts executable and run them to get dependencies built
chmod +x ./build-whisper.sh
./build-whisper.sh # Options: -d for debug, -r for release, -c to clean

# For WebRTCsays.ai project, by default, we use "speech" enabled audio.
# Set to false to disable in file webrtc.gni
rtc_use_speech_audio_devices = true
```
### Build macOS Deployment Target
```bash

# Check and set macOS deployment target for compatibility with Whisper and LLaMA
# which demand macOS 14.0 minimum
grep mac_deployment_target src/build/config/mac/mac_sdk.gni

# Update deployment target if necessary
perl -i -pe's/mac_deployment_target = "11.0"/mac_deployment_target = "14.0"/g' build/config/mac/mac_sdk.gni
# For Mac Mx machines
perl -i -pe's/mac_deployment_target = "11.0"/mac_deployment_target = "15.0"/g' build/config/mac/mac_sdk.gni

# Modify audio device module for macOS if not yet
perl -i -pe's/Master/Main/g' modules/audio_device/mac/audio_device_mac.cc

# Remove obsolete Mac desktop capture code in webrtc.gni in case deployment target is more than 14.0
# Find
rtc_desktop_capture_supported =
    (is_win && current_os != "winuwp") || is_mac ||
    ((is_linux || is_chromeos) && (rtc_use_x11_extensions || rtc_use_pipewire))
# and remove "is_mac ||"

```
### Build Linux 
```bash

# Here will be notes specific to Linux build

```
### Generate and build "direct" application 
```bash

# Generate WebRTC example "direct"
gn gen out/debug --args="is_debug=true rtc_include_opus = true rtc_build_examples = true"

# Debug build
ninja -C out/debug direct

# Release build
gn gen out/release --args="is_debug=false rtc_include_opus = true rtc_build_examples = true"
ninja -C out/release direct

```
### Testing direct peer to peer application
```bash

# Make self-signed cert.pem and key.pem used for encryption option
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -sha256 -days 3650 -nodes -subj "/C=XX/ST=StateName/L=CityName/O=CompanyName/OU=CompanySectionName/CN=CommonNameOrHostname"

# Help with options
./out/debug/direct --help

./out/debug/direct --mode=callee 127.0.0.1:3456 --encryption --webrtc_cert_path=cert.pem --webrtc_key_path=key.pem
./out/debug/direct --mode=caller 127.0.0.1:3456 --encryption --webrtc_cert_path=cert.pem --webrtc_key_path=key.pem

# Run direct with whisper
./out/debug/direct --mode=callee 127.0.0.1:3456 --whisper --encryption
./out/debug/direct --mode=caller 127.0.0.1:3456 --whisper --encryption

```
