#!/bin/sh
# Apply POSIX-to-PSL1GHT compatibility patches to pinned WebRTC dependencies.
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
JUICE="$ROOT_DIR/vendor/libdatachannel/deps/libjuice/src/tcp.c"
UDP="$ROOT_DIR/vendor/libdatachannel/deps/libjuice/src/udp.c"
SCTP_IND="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_indata.c"
SCTP_INPUT="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_input.c"
SCTP_OUTPUT="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_output.c"
SCTP_PCB="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_pcb.c"
SCTP_BSD_ADDR="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_bsd_addr.c"
SCTP_OS="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_os_userspace.h"

for file in "$JUICE" "$UDP" "$SCTP_IND" "$SCTP_INPUT" "$SCTP_OUTPUT" "$SCTP_PCB" "$SCTP_BSD_ADDR" "$SCTP_OS"; do
  [ -f "$file" ] || { echo "WebRTC source not found: $file" >&2; exit 1; }
done

# PSL1GHT exposes FNONBIO, and does not implement MSG_NOSIGNAL.
sed -i 's/ioctlsocket(sock, FIONBIO, \&nbio)/ioctlsocket(sock, FNONBIO, \&nbio)/g' "$JUICE" "$UDP"
sed -i 's/int flags = MSG_NOSIGNAL;/int flags = 0;/g' "$JUICE"

# PS3 networking is IPv4-only here. Remove IPv6-only socket-option statements
# together with their guarding if-lines; otherwise an empty if is left behind.
sed -i '/if (ai->ai_family == AF_INET6)/d' "$UDP"
sed -i '/setsockopt(sock, IPPROTO_IPV6,/d' "$UDP"
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

# PSL1GHT's newlib headers omit BSD timeval and ancillary-data macros used by usrsctp.
python3 - "$SCTP_IND" "$SCTP_INPUT" "$SCTP_OUTPUT" "$SCTP_OS" <<'PY'
import pathlib, sys

indata = pathlib.Path(sys.argv[1])
sctp_input = pathlib.Path(sys.argv[2])
sctp_output = pathlib.Path(sys.argv[3])
s = indata.read_text()
needle = "#include <netinet/sctp_os.h>"
compat = r"""
#ifndef timercmp
#define timercmp(a, b, op) \
    (((a)->tv_sec == (b)->tv_sec) ? \
     ((a)->tv_usec op (b)->tv_usec) : \
     ((a)->tv_sec op (b)->tv_sec))
#endif
"""
if "define timercmp(a, b, op)" not in s:
    if needle not in s:
        raise SystemExit("Could not locate sctp_indata.c OS include")
    s = s.replace(needle, needle + "\n" + compat, 1)
    indata.write_text(s)

# sctp_input.c also uses BSD timeval helpers.
s = sctp_input.read_text()
needle = "#include <netinet/sctp_os.h>"
compat = r"""
#ifndef timercmp
#define timercmp(a, b, op) \
    (((a)->tv_sec == (b)->tv_sec) ? \
     ((a)->tv_usec op (b)->tv_usec) : \
     ((a)->tv_sec op (b)->tv_sec))
#endif
#ifndef timersub
#define timersub(a, b, result) do { \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
    if ((result)->tv_usec < 0) { \
        --(result)->tv_sec; \
        (result)->tv_usec += 1000000; \
    } \
} while (0)
#endif
"""
if "define timercmp(a, b, op)" not in s:
    if needle not in s:
        raise SystemExit("Could not locate sctp_input.c OS include")
    s = s.replace(needle, needle + "\n" + compat, 1)
    sctp_input.write_text(s)

# PSL1GHT's IPv4 headers provide struct ip but omit BSD IPVERSION.
s = sctp_output.read_text()
needle = "#include <netinet/sctp_os.h>"
compat = """
#ifndef IPVERSION
#define IPVERSION 4
#endif
"""
if "#define IPVERSION 4" not in s:
    if needle not in s:
        raise SystemExit("Could not locate sctp_output.c OS include")
    s = s.replace(needle, needle + "\n" + compat, 1)
    sctp_output.write_text(s)

os_header = pathlib.Path(sys.argv[4])
s = os_header.read_text()
needle = "#include <sys/socket.h>"
compat = r"""
/* PSL1GHT's socket headers omit these POSIX/BSD constants and helpers. */
#ifndef SOCK_SEQPACKET
#define SOCK_SEQPACKET 5
#endif
#ifndef IPPORT_RESERVED
#define IPPORT_RESERVED 1024
#endif
#ifndef CMSG_ALIGN
#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#endif
#ifndef CMSG_DATA
#define CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#endif
#ifndef CMSG_LEN
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#endif
#ifndef CMSG_SPACE
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#endif
"""
if "PSL1GHT's socket headers do not expose" not in s:
    if needle not in s:
        raise SystemExit("Could not locate sctp_os_userspace.h socket include")
    s = s.replace(needle, needle + "\n" + compat, 1)
    os_header.write_text(s)
PY

grep -q 'FNONBIO' "$JUICE"
grep -q 'FNONBIO' "$UDP"
grep -q 'get_local_default_inet4' "$UDP"
# pthread_t is an opaque structure in PSL1GHT, not a scalar condition.
# usrsctp already tracks successful iterator-thread startup in this flag.
if grep -q 'if (sctp_it_ctl.thread_proc)' "$SCTP_BSD_ADDR"; then
  sed -i 's/if (sctp_it_ctl.thread_proc)/if (SCTP_BASE_VAR(iterator_thread_started))/' "$SCTP_BSD_ADDR"
fi
grep -q 'if (SCTP_BASE_VAR(iterator_thread_started))' "$SCTP_BSD_ADDR"

# pthread_t is a structure in PSL1GHT; assigning integer zero after join is invalid.
if grep -q 'sctp_it_ctl.thread_proc = 0;' "$SCTP_PCB"; then
  sed -i 's/sctp_it_ctl.thread_proc = 0;/memset(\&sctp_it_ctl.thread_proc, 0, sizeof(sctp_it_ctl.thread_proc));/' "$SCTP_PCB"
fi

grep -q 'memset(\&sctp_it_ctl.thread_proc' "$SCTP_PCB"
echo "Applied PS3 libjuice/usrsctp compatibility patches."
