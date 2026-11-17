# CloudMoon WebRTC implementation

## Stack

The browser client receives an SDP offer, sends an SDP answer, exchanges trickled ICE candidates, and uses a reliable ordered DataChannel named `config`. The native implementation uses libdatachannel's C API for ICE, DTLS, SRTP, SCTP and media depacketization.

## PS3 media path

- H.264 RTP track → libdatachannel H.264 depacketizer → Annex-B access unit → existing cellVdec submit path.
- Opus RTP track → libdatachannel Opus depacketizer → existing PS3 Opus multistream decoder configured as one coupled stereo stream.
- DualShock 3 input polling thread → CloudMoon touch messages on DataChannel.

## Direct session bootstrap

`cloudmoon.ini` stores the active browser session's game package, user ID and Base64 `android_instance_id`. The coordinator URL is decoded from `coorUrl`; WebSocket signaling is then opened directly from the PS3.

## Build

`make cloudmoon-pkg` builds Mbed TLS and libdatachannel with the PPU cross toolchain, injects the CloudMoon runtime into the PS3-Moonlight shell, and creates the PKG. This build route is new and must be validated by the CI cross-build; no runtime success is claimed until tested on PS3 HEN.
