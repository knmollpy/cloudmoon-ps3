# CloudMoon PS4 — native port

This is a separate PS4 application, not a reskin of Moonlight. The PS3 tree and its build remain untouched.

## Target console

- Firmware target: **13.52**.
- Homebrew runtime: GoldHEN-compatible PKG, built with OpenOrbis.
- First stream profile: 1280×720 at 60 FPS, H.264 video and Opus audio.
- PS4-specific input/output: DualShock 4 (scePad), video output (sceVideoOut) and audio output (sceAudioOut).

GoldHEN support for 13.52 is documented, but the public Moonlight-PS4 reference has only been validated on firmware 9.00. A successful compile is not evidence that this build boots on 13.52; the final PKG must be tested on the target console.

## Product interface — original CloudMoon UI

The new frontend lives in ps4/src/ui/cloudmoon_ui.c and is independent of Moonlight's APPS/SETTINGS menu. It has its own CloudMoon dashboard, navigation, library, account-linking page and settings layout. The only reason to consult Moonlight-PS4 is for low-level PS4 examples (video, audio, controller and packaging), not to inherit its screen design or its Sunshine/GameStream workflow.

The dashboard is model-driven:
- The library receives game names from the CloudMoon account/catalog layer; it does not ship a fake list.
- The linking view renders a QR only when a real QR matrix and link payload are supplied.
- With no valid account session, it clearly shows the unlinked state instead of pretending to be logged in.
- DualShock navigation is abstracted into button flags so UI tests can run on a normal host.

The standalone UI component and smoke test can be built with CMake:

\`\`\`sh
cmake -S ps4 -B build/cloudmoon-ps4-ui
cmake --build build/cloudmoon-ps4-ui
ctest --test-dir build/cloudmoon-ps4-ui --output-on-failure
\`\`\`

GitHub Actions also runs the host UI smoke test on changes to ps4/**.

## CloudMoon protocol and account data

The portable signalling code to adapt is in the existing ../src tree:
- Socket.IO / Engine.IO packet handling and client registration: cloudmoon_protocol.c, cloudmoon_session.c, cloudmoon_client.c.
- Coordinator discovery and Android instance ID parsing: cloudmoon_discovery.c.
- TLS WebSocket transport: cloudmoon_ws.c, cloudmoon_ws_tls.c.
- WebRTC negotiation and media callbacks: cloudmoon_webrtc.c.

CloudMoon's web client uses the session values token, email, game, userid and android_instance_id; the coordinator URL is derived from the encoded Android instance data. The signalling flow then registers game, userId, slotId and instanceId, before exchanging WebRTC offer/candidate messages. The public material found so far documents that client protocol, but not a general-purpose official QR device-pairing endpoint. Therefore:
- Do not put the user's Google password or CloudMoon password into a homemade console form.
- Do not substitute NVIDIA/GeForce NOW OAuth or Sunshine pairing.
- A QR must carry a valid, short-lived link/session created by a real linking flow. The UI intentionally refuses to fabricate one.

## Platform port plan

1. Keep the CloudMoon dashboard and navigation independent from the streaming engine.
2. Port POSIX/TLS/WebSocket coordinator transport to Orbis and verify registration.
3. Port libdatachannel/WebRTC callbacks, ICE/DTLS/SRTP and DataChannel input.
4. Add H.264 decode and sceVideoOut presentation, beginning with software decode at 720p60.
5. Add Opus output and DualShock 4 touch/button mapping.
6. Implement a secure local QR/session hand-off once the session acquisition path is verified.
7. Produce and validate an installable PKG specifically on 13.52 before calling the app playable.

PS3 platform code must not be copied blindly: thread APIs, TLS entropy, pad, filesystem paths, video/audio calls and package metadata all need a PS4 implementation. Decoder, renderer, account/session, signalling and frontend remain separate modules so a failure in one does not force a Moonlight menu or a full rewrite of the other parts.

## Current status

- [x] Separate ps4-port branch; the PS3 implementation is unchanged.
- [x] Independent CloudMoon PS4 dashboard module and controller navigation model.
- [x] Host-side UI smoke test and GitHub Actions workflow.
- [ ] Bind the frontend to PS4 VideoOut + scePad.
- [ ] CloudMoon session hand-off and account/catalog retrieval.
- [ ] Native CloudMoon WebRTC/audio/video pipeline.
- [ ] Firmware 13.52 PKG build and console validation.

No installable PS4 PKG is claimed by this stage. The current commits establish and test the independent interface layer; the streaming and account layers still have to be ported.
