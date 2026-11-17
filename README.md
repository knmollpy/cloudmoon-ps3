# CloudMoon PS3

Cliente nativo experimental para PlayStation 3 con HEN. La arquitectura objetivo es PS3 conectada directamente a CloudMoon, sin PC, teléfono ni servidor puente.

> **Estado:** transporte TCP/TLS + WebSocket, descubrimiento de coordinator y flujo inicial de señalización en desarrollo; todavía no es un cliente de streaming funcional.

## Incluido

- Parser Engine.IO v3 y eventos Socket.IO.
- Decodificación de `android_instance_id` Base64 y extracción de `coorUrl` HTTPS.
- Bucle de señalización: Engine.IO open/ping, Socket.IO connect, `hello_server`, `client_register`, `action_done2`, `register`, `msg` y `getout`.
- Frames WebSocket RFC 6455 y HTTP Upgrade.
- Interfaz RNG y adaptador PS3 `sysGetRandomNumber`.
- Traducción inicial de DualShock 3 a eventos touch.
- Adaptador preliminar PolarSSL TLS 1.2 con CA y hostname verification obligatorios.
- Transporte TLS/WebSocket que conserva datos recibidos junto con las cabeceras HTTP.
- Pruebas host C99 con transporte TLS simulado y parser de coordinator.

## Pruebas host

```sh
make host-test
```

Las pruebas no prueban TLS real, red PS3 ni CloudMoon.

## Compilar el prototipo PS3

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make prototype-pkg
```

El objetivo prepara el SDK, copia los módulos CloudMoon al árbol PS3 y los compila junto con el ejecutable base.

**Importante:** el PKG resultante todavía ejecuta la aplicación Moonlight upstream para Sunshine/GameStream. Los módulos CloudMoon quedan enlazados, pero aún no están conectados al UI ni al protocolo de vídeo/audio. No es un cliente CloudMoon funcional.

## Pendiente para streaming CloudMoon

- Confirmar autenticación y obtención real de `android_instance_id`.
- Completar el intercambio real con coordinator y eventos de sesión.
- Implementar o adaptar ICE, DTLS, SRTP, RTP y SCTP/DataChannel.
- Conectar vídeo H.264 y audio Opus al decodificador de hardware de PS3.
- Integrar la sesión con el UI y DualShock 3.
- Probar conexión, vídeo y audio en PS3 HEN real.

La PS3 no ofrece una API WebRTC de navegador. WebSocket solo resuelve señalización; no transporta por sí mismo el vídeo del juego.
