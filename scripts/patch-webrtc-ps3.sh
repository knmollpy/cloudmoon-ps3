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
SCTP_USERSPACE="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_userspace.c"
RTC_UTILS="$ROOT_DIR/vendor/libdatachannel/include/rtc/utils.hpp"
RTC_CANDIDATE="$ROOT_DIR/vendor/libdatachannel/src/candidate.cpp"
RTC_CONFIGURATION="$ROOT_DIR/vendor/libdatachannel/src/configuration.cpp"

for file in "$JUICE" "$UDP" "$SCTP_IND" "$SCTP_INPUT" "$SCTP_OUTPUT" "$SCTP_PCB" "$SCTP_BSD_ADDR" "$SCTP_OS" "$SCTP_USERSPACE" "$RTC_UTILS" "$RTC_CANDIDATE" "$RTC_CONFIGURATION"; do
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
if "define CMSG_DATA" not in s:
    if needle not in s:
        raise SystemExit("Could not locate sctp_os_userspace.h socket include")
    s = s.replace(needle, needle + "\n" + compat, 1)
    os_header.write_text(s)

# sctp_timer.c and the rest of usrsctp include sctp_os.h, which imports
# this userspace header. Define timercmp centrally for every translation unit.
s = os_header.read_text()
needle = "#include <sys/time.h>"
compat = """
#ifndef timercmp
#define timercmp(a, b, op) (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec op (b)->tv_usec) : ((a)->tv_sec op (b)->tv_sec))
#endif
"""
if "define timercmp(a, b, op)" not in s:
    if needle not in s:
        raise SystemExit("Could not locate sys/time.h in sctp_os_userspace.h")
    s = s.replace(needle, needle + "\n" + compat, 1)
    os_header.write_text(s)
PY

# The pinned usrsctp userspace socket layer assumes BSD kernel constants that
# patched newlib/PSL1GHT does not publish. Keep these fallbacks target-scoped.
python3 - "$SCTP_OS" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
needle = "#include <sys/socket.h>"
compat = r"""
#ifndef UIO_MAXIOV
#define UIO_MAXIOV 16
#endif
#ifndef ERESTART
#define ERESTART EINTR
#endif
#ifndef SOMAXCONN
#define SOMAXCONN 128
#endif
"""
if "#define UIO_MAXIOV 16" not in s:
    if needle not in s:
        raise SystemExit("Could not locate socket include for usrsctp socket constants")
    s = s.replace(needle, needle + "\\n" + compat, 1)
    p.write_text(s)
PY

# libdatachannel's pinned headers omit direct standard includes needed by PPU GCC.
# PSL1GHT's netdb.h exposes gethostbyname(), but not the POSIX getaddrinfo family.
python3 - "$RTC_UTILS" "$RTC_CANDIDATE" <<'PY'
import pathlib, sys
candidate = pathlib.Path(sys.argv[2])
s = candidate.read_text()
s = s.replace("std::stoul(servbuffer)", "strtoul(servbuffer, nullptr, 10)")
if "std::stoul(servbuffer)" in s:
    raise SystemExit("std::stoul remains in candidate.cpp")
if "strtoul(servbuffer, nullptr, 10)" not in s:
    raise SystemExit("Could not patch candidate service-port conversion")
candidate.write_text(s)
PY

grep -q 'strtoul(servbuffer, nullptr, 10)' "$RTC_CANDIDATE"
if grep -q 'std::stoul(servbuffer)' "$RTC_CANDIDATE"; then
  echo "Unsupported std::stoul remains in candidate.cpp" >&2
  exit 1
fi

# configuration.cpp also uses std::stoul, which is unavailable in the PS3 libstdc++.
# Convert every std::string port value through the C runtime instead.
python3 - "$RTC_CONFIGURATION" <<'PY'
import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
pattern = re.compile(r"std::stoul\(([^()]*)\)")
s, count = pattern.subn(lambda m: "strtoul(" + m.group(1) + ".c_str(), nullptr, 10)", s)
if count != 4:
    raise SystemExit("Expected to patch 4 std::stoul calls in configuration.cpp, patched " + str(count))
if "#include <stdlib.h>" not in s:
    lines = s.splitlines(True)
    last_include = max((i for i, line in enumerate(lines) if line.startswith("#include ")), default=-1)
    if last_include < 0:
        raise SystemExit("Could not find include section in configuration.cpp")
    lines.insert(last_include + 1, "#include <stdlib.h>\n")
    s = "".join(lines)
if "std::stoul(" in s:
    raise SystemExit("Unsupported std::stoul remains in configuration.cpp")
p.write_text(s)
PY
grep -q 'strtoul(service.c_str(), nullptr, 10)' "$RTC_CONFIGURATION"
grep -q 'strtoul(service_.c_str(), nullptr, 10)' "$RTC_CONFIGURATION"
if grep -q 'std::stoul(' "$RTC_CONFIGURATION"; then
  echo "Unsupported std::stoul remains in configuration.cpp" >&2
  exit 1
fi


# usrsctp has no RNG backend selected for the PSL1GHT/newlib target.
# Use the OS entropy device (never a deterministic/fuzzing RNG).
python3 - "$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/user_environment.c" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
needle = '#elif defined(__linux__)'
branch = r"""
#elif defined(__PS3__) || defined(__Userspace__)
#include <lv2/system.h>

void
init_random(void)
{
}

void
read_random(void *buf, size_t size)
{
    size_t done = 0;
    while (done < size) {
        u32 chunk = (u32)((size - done) > 64 ? 64 : (size - done));
        if (sysGetRandomNumber((uint8_t *)buf + done, chunk) != 0)
            abort();
        done += chunk;
    }
}

void
finish_random(void)
{
}
"""
if "sysGetRandomNumber" not in s:
    if needle not in s:
        raise SystemExit("Could not locate usrsctp Linux RNG branch")
    s = s.replace(needle, branch + "\n" + needle, 1)
p.write_text(s)
PY
grep -q 'sysGetRandomNumber' "$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/user_environment.c"

# PSL1GHT lacks the BSD/Linux interface-query API used by usrsctp.
# Use the PS3 GameOS Ethernet/WLAN MTU rather than emulating ifreq/ioctl.
python3 - "$SCTP_USERSPACE" <<'PY'
import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
pattern = r'int\s+sctp_userspace_get_mtu_from_ifn\(uint32_t if_index\)\s*\{.*?\n\}'
replacement = """int
sctp_userspace_get_mtu_from_ifn(uint32_t if_index)
{
    /* PSL1GHT has no ifreq/if_indextoname/SIOCGIFMTU interface query. */
    return (if_index == 0xffffffff) ? 1280 : 1500;
}"""
updated, count = re.subn(pattern, replacement, s, count=1, flags=re.S)
if count != 1:
    raise SystemExit("Could not replace usrsctp MTU interface-query function")
p.write_text(updated)
PY
grep -q 'return (if_index == 0xffffffff) ? 1280 : 1500;' "$SCTP_USERSPACE"
if grep -q 'if (if_indextoname(if_index' "$SCTP_USERSPACE" || grep -q 'struct ifreq ifr;' "$SCTP_USERSPACE"; then
  echo "Unsupported BSD interface-query code remains in usrsctp MTU helper" >&2
  exit 1
fi

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


# Apply one target-wide pthread-backed C++ threading compatibility layer.
# This patches libdatachannel's own CMake target after submodule checkout, so
# every datachannel translation unit sees the same declarations.
python3 - "$ROOT_DIR/vendor/libdatachannel/CMakeLists.txt" "$ROOT_DIR/cmake/ps3/include/ps3_threads.hpp" <<'PY'
import pathlib, sys
cmake_file = pathlib.Path(sys.argv[1])
shim_file = pathlib.Path(sys.argv[2])
if not shim_file.is_file():
    raise SystemExit("PS3 C++ threading shim is missing: " + str(shim_file))
s = cmake_file.read_text()
marker = "# CloudMoon PS3 pthread-backed C++ threading shim"
if marker not in s:
    needle = "target_compile_definitions(datachannel PRIVATE RTC_EXPORTS)"
    if needle not in s:
        raise SystemExit("Could not locate datachannel target in libdatachannel CMakeLists.txt")
    block = marker + r"""
if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    target_compile_options(datachannel PRIVATE -include "${CMAKE_SOURCE_DIR}/../../cmake/ps3/include/ps3_threads.hpp")
    target_link_libraries(datachannel PRIVATE -lpthread)
    target_compile_options(datachannel-static PRIVATE -include "${CMAKE_SOURCE_DIR}/../../cmake/ps3/include/ps3_threads.hpp")
    target_link_libraries(datachannel-static PRIVATE -lpthread)
endif()

"""
    s = s.replace(needle, block + needle, 1)
cmake_file.write_text(s)
PY
grep -q 'CloudMoon PS3 pthread-backed C++ threading shim' "$ROOT_DIR/vendor/libdatachannel/CMakeLists.txt"
grep -q 'struct ps3_future_state' "$ROOT_DIR/cmake/ps3/include/ps3_threads.hpp"
grep -q 'class condition_variable' "$ROOT_DIR/cmake/ps3/include/ps3_threads.hpp"


# Repair legacy malformed newline insertion in the usrsctp compatibility header.
python3 - "$SCTP_OS" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
s = s.replace("#include <sys/socket.h>\\n", "#include <sys/socket.h>\n")
p.write_text(s)
PY

# Define the complete ancillary-data API required by usrsctp on PSL1GHT.
python3 - "$SCTP_OS" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
anchor = "#include <sys/socket.h>"
if anchor not in s:
    raise SystemExit("Cannot add ancillary-data compatibility: socket include missing")
block = r"""
#ifndef CMSG_FIRSTHDR
#define CMSG_FIRSTHDR(m) \
    ((m)->msg_controllen >= sizeof(struct cmsghdr) ? \
     (struct cmsghdr *)(m)->msg_control : (struct cmsghdr *)0)
#endif
#ifndef CMSG_NXTHDR
#define CMSG_NXTHDR(m, c) \
    (((char *)(c) + CMSG_ALIGN((c)->cmsg_len) + sizeof(struct cmsghdr) <= \
      (char *)(m)->msg_control + (m)->msg_controllen) ? \
     (struct cmsghdr *)((char *)(c) + CMSG_ALIGN((c)->cmsg_len)) : \
     (struct cmsghdr *)0)
#endif
"""
if "define CMSG_FIRSTHDR" not in s:
    s = s.replace(anchor, anchor + "\n" + block, 1)
p.write_text(s)
PY
grep -q 'define CMSG_FIRSTHDR' "$SCTP_OS"
grep -q 'define CMSG_NXTHDR' "$SCTP_OS"

# Use the PS3 dependency prefix before SDK headers so Mbed TLS 3.x headers
# cannot be shadowed by an older mbedtls copy bundled with PSL1GHT.
python3 - "$ROOT_DIR/vendor/libdatachannel/src" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
for p in root.rglob("*"):
    if p.suffix not in (".cpp", ".hpp", ".h"):
        continue
    s = p.read_text(errors="ignore")
    old = "std::thread"
    if old in s:
        s = s.replace(old, "rtc::impl::Ps3Thread")
    s = s.replace("std::this_thread::sleep_for(", "rtc::impl::ps3_sleep_for(")
    p.write_text(s)

utils = root / "impl" / "utils.cpp"
import re
s = utils.read_text()
s, count = re.subn(
    r"static_cast<unsigned int>\(\s*std::hash<rtc::impl::Ps3Thread::id>\{\}\(std::this_thread::get_id\(\)\)\)",
    "static_cast<unsigned int>((uintptr_t)pthread_self())",
    s
)
if count != 1:
    raise SystemExit("Expected one libdatachannel thread-id seed expression, patched " + str(count))
if "#include <pthread.h>" not in s:
    lines = s.splitlines(True)
    i = max((i for i, line in enumerate(lines) if line.startswith("#include ")), default=-1)
    if i < 0:
        raise SystemExit("Cannot add pthread include to libdatachannel utils.cpp")
    lines.insert(i + 1, "#include <pthread.h>\n")
    s = "".join(lines)
if "#include <stdint.h>" not in s:
    s = s.replace("#include <pthread.h>", "#include <pthread.h>\n#include <stdint.h>", 1)
if "std::this_thread::get_id()" in s or "Ps3Thread::id" in s:
    raise SystemExit("Unsupported libdatachannel thread-id usage remains")
utils.write_text(s)
PY

# Fail early if any compiled libdatachannel source still uses the unsupported
# std::thread constructor or std::this_thread sleep implementation.
if grep -R -n 'std::thread\\|std::this_thread::sleep_for' "$ROOT_DIR/vendor/libdatachannel/src"; then
  echo "Unsupported std::thread usage remains in libdatachannel sources" >&2
  exit 1
fi
