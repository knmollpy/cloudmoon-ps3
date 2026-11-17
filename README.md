# CloudMoon PS3

Cliente nativo experimental para PlayStation 3 con HEN. La arquitectura objetivo es PS3 conectada directamente a CloudMoon, sin PC, teléfono ni servidor puente.

> **Estado:** transporte TCP/TLS + WebSocket en desarrollo; todavía no es un cliente de streaming funcional. No se ha compilado con PSL1GHT ni probado en una PS3.

## Incluido

- Parser Engine.IO v3 y eventos Socket.IO.
- Construcción de registros `client_register` y `register`.
- Máquina de estados de señalización.
- Frames WebSocket RFC 6455.
- HTTP WebSocket Upgrade con validación SHA-1/Base64 de `Sec-WebSocket-Accept`.
- Interfaz RNG y adaptador PS3 `sysGetRandomNumber`.
- Traducción inicial de DualShock 3 a eventos touch.
- Adaptador preliminar PolarSSL TLS 1.2 con CA y hostname verification obligatorios.
- Transporte TLS/WebSocket que preserva datos recibidos junto con las cabeceras HTTP.
- Pruebas host C99, incluidas pruebas simuladas de Upgrade y recepción del primer frame.

## Pruebas host

```sh
make -f Makefile.host-test clean test
```

Las pruebas usan un transporte TLS simulado para verificar el WebSocket; no prueban PolarSSL real, red de PS3 ni CloudMoon.

## Crear un PKG base de PS3

Este repositorio fija [PS3-Moonlight](https://github.com/Cruslan/PS3-Moonlight) como submódulo para reutilizar su toolchain PSL1GHT y pipeline de empaquetado:

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make prepare
make baseline-pkg
```

**Importante:** `baseline-pkg` construye el cliente upstream Moonlight para Sunshine/GameStream. No es todavía el cliente CloudMoon. El código CloudMoon todavía no está conectado al UI y al pipeline de medios del ejecutable PS3.

## Pendiente para streaming CloudMoon

- Compilar y verificar el adaptador PolarSSL con el SDK exacto.
- Integrar autenticación y descubrimiento del coordinator CloudMoon.
- Implementar o adaptar ICE, DTLS, SRTP, RTP, SCTP/DataChannel.
- Conectar vídeo H.264 y audio Opus al decodificador de hardware de PS3.
- Integrar DualShock 3 y la gestión de sesión con el UI.
- Probar el PKG en PS3 HEN real.

La PS3 no ofrece una API WebRTC de navegador. WebSocket solo resuelve señalización; no transporta por sí mismo el vídeo del juego.

## Seguridad

No incluir tokens, claves privadas ni credenciales. No desactivar la verificación TLS. No llamar funcional a un PKG hasta verificar conexión, vídeo y audio en hardware.
