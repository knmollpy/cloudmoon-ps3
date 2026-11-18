#include "cloudmoon_protocol.h"
#include "cloudmoon_session.h"
#include "cloudmoon_discovery.h"
#include "cloudmoon_ws.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char packet[4096];
    char host[256];
    char path[512];
    char decoded[256];
    uint16_t port = 0;
    size_t decoded_len = 0;
    cm_sio_event_t event;
    cm_session_t session;
    cm_ws_frame_info_t frame_info;
    uint8_t frame[128], payload[128], key[4] = {0x12,0x34,0x56,0x78};
    const char *event_packet = "42[\"hello_server\",{}]";
    const char *base64 = "eyJ4IjoxfQ==";
    int n;

    assert(cm_eio_packet_type("2probe") == CM_EIO_PING);
    assert(cm_eio_packet_type("4payload") == CM_EIO_MESSAGE);
    assert(cm_eio_make_pong(packet, sizeof(packet)) == 1);
    assert(strcmp(packet, "3") == 0);

    n = cm_socketio_make_client_register(packet, sizeof(packet), 1280, 720,
                                         "com.roblox.client", "user-42", "instance-abc");
    assert(n > 0);
    assert(strstr(packet, "\"screenRes\":\"1280x720\"") != NULL);
    assert(strstr(packet, "\"slotId\":\"user-42@com.roblox.client\"") != NULL);

    assert(cm_socketio_parse_event(event_packet, &event) == 0);
    assert(event.type == CM_SIO_EVENT_HELLO_SERVER);
    cm_session_init(&session);
    session.state = CM_SESSION_SOCKET_CONNECTED;
    assert(cm_session_on_event(&session, &event) == CM_SESSION_ACTION_SEND_CLIENT_REGISTER);
    assert(session.state == CM_SESSION_CLIENT_REGISTER_SENT);

    assert(cm_cloudmoon_decode_instance_id("eyJ4IjoxfQ%3D%3D",
                                           decoded, sizeof(decoded)) == 16);
    assert(strcmp(decoded, base64) == 0);
    assert(cm_base64_decode(decoded, (uint8_t *)packet, sizeof(packet),
                            &decoded_len) == 0);
    assert(decoded_len == 7);
    assert(memcmp(packet, "{\"x\":1}", 7) == 0);

    assert(cm_cloudmoon_parse_https_url("https://coord.example.net:8443/socket",
                                         host, sizeof(host), &port,
                                         path, sizeof(path)) == 0);
    assert(strcmp(host, "coord.example.net") == 0);
    assert(port == 8443);
    assert(strcmp(path, "/socket") == 0);
    assert(cm_cloudmoon_parse_https_url("http://not-tls.example",
                                         host, sizeof(host), &port,
                                         path, sizeof(path)) != 0);

    n = cm_ws_encode_client_frame(frame, sizeof(frame), CM_WS_TEXT, 1,
                                  (const uint8_t *)"hello", 5, key);
    assert(n > 0);
    assert(cm_ws_decode_server_frame(frame, (size_t)n, &frame_info,
                                     payload, sizeof(payload)) == n);
    assert(frame_info.opcode == CM_WS_TEXT);
    assert(frame_info.payload_len == 5);
    assert(memcmp(payload, "hello", 5) == 0);

    puts("CloudMoon protocol smoke tests passed");
    return 0;
}
