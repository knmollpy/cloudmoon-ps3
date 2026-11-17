# CloudMoon PS3

Cliente nativo experimental para PS3 HEN, con conexión directa al coordinator de CloudMoon. No utiliza PC, móvil ni servidor puente.

## Implementación incluida

- Descubrimiento del coordinator desde `android_instance_id` (Base64 JSON).
- Socket.IO 2.4 / Engine.IO v3 por WebSocket TLS.
- Registro `client_register` y unión a sala con `signalingRoomId`.
- Motor WebRTC integrado mediante libdatachannel: ICE/STUN, DTLS, SRTP, SCTP/DataChannel y negociación SDP.
- DataChannel fiable `config`, envío de resolución y controles.
- DualShock 3 traducido a eventos touch CloudMoon.
- H.264 depacketizado hacia el decodificador hardware cellVdec.
- Opus estéreo 48 kHz hacia el reproductor de audio PS3.
- CA bundle incluido en el PKG para validar TLS.

## Compilar el PKG CloudMoon

```sh
git clone --recurse-submodules https://github.com/knmollpy/cloudmoon-ps3.git
cd cloudmoon-ps3
make cloudmoon-pkg
```

El objetivo cruza Mbed TLS y libdatachannel para PPU/PSL1GHT y luego genera el PKG. La primera compilación puede tardar por las dependencias.

## Configuración de sesión

El primer arranque crea:

`/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon.ini`

Editar con FTP/gestor de archivos y reiniciar:

```ini
game=PACKAGE_NAME
user_id=CLOUDMOON_USER_ID
android_instance_id=BASE64_ANDROID_INSTANCE_ID
```

Los valores se copian de la URL de la sesión activa de CloudMoon:
- `game`: paquete del juego.
- `user_id`: parámetro `userid`.
- `android_instance_id`: parámetro completo `android_instance_id`, sin decodificar.

No compartir ese archivo: identifica una sesión privada.

## Estado y límites

La ruta de conexión y los puentes a vídeo/audio/input ya están escritos. La compatibilidad real de libdatachannel y sus dependencias con PSL1GHT/Cell debe confirmarse en la compilación cruzada y después en hardware PS3 HEN. No se declara funcional hasta verificar una sesión CloudMoon real con imagen, sonido y controles.
