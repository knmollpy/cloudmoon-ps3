#!/bin/sh
# Apply POSIX-to-PSL1GHT compatibility patches to pinned WebRTC dependencies.
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
JUICE="$ROOT_DIR/vendor/libdatachannel/deps/libjuice/src/tcp.c"
UDP="$ROOT_DIR/vendor/libdatachannel/deps/libjuice/src/udp.c"
SCTP="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_bsd_addr.c"

for file in "$JUICE" "$UDP" "$SCTP"; do
  [ -f "$file" ] || { echo "WebRTC source not found: $file" >&2; exit 1; }
done

# PSL1GHT exposes FNONBIO, and does not implement MSG_NOSIGNAL.
sed -i 's/ioctlsocket(sock, FIONBIO, \&nbio)/ioctlsocket(sock, FNONBIO, \&nbio)/g' "$JUICE" "$UDP"
sed -i 's/int flags = MSG_NOSIGNAL;/int flags = 0;/g' "$JUICE"

# PS3 networking is IPv4-only here. Avoid compiling unsupported IPv6 socket options.
sed -i '/setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,/d' "$UDP"
sed -i 's/hints.ai_family = AF_UNSPEC;/hints.ai_family = AF_INET;/' "$UDP"
sed -i 's/const int families\[2\] = {AF_INET6, AF_INET};.*/const int families[1] = {AF_INET};/' "$UDP"
sed -i 's/const char \*names\[2\] = {"IPv6", "IPv4"};/const char *names[1] = {"IPv4"};/' "$UDP"
sed -i 's/for (int i = 0; i < 2; ++i)/for (int i = 0; i < 1; ++i)/' "$UDP"

# Replace unsupported SIOCGIFCONF enumeration with a route-based IPv4 probe.
python3 - "$UDP" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
marker = "#if !defined(_WIN32) && defined(NO_IFADDRS)"
start = s.find(marker)
end = s.find("\nint udp_get_addrs(", start)
if start < 0 or end < 0:
    raise SystemExit("Could not locate libjuice NO_IFADDRS address-discovery section")
helper = r'''#if !defined(_WIN32) && defined(NO_IFADDRS)
static int get_local_default_inet4(uint16_t port, struct sockaddr_in *result) {
    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(9);
    if (inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr) != 1)
        return -1;
    socket_t probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET)
        return -1;
    int ret = -1;
    if (connect(probe, (const struct sockaddr *)&remote, sizeof(remote)) == 0) {
        socklen_t len = sizeof(*result);
        if (getsockname(probe, (struct sockaddr *)result, &len) == 0 &&
            len == sizeof(*result) && result->sin_addr.s_addr != htonl(INADDR_ANY)) {
            result->sin_port = htons(port);
            ret = 0;
        }
    }
    closesocket(probe);
    return ret;
}
#endif
'''
s = s[:start] + helper + s[end:]
needle = "#else // NO_IFADDRS defined"
a = s.find(needle)
b = s.find("\n#endif\n#endif", a)
if a < 0 or b < 0:
    raise SystemExit("Could not locate libjuice NO_IFADDRS ioctl branch")
replacement = r'''#else // NO_IFADDRS defined
    if (bound.addr.ss_family == AF_INET) {
        struct sockaddr_in sin;
        if (get_local_default_inet4(port, &sin) == 0 &&
            !addr_is_local((const struct sockaddr *)&sin)) {
            ++ret;
            if (current != end) {
                memcpy(&current->addr, &sin, sizeof(sin));
                current->len = sizeof(sin);
                current->socktype = SOCK_DGRAM;
                ++current;
            }
        }
    }'''
s = s[:a] + replacement + s[b:]
p.write_text(s)
PY

# PSL1GHT pthread_t is a structure; use usrsctp's existing started flag.
if grep -q 'if (sctp_it_ctl.thread_proc)' "$SCTP"; then
  sed -i 's/if (sctp_it_ctl.thread_proc)/if (SCTP_BASE_VAR(iterator_thread_started))/' "$SCTP"
fi

grep -q 'FNONBIO' "$JUICE"
grep -q 'FNONBIO' "$UDP"
grep -q 'get_local_default_inet4' "$UDP"
grep -q 'SCTP_BASE_VAR(iterator_thread_started)' "$SCTP"
echo "Applied PS3 libjuice/usrsctp compatibility patches."
