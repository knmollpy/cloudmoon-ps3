# CloudMoon PS4 — native port

This is a separate PS4 application, not a reskin of Moonlight. The PS3 tree and its build remain untouched.

## Target console

- Firmware target: **13.52**.
- Homebrew runtime: GoldHEN-compatible PKG, built with OpenOrbis 0.5.4.
- Initial stream profile: 1280×720 at 60 FPS, H.264 video and Opus audio.
- PS4-specific I/O: DualShock 4 (scePad), VideoOut (sceVideoOut) and audio output (sceAudioOut).

A successful cross-compile does not prove the application boots on a physical PS4. The eventual PKG must be installed and tested on firmware 13.52.

## Product interface — original CloudMoon UI

The frontend in ps4/src/ui/cloudmoon_ui.c is independent of Moonlight's APPS/SETTINGS menu. It has its own CloudMoon dashboard, navigation, paginated library, account-linking view and settings layout. Moonlight-PS4 is used only as a low-level platform reference for Orbis input/video APIs and packaging; its screen design and Sunshine/GameStream workflow are not reused.

The dashboard is model-driven:
- It displays games provided by the account/catalog layer, not a fabricated local list.
- The library pages through up to 64 entries instead of silently hiding titles after the first eight.
- The linking view draws a QR only when a real QR matrix and link payload are supplied. It does not generate a decorative or non-working QR.
- An unlinked account is shown as unlinked; it is not silently treated as authenticated.
- DualShock navigation is separated from rendering so the frontend can be host-tested.

## CloudMoon protocol and account data

The PS4 build now includes the portable CloudMoon signalling modules:
- Socket.IO / Engine.IO packet processing and client registration: cloudmoon_protocol.c, cloudmoon_session.c, cloudmoon_client.c.
- Coordinator discovery and Android instance ID decoding: cloudmoon_discovery.c.
- WebSocket transport: cloudmoon_ws.c and cloudmoon_ws_tls.c.
- PS4 TLS: Mbed TLS 3.6.5 with required certificate-chain and hostname verification, TLS 1.2, and an explicit Orbis entropy source. The backend is cross-compiled, but network operation has not yet been tested on a console.
- Session import: the PS4 parser accepts only HTTPS links from web.cloudmoonapp.com with token, email, game, userid and android_instance_id fields. Duplicate fields, fragments masquerading as query fields and malformed coordinator data are rejected.
- Session-to-client bridge: a validated session creates a 1280×720 CloudMoon client registration and starts the coordinator WebSocket loop from a worker thread.

CloudMoon's public material documents the browser session fields and coordinator signalling flow, but no general-purpose official QR device-pairing endpoint has been verified. Therefore:
- The console does not ask for the user's Google password or CloudMoon password.
- NVIDIA/GeForce NOW OAuth and Sunshine pairing are not substituted.
- The QR flow and phone-to-console hand-off are still pending. The frontend will not display a fake QR.

## Platform port plan

1. [x] Independent CloudMoon dashboard and navigation.
2. [x] Orbis DualShock 4 input and VideoOut framebuffer adapters, cross-compiled with OpenOrbis.
3. [x] Portable CloudMoon protocol/client, WebSocket and Mbed TLS backend, cross-compiled for Orbis.
4. [ ] Verify coordinator TLS/WebSocket registration on the physical 13.52 console.
5. [ ] Port libdatachannel WebRTC callbacks, ICE/DTLS/SRTP and DataChannel input.
6. [ ] Add H.264 decode and present streaming frames through VideoOut.
7. [ ] Add Opus output and full DualShock 4 touch/button mapping.
8. [ ] Implement the real phone/session hand-off and dynamic catalog retrieval.
9. [ ] Produce an installable PKG and validate it on firmware 13.52.

PS3 platform code is not copied blindly. Threading, TLS entropy, controller, filesystem, renderer, audio and package metadata each need a PS4 implementation. UI, session/account, signalling, WebRTC, decoder, audio and rendering remain separate modules so the final app does not depend on Moonlight's menu or launcher.

## Build and verification

Host-side tests build and run with:

\`\`\`sh
cmake -S ps4 -B build/cloudmoon-ps4-ui
cmake --build build/cloudmoon-ps4-ui
ctest --test-dir build/cloudmoon-ps4-ui --output-on-failure
\`\`\`

GitHub Actions runs UI, protocol and session smoke tests, and a separate OpenOrbis cross-build. The Orbis workflow exports all static libraries built for the PS4 target.

## Current status

- [x] Separate ps4-port branch; PS3 source/build remains intact.
- [x] Original CloudMoon dashboard, paginated catalog UI and controller navigation.
- [x] DualShock 4 and VideoOut PS4 platform modules.
- [x] CloudMoon protocol, session parser, Socket.IO client and WebSocket transport.
- [x] Mbed TLS backend and secure random source cross-compile for Orbis.
- [x] Host UI/protocol/session tests pass.
- [ ] Physical PS4 network and rendering test.
- [ ] WebRTC, media decode, audio, QR/session pairing and full catalog integration.
- [ ] Final PKG and FW 13.52 installation test.

**No installable PKG is claimed yet.** The current verified output is a set of cross-compiled static libraries, not a playable application.
