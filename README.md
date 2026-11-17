# CloudMoon PS3

Native PlayStation 3 HEN client for connecting directly to CloudMoon. No PC, phone, or local bridge is part of the intended architecture.

> **Status: experimental / not yet a working streaming client.** The current code is protocol groundwork. It has not been compiled with the PS3 toolchain or tested on PS3 hardware, and it does not yet establish a complete CloudMoon session.

## Current groundwork (phase 5)

- Engine.IO v3 packet parsing and ping/pong handling.
- Socket.IO event parsing and session state handling.
- CloudMoon registration JSON builders.
- RFC 6455 WebSocket frame encoding/decoding.
- WebSocket HTTP Upgrade request construction and response validation.
- Cryptographic random-byte abstraction and PS3 RNG adapter using `sysGetRandomNumber`.
- Initial PolarSSL TLS 1.2 transport adapter with certificate and hostname verification intended to fail closed.
- DualShock input mapping to the observed CloudMoon touch-input format.
- Host-side C99 tests for the portable protocol code.

## Important limitations

- PolarSSL adapter API compatibility still needs verification against the exact PSL1GHT/PolarSSL versions used by the PS3-Moonlight toolchain.
- TLS has not yet been integrated with the WebSocket Upgrade flow.
- CloudMoon authentication and coordinator discovery are not fully implemented.
- PS3 does not provide a browser-style WebRTC API. A direct client still needs working ICE, DTLS, SRTP, RTP, SCTP/DataChannel support (or a compatible transport implementation), plus video/audio decode integration.
- No PKG, successful PS3 build, or real CloudMoon connection is claimed.

## Layout

```
src/        Portable protocol/session code and PS3 transport adapters
tests/      Host-side C tests
docs/       Implementation notes and research
```

## Host tests

Run:

```sh
make -f Makefile.host-test clean
make -f Makefile.host-test test
```

The host tests do not compile the PS3-specific PolarSSL adapter.

## Upstream and research

- [PS3-Moonlight](https://github.com/Cruslan/PS3-Moonlight)
- [CloudMoon client teardown](https://maddoxschmidlkofer.com/blog/cloudmoon-client-teardown)

## Development rule

Do not weaken TLS verification, invent authentication credentials, or claim hardware compatibility without testing. Keep the direct PS3-to-CloudMoon architecture; do not introduce a PC/phone bridge.
