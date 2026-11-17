# CloudMoon PS3

Cliente nativo experimental para PlayStation 3 con HEN. La arquitectura objetivo es PS3 conectada directamente a CloudMoon, sin PC, teléfono ni servidor puente.

> **Estado:** base de protocolo en desarrollo; todavía no es un cliente de streaming funcional. No se ha compilado con PSL1GHT ni probado en una PS3.

## Incluido en este repositorio

- Parser Engine.IO v3 y eventos Socket.IO.
- Construcción de mensajes de registro `client_register` y `register`.
- Máquina de estados de señalización.
- Codec de frames WebSocket RFC 6455.
- Construcción y validación de HTTP WebSocket Upgrade, incluyendo SHA-1/Base64 de `Sec-WebSocket-Accept`.
- Interfaz RNG y adaptador PS3 basado en `sysGetRandomNumber`, sin fallback débil.
- Traducción inicial de DualShock 3 a eventos touch de CloudMoon.
- Pruebas host C99 y notas técnicas.

## Compilar pruebas host

```sh
make -f Makefile.host-test clean test
```

Las pruebas host no compilan código específico de PS3 ni demuestran compatibilidad con el hardware.

## Pendiente

- Incorporar y validar el adaptador TLS PolarSSL en este repositorio.
- Integrar TLS con WebSocket Upgrade y conservar bytes que lleguen junto a las cabeceras HTTP.
- Resolver el flujo real de autenticación y descubrimiento del coordinator CloudMoon.
- Implementar transporte de medios: ICE, DTLS, SRTP, RTP, SCTP/DataChannel y decodificación de audio/vídeo en PS3.
- Integrar el bucle de entrada/salida con el cliente PS3.

La PS3 no ofrece una API WebRTC de navegador. El WebSocket de señalización por sí solo no puede recibir el vídeo del juego.

## Referencias

- [PS3-Moonlight](https://github.com/Cruslan/PS3-Moonlight)
- [CloudMoon client teardown](https://maddoxschmidlkofer.com/blog/cloudmoon-client-teardown)

## Seguridad

No incluir tokens, claves privadas ni credenciales. No desactivar la verificación TLS para hacer que una conexión falle menos. No declarar un PKG funcional hasta compilarlo con el SDK correcto y probarlo en hardware.
