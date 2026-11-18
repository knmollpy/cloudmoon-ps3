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

## Vinculación de una sesión desde el celular (QR local)

La aplicación de PS3 genera su propio QR de un solo uso; no es un QR oficial de inicio de sesión de CloudMoon. El celular y la PS3 deben estar conectados a la misma red local.

1. En la PS3, elegí **LINK CLOUDMOON SESSION FROM PHONE**.
2. Escaneá el QR con el celular. Se abre la página de vinculación alojada directamente en la PS3.
3. En esa página, abrí CloudMoon oficial en una segunda pestaña, iniciá sesión con el método que CloudMoon ofrezca y pulsá **Jugar** en el juego elegido.
4. Copiá la URL completa de la sesión activa, volvé a la pestaña de vinculación, pegala y pulsá **Validar sesión y vincular con esta PS3**.
5. La PS3 valida los campos y guarda `cloudmoon.ini`. Volvé al menú y elegí **CONNECT TO CLOUDMOON**.

El proceso vincula una **sesión de juego activa**, no guarda un inicio de sesión permanente de CloudMoon. Para cambiar de juego, iniciá el nuevo juego en CloudMoon y repetí la vinculación. La exploración del catálogo y la selección del juego siguen ocurriendo en la página oficial del celular; el catálogo nativo de PS3 todavía no está implementado.

La página local no solicita tu contraseña ni códigos de Google, y no envía la URL a servicios de terceros. La transferencia entre el celular y la PS3 usa HTTP dentro de la red local (no TLS): usá una Wi-Fi privada de confianza y no compartas el QR, la URL ni el archivo de sesión. El QR caduca a los 10 minutos, al cancelar la pantalla o al completar la vinculación.

### Alternativa por FTP

También podés abrir `docs/cloudmoon-mobile-link.html` fuera del modo QR, pegar la URL completa y descargar `cloudmoon.ini`. Transferilo por FTP a:

`/dev_hdd0/game/MNLT00001/USRDIR/cloudmoon.ini`

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
