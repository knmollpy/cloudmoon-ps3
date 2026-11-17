# Build route and current boundary

## Fastest PS3 cross-compile route

The repository pins [PS3-Moonlight](https://github.com/Cruslan/PS3-Moonlight) as a Git submodule. The `prototype-pkg` target copies the CloudMoon modules into that build tree, appends them to its PPU source list, and invokes the upstream PSL1GHT package pipeline.

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make host-test
make prototype-pkg
```

## CloudMoon progress

- Base64 decode of `android_instance_id` and extraction of HTTPS `coorUrl`.
- Coordinator URL parsing (hostname, port and Socket.IO path).
- Engine.IO open/ping and Socket.IO connect handling.
- Client registration after `hello_server`.
- Room registration after `action_done2`.
- Dispatch of signaling payloads from `msg` and shutdown on `getout`.
- WebSocket framing, HTTP Upgrade validation and TLS integration.
- Initial PolarSSL TLS 1.2 adapter with mandatory CA-chain and hostname verification.
- Host-side tests for protocol handling, coordinator parsing and a simulated TLS/WebSocket transport.

The prototype PKG still launches the upstream Moonlight/Sunshine app. CloudMoon modules are compiled into the executable but are not yet connected to its UI or media pipeline. Host tests do not validate TLS handshakes, DNS, certificates, or PS3 sockets.

## Remaining blockers

1. Confirm authentication and obtain the real instance/session parameters from CloudMoon.
2. Implement or port ICE, DTLS, SRTP, RTP and SCTP/DataChannel to PS3.
3. Bridge decoded H.264 and Opus into the upstream PS3 media callbacks.
4. Connect the CloudMoon session to the native UI and DualShock 3.
5. Verify a complete session on real PS3 HEN hardware.

A WebSocket connection only handles signaling. It does not provide the media stream.
