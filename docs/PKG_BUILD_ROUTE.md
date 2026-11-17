# Build route and current boundary

## Fastest bootable PS3 package route

The repository pins [PS3-Moonlight](https://github.com/Cruslan/PS3-Moonlight) as a Git submodule at `vendor/PS3-Moonlight`. It supplies a known PSL1GHT package pipeline and the PS3 hardware H.264 / Opus playback path.

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make prepare
make baseline-pkg
```

This generates the upstream Moonlight/Sunshine baseline package. It is a build and hardware-validation milestone, **not** a CloudMoon streaming client.

## CloudMoon transport progress

The code now has:
- Engine.IO v3 / Socket.IO parsing and event-state groundwork.
- WebSocket RFC 6455 framing and HTTP Upgrade validation.
- An initial PolarSSL TLS 1.2 adapter that requires a CA bundle and hostname verification.
- TLS/WebSocket integration with a bounded HTTP-header parser and preservation of bytes that arrive after the Upgrade headers.
- Client frame masking with cryptographic random bytes.
- Host-side simulated tests for Upgrade, first-frame spillover, and client masking.

The PolarSSL adapter remains unverified against the exact PS3 SDK headers and libraries. Host tests use a fake TLS transport and do not validate TLS handshakes, DNS, certificates, or PS3 sockets.

## Remaining blockers for a functional CloudMoon PKG

1. Confirm the exact CloudMoon authentication and coordinator-discovery flow.
2. Complete the actual CloudMoon Socket.IO event exchange.
3. Implement or port ICE, DTLS, SRTP, RTP and SCTP/DataChannel to PS3.
4. Bridge decoded H.264 and Opus into the upstream PS3 media callbacks.
5. Connect the session to the native UI and DualShock 3.
6. Build with PSL1GHT and verify a complete session on real PS3 HEN hardware.

A WebSocket connection only handles signaling. It does not provide the media stream.

## Milestones are kept separate

- Host tests: portable logic and simulated transport only.
- Baseline PKG: proves the upstream PS3 packaging route.
- CloudMoon signaling: proves authentication and session negotiation.
- Real stream: proves H.264 video and Opus audio decode on PS3.
