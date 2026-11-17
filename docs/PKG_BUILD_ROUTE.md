# Build route and current boundary

## Fastest path to a bootable PS3 package

The repository pins the upstream [PS3-Moonlight](https://github.com/Cruslan/PS3-Moonlight) application as a Git submodule at `vendor/PS3-Moonlight`. It contains the established PSL1GHT build pipeline, PS3 UI, hardware H.264 decoder, Opus audio path, and package-generation rules.

On a Linux x86_64 build host with Git, Make, curl, and a working network:

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make prepare
make baseline-pkg
```

The submodule's `make prepare` downloads its pinned PSL1GHT toolchain. The resulting baseline package is **Moonlight for Sunshine/GameStream**, not a CloudMoon client. This route is useful for checking that the console build environment and package installation pipeline work before changing the application.

## CloudMoon integration status

The project's Engine.IO / Socket.IO / WebSocket protocol code and a preliminary TLS adapter are separate from the upstream streaming application. They are not yet wired into its UI or media pipeline. CloudMoon's signaling transport is not itself a video transport: a complete direct client still needs compatible ICE, DTLS, SRTP, RTP, SCTP/DataChannel and media handling.

The preliminary `cloudmoon_tls_polarssl.c` has not been compiled against the pinned SDK's exact PolarSSL headers. Its API names and signatures must be checked before enabling it. It is deliberately not included in the baseline package build.

## Do not confuse milestones

1. Host protocol tests pass — portable C code only.
2. Baseline Moonlight PKG builds — proves PSL1GHT packaging, not CloudMoon support.
3. CloudMoon handshake succeeds — proves only signaling/authentication.
4. Video and audio stream decode on real PS3 HEN — required for a functional CloudMoon client.

No step is claimed complete unless its own test has passed.
