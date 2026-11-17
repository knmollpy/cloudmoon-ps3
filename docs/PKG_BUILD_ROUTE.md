# Build route and implementation boundary

## Build

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make host-test
make prototype-pkg
```

The prototype package is built from the PS3-Moonlight base and links the CloudMoon C modules. It is not yet a CloudMoon streaming client.

## Verified protocol observations

The public CloudMoon teardown describes:
- Socket.IO path `/client/socket.io`, WebSocket-only transport.
- `hello_server` followed by `client_register`.
- `action_done2` provides `signalingRoomId`; the client emits `register` with `clientid: "1111111"`.
- `msg` carries WebRTC offer/candidate signaling.
- `getout` carries a JSON string with an `errorCode`; code 10 indicates the web client considers the stream established.
- `android_instance_id` is Base64 JSON containing `coorUrl`.

## Implemented in this repository

- Base64 decoder and HTTPS coordinator URL parser.
- Engine.IO open/ping and Socket.IO connect handling.
- Registration and room-join message builders.
- Signaling message receive callback and outbound `msg` helper.
- `getout.errorCode` handling.
- TLS/WebSocket Upgrade transport with post-header spillover preservation.

## Still required

CloudMoon's browser client relies on native WebRTC. A PS3 implementation still needs ICE, DTLS, SRTP, RTP and SCTP/DataChannel, plus H.264/VP8 video and audio playback integration. The current PKG is a cross-compile prototype using the upstream Moonlight shell; it does not yet stream CloudMoon.
