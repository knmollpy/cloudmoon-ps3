# CloudMoon PS3

Cliente nativo experimental para PlayStation 3 con HEN. La arquitectura objetivo es PS3 conectada directamente a CloudMoon, sin PC, teléfono ni servidor puente.

> **Estado:** transporte TCP/TLS + WebSocket, descubrimiento del coordinator y señalización inicial en desarrollo. Todavía no es un cliente de streaming funcional.

## Incluido

- Decodificación Base64 de `android_instance_id` y extracción de `coorUrl` HTTPS.
- Parser Engine.IO v3 y Socket.IO.
- Flujo `hello_server` → `client_register` y `action_done2.signalingRoomId` → `register`.
- Lectura de `msg` y envío de respuestas/candidatos mediante `msg`.
- Interpretación de `getout.errorCode`.
- WebSocket RFC 6455 sobre TLS PolarSSL, validación de certificados y preservación de datos posteriores al Upgrade.
- Adaptador inicial de DualShock 3 a eventos touch.
- Pruebas host C99, incluida simulación de TLS/WebSocket.

## Pruebas host

```sh
make host-test
```

## Compilar el prototipo PS3

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make prototype-pkg
```

El objetivo compila los módulos con PSL1GHT y genera un PKG basado en el ejecutable de PS3-Moonlight.

**Importante:** el paquete actual sigue ejecutando la interfaz y el protocolo Moonlight/Sunshine. Los módulos CloudMoon están enlazados, pero todavía falta conectar WebRTC y los medios al ejecutable. No es un cliente CloudMoon funcional.

## Bloqueos restantes

- Obtener los parámetros reales de autenticación y sesión de CloudMoon.
- Implementar ICE, DTLS, SRTP, RTP y SCTP/DataChannel.
- Conectar el vídeo H.264 y audio Opus al decodificador de hardware de PS3.
- Integrar la sesión en el UI y verificar todo en una PS3 HEN real.
