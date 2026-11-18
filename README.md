# CloudMoon PS3

Cliente nativo experimental para PS3 HEN, con conexión directa al coordinator de CloudMoon. El celular se usa únicamente para iniciar sesión en CloudMoon y exportar una sesión activa; no se necesita un PC ni un servidor puente.

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

## Vinculación desde el celular

CloudMoon publica un catálogo navegable y un botón de inicio de sesión en su web. El método de autenticación exacto lo controla CloudMoon; esta aplicación no solicita ni almacena tu contraseña de Google.

1. Abrí la página oficial de CloudMoon en el navegador del celular e iniciá sesión con el método que allí te ofrezcan.
2. Elegí un juego y pulsá **Jugar**. Esperá a que la sesión del juego se abra.
3. Usá el importador local `docs/cloudmoon-mobile-link.html`: pegá la URL completa de la sesión y descargá `cloudmoon.ini`.
4. Transferí el archivo por FTP a `/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon.ini` en la PS3.
5. En la PS3, seleccioná **CONNECT TO CLOUDMOON**.

El importador procesa la URL completamente dentro del navegador: no la envía a ningún servidor ni guarda la contraseña. La URL y el archivo INI contienen identificadores privados de sesión; no los publiques ni los compartas.

## Configuración manual alternativa

El primer arranque crea:

`/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon.ini`

También se puede completar manualmente:

```ini
game=PACKAGE_NAME
user_id=CLOUDMOON_USER_ID
android_instance_id=BASE64_ANDROID_INSTANCE_ID
```

El cliente también acepta una línea `session_url=` con la URL HTTPS completa de una sesión iniciada desde un subdominio oficial de `cloudmoonapp.com`; la valida localmente y extrae `game`, `userid` y `android_instance_id`.

Los datos requeridos vienen de la URL de una partida activa:
- `game`: paquete del juego.
- `user_id`: parámetro `userid` (o `user_id`).
- `android_instance_id`: identificador Base64 completo.

Una sesión activa identifica un juego concreto; volver a importar cuando se inicie otra partida. No compartas ese archivo: identifica una sesión privada.

## Estado y límites

La ruta de conexión y los puentes a vídeo/audio/input ya están escritos. La compatibilidad real de libdatachannel y sus dependencias con PSL1GHT/Cell debe confirmarse en la compilación cruzada y después en hardware PS3 HEN. No se declara funcional hasta verificar una sesión CloudMoon real con imagen, sonido y controles.
