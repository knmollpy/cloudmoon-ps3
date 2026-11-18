# CloudMoon PS4 port

This directory is the native PlayStation 4 port. The existing PS3 implementation remains untouched.

## Target

- OpenOrbis toolchain / GoldHEN homebrew package.
- Baseline output: 1280x720 at 60 FPS, H.264 + Opus.
- Native PS4 input via `scePad`, audio via `sceAudioOut`, presentation via `sceVideoOut`.
- CloudMoon coordinator signaling remains the existing CloudMoon protocol; it is not NVIDIA GeForce NOW OAuth or GameStream pairing.

## Reuse and boundaries

Portable CloudMoon code to adapt from `../src`:
- Socket.IO / Engine.IO packet handling and registration: `cloudmoon_protocol.c`, `cloudmoon_session.c`, `cloudmoon_client.c`.
- Coordinator discovery and instance-id parsing: `cloudmoon_discovery.c`.
- CloudMoon WebSocket signaling: `cloudmoon_ws.c`, `cloudmoon_ws_tls.c`.
- CloudMoon WebRTC negotiation and media callbacks: `cloudmoon_webrtc.c`.

Platform-specific code must be replaced, not copied blindly:
- PS3 thread, pad, TLS entropy, video and audio calls.
- PS3 filesystem paths and package metadata.
- The PS3 Moonlight/Sunshine pairing UI is not part of CloudMoon authentication.

## PS4 platform layer

The first playable milestone uses the PS4 native output path and software H.264 decode at 720p60. Hardware decode is an optimization, not a prerequisite. The decoder and renderer stay behind separate interfaces so a later hardware path cannot destabilize networking or the UI.

- Video: FFmpeg H.264 decode -> PS4 video-out renderer.
- Audio: Opus -> `sceAudioOut`.
- Input: DualShock 4 via `scePad`, translated to CloudMoon touch events.
- Network: POSIX sockets on Orbis, mbedTLS TLS, CloudMoon Socket.IO v2 / Engine.IO v3.
- WebRTC: port the existing CloudMoon libdatachannel adapter to PS4 callbacks and threading.

## Authentication and QR

The QR must link the PS4 to a CloudMoon account/session. The NVIDIA QR flow in third-party GeForce NOW clients is service-specific and cannot authenticate a CloudMoon account. The current PS3 helper only accepts an already active CloudMoon session URL; it is not persistent OAuth. Before implementing a different flow, verify the actual CloudMoon web sign-in/session API and keep passwords and Google credentials off the console.

## Build gates

1. Compile the portable CloudMoon protocol and signaling tests on the host.
2. Cross-compile the PS4 platform layer and produce a diagnostic PKG.
3. Verify network/TLS and CloudMoon coordinator registration on console.
4. Verify WebRTC negotiation, 720p video, audio and DS4 input on console.
5. Add the QR/account flow and catalog UI after the session API is verified.
6. Optimize decode/presentation only after frame-time measurements.

A successful CI build is not proof of a successful PS4 runtime session. Hardware validation is required before calling the client playable.
