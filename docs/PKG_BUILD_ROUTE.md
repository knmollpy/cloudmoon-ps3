# CloudMoon PS3 package build

The direct CloudMoon package target is:

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make cloudmoon-pkg
```

The target initializes PSL1GHT, downloads the Mozilla CA bundle, cross-builds Mbed TLS and libdatachannel for PPU, injects the CloudMoon signaling/WebRTC runtime and creates the PS3 PKG.

## Runtime configuration

On first launch the app creates `/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon.ini`. Fill these values from an active CloudMoon browser session URL:

- `game`: game package name
- `user_id`: `userid` query value
- `android_instance_id`: full Base64 `android_instance_id` query value

The PKG carries `cacert.pem` for coordinator TLS verification.

## Media integration

The WebRTC adapter negotiates the server offer and local answer, exchanges trickled ICE candidates, creates the reliable `config` DataChannel, forwards DualShock 3 touch events, depacketizes H.264 and Opus, and routes them into the existing PS3 cellVdec/audio paths.

## Validation boundary

A successful PPU link and PKG creation do not prove CloudMoon service compatibility. The remaining validation is an actual active-session run on PS3 HEN. This repository does not claim that a live CloudMoon session has already been tested.
