# Phase 4 — TLS transport research

The PS3-Moonlight project is the implementation reference for PSL1GHT sockets and PolarSSL. The CloudMoon client must not copy a TLS configuration that disables certificate verification for a locally paired host: CloudMoon's public coordinator requires normal CA-chain and hostname validation.

The project introduces an injectable random source and a PS3 adapter based on `sysGetRandomNumber`. It fails closed if the system RNG fails; it does not fall back to a time-seeded PRNG or `rand()`.

The WebSocket `Sec-WebSocket-Key` is derived from 16 random bytes. Host-side tests cover the deterministic Base64 vector and RNG failure.

## Remaining work

Verify the exact PolarSSL/mbedTLS version in the PS3 toolchain; implement TLS with SNI, hostname verification, trusted CA roots, timeout/cancellation, WANT_READ/WANT_WRITE handling, and integrate the WebSocket Upgrade while preserving bytes received after the HTTP headers.
