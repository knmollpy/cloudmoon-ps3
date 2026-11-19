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
- The library supports up to 512 catalog entries and displays eight per page.
- The linking view draws a QR only when a real QR matrix and link payload are supplied. It does not generate a decorative or non-working QR.
- An unlinked account is shown as unlinked; it is not silently treated as authenticated.
- DualShock navigation is separated from rendering so the frontend can be host-tested.

## CloudMoon protocol and account data

The PS4 build now includes the portable CloudMoon signalling modules:
- Socket.IO / Engine.IO packet processing and client registration: cloudmoon_protocol.c, cloudmoon_session.c, cloudmoon_client.c.
- Coordinator discovery and Android instance ID decoding: cloudmoon_discovery.c.
- WebSocket transport: cloudmoon_ws.c and cloudmoon_ws_tls.c.
- PS4 TLS: Mbed TLS 3.6.5 with required certificate-chain and hostname verification, TLS 1.2, and an explicit Orbis entropy source. The backend is cross-compiled, but network operation has not yet been tested on a console.
- Account authentication: the console generates a one-time local QR page and a reusable phone bookmarklet. The user signs in with Google on the official `web.cloudmoonapp.com` origin, where CloudMoon stores its own `userData` bearer token in browser localStorage. The bookmarklet reads only that CloudMoon token and user ID, asks for the PS4 pairing URL, encrypts the payload with the ephemeral AES-CBC/HMAC key carried in the URL fragment, and submits it to the local console using a browser form POST. The PS4 validates the bearer token against `/user/info` over verified TLS and keeps the token in memory; no Google password or separate CloudMoon password is requested.
- Remote phone session: after account authorization, the PS4 calls CloudMoon `/phone/list` to get the account's cloud-phone ID and `/phone/connect` for the selected game and region, then decodes the returned `android_instance_id` to derive the coordinator Socket.IO endpoint.
- Session-to-client bridge: the authenticated account and newly-created game instance supply the coordinator data used to register a 1280×720 CloudMoon client and start the coordinator WebSocket loop from a worker thread.
- The catalogue fetcher calls the CloudMoon game-list API and the parser keeps active mobile packages. The source integration is present; authenticated/personalized filtering and real-console API behaviour still need verification.

The QR is a local-network hand-off; it does not use an undocumented CloudMoon cloud pairing endpoint:
- Google authentication is performed by Google's own Identity Services button. The PS4 never asks for or stores a Google password.
- A separate CloudMoon email/password option is available for accounts that have a CloudMoon password configured; credentials are not retained after the official HTTPS login request.
- The local hand-off is protected by a one-time nonce and an ephemeral AES-CBC/HMAC key. The phone-to-console page is still served over the local network; active-LAN threat testing remains pending.
- The current CloudMoon Google OAuth client may restrict allowed browser origins. The Google button must be tested from the local PS4 pairing page; if Google rejects the local origin, CloudMoon needs to authorize this origin or provide a device-code flow. Google auth on a physical phone/PS4 has not yet been verified.

## Platform port plan

1. [x] Independent CloudMoon dashboard and navigation.
2. [x] Orbis DualShock 4 input and VideoOut framebuffer adapters, cross-compiled with OpenOrbis.
3. [x] Portable CloudMoon protocol/client, WebSocket and Mbed TLS backend are in the PS4 build graph; successful Orbis compilation and console TLS verification remain pending.
4. [ ] Verify coordinator TLS/WebSocket registration on the physical 13.52 console.
5. [x] Implement the libdatachannel WebRTC callbacks, ICE/DTLS/SRTP and DataChannel input path; end-to-end streaming validation remains pending.
6. [x] Implement the H.264 decoder and VideoOut presentation path; hardware validation remains pending.
7. [x] Implement Opus output and DualShock 4 input mapping; hardware latency and control validation remain pending.
8. [x] Implement local QR/session hand-off and CloudMoon game-list API retrieval; live phone/console verification remains pending.
9. [ ] Complete the OpenOrbis application build, create the PKG and test installation on firmware 13.52.

PS3 platform code is not copied blindly. Threading, TLS entropy, controller, filesystem, renderer, audio and package metadata each need a PS4 implementation. UI, session/account, signalling, WebRTC, decoder, audio and rendering remain separate modules so the final app does not depend on Moonlight's menu or launcher.

## Build and verification

Host-side tests build and run with:

\`\`\`sh
cmake -S ps4 -B build/cloudmoon-ps4-ui
cmake --build build/cloudmoon-ps4-ui
ctest --test-dir build/cloudmoon-ps4-ui --output-on-failure
\`\`\`

GitHub Actions runs host UI, protocol, QR, catalogue and session smoke tests, plus an OpenOrbis application build that links the app and creates a PKG. The Orbis cross-build previously stopped in libjuice because UDP sockets used the Windows-only FIONBIO call; the PS4 patch now uses fcntl/O_NONBLOCK for Orbis. The current build is being used to verify that fix before a PKG is claimed.

## Current status

- [x] Separate ps4-port branch; PS3 source/build remains intact.
- [x] Original CloudMoon dashboard, paginated catalog UI and controller navigation.
- [x] DualShock 4 and VideoOut PS4 platform modules.
- [x] CloudMoon protocol, session parser, Socket.IO client and WebSocket transport are integrated in source.
- [x] Mbed TLS backend, QR sid hand-off, game-list API, WebRTC callbacks, H.264 decode and Opus output are wired into the PS4 application source.
- [x] Host UI/protocol/QR/catalogue/session smoke tests pass.
- [ ] Successful full Orbis application link and PKG generation.
- [ ] Physical PS4 network, QR pairing, rendering, audio and controller test.
- [ ] End-to-end WebRTC game launch and firmware 13.52 installation test.

**No successful PS4 PKG is claimed yet.** Source-level integration and host tests do not prove that the Orbis app links, installs or streams correctly on a real console.
