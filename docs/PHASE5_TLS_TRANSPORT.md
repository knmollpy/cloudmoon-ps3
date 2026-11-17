# Phase 5 — direct TLS transport (implementation notes)

The local development workspace contains an initial PSL1GHT/PolarSSL TCP/TLS 1.2 adapter with IPv4 DNS, socket timeouts, SNI, hostname verification, mandatory CA bundle, PS3 system RNG, TLS read/write and cleanup.

## Security

No `SSL_VERIFY_NONE` fallback is allowed. Missing or invalid CA data, hostname mismatch, or certificate-chain failure must abort the connection. No credentials or private keys are committed.

## Validation status

The adapter is not yet verified against the exact PS3 SDK / PolarSSL headers and libraries. It has not been compiled into a PS3 ELF or PKG and has not been run on hardware. Confirm the signatures and availability of the hostname, CA-chain, verify-result, TLS-version and socket callback APIs before relying on it.

## Next

Integrate verified TLS with HTTP WebSocket Upgrade, preserve any post-header bytes received in the same TLS read, then implement CloudMoon coordinator discovery and authentication. WebRTC ICE, DTLS, SRTP, RTP, SCTP/DataChannel and media decode remain separate major tasks.
