# Build route and current boundary

## Fastest PS3 cross-compile route

The repository pins [PS3-Moonlight](https://github.com/Cruslan/PS3-Moonlight) as a Git submodule at `vendor/PS3-Moonlight`. The `prototype-pkg` target copies the CloudMoon modules into that build tree, appends them to its PPU source list, and invokes the upstream PSL1GHT package pipeline.

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make host-test
make prototype-pkg
```

This is intended to prove that the CloudMoon C modules compile with the PS3 PPU toolchain and produce an installable prototype package. The running application still follows the upstream Moonlight/Sunshine flow. The modules are linked but not yet invoked by its UI or streaming session.

## CloudMoon transport progress

- Engine.IO v3 / Socket.IO parsing and event-state groundwork.
- WebSocket RFC 6455 framing and HTTP Upgrade validation.
- Preliminary PolarSSL TLS 1.2 adapter with mandatory CA-chain and hostname verification.
- TLS/WebSocket integration preserving bytes received after the HTTP Upgrade headers.
- Client frame masking with cryptographic random bytes.
- Host-side simulated tests for Upgrade, first-frame spillover, and client masking.

The PolarSSL adapter must be validated by the PPU build against the exact SDK headers/libraries. Host tests use a fake TLS transport and do not validate TLS handshakes, DNS, certificates, or PS3 sockets.

## Remaining blockers

1. Confirm CloudMoon authentication and coordinator discovery.
2. Complete the real CloudMoon Socket.IO event exchange.
3. Implement or port ICE, DTLS, SRTP, RTP and SCTP/DataChannel to PS3.
4. Bridge decoded H.264 and Opus into the upstream PS3 media callbacks.
5. Connect the session to the native UI and DualShock 3.
6. Verify a complete session on real PS3 HEN hardware.

A WebSocket connection only handles signaling. It does not provide the media stream.
