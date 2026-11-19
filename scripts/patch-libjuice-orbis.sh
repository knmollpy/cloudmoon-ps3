#!/usr/bin/env bash
set -euo pipefail

HEADER="${1:-vendor/libdatachannel/deps/libjuice/src/thread.h}"
test -f "$HEADER"

python3 - "$HEADER" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()

old_include = """#if defined(__FreeBSD__)
#include <pthread_np.h> // for pthread_set_name_np
#endif"""
new_include = """#if defined(__FreeBSD__) && !defined(__ORBIS__)
#include <pthread_np.h> // for pthread_set_name_np
#endif"""
old_name = """#elif defined(__FreeBSD__)
\tpthread_set_name_np(pthread_self(), name);
#else
\t(void)name;"""
new_name = """#elif defined(__FreeBSD__) && !defined(__ORBIS__)
\tpthread_set_name_np(pthread_self(), name);
#else
\t(void)name;"""

if old_include not in text and new_include not in text:
    raise SystemExit("libjuice pthread_np include block changed unexpectedly")
if old_name not in text and new_name not in text:
    raise SystemExit("libjuice thread-name block changed unexpectedly")

text = text.replace(old_include, new_include).replace(old_name, new_name)
path.write_text(text)
PY


UDP_SOURCE="${2:-vendor/libdatachannel/deps/libjuice/src/udp.c}"
test -f "$UDP_SOURCE"

python3 - "$UDP_SOURCE" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()

include = "#include <time.h>\n"
orbis_include = "#include <time.h>\n\n#if defined(__ORBIS__)\n#include <fcntl.h>\n#endif\n"
if orbis_include not in text:
    if include not in text:
        raise SystemExit("libjuice udp include anchor changed unexpectedly")
    text = text.replace(include, orbis_include, 1)

old = """\tctl_t nbio = 1;
\tif (ioctlsocket(sock, FIONBIO, &nbio)) {
\t\tJLOG_ERROR("Setting non-blocking mode on UDP socket failed, errno=%d", sockerrno);
\t\tgoto error;
\t}"""
new = """#if defined(__ORBIS__)
\tint flags = fcntl(sock, F_GETFL, 0);
\tif (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
\t\tJLOG_ERROR("Setting non-blocking mode on UDP socket failed, errno=%d", sockerrno);
\t\tgoto error;
\t}
#else
\tctl_t nbio = 1;
\tif (ioctlsocket(sock, FIONBIO, &nbio)) {
\t\tJLOG_ERROR("Setting non-blocking mode on UDP socket failed, errno=%d", sockerrno);
\t\tgoto error;
\t}
#endif"""

if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("libjuice UDP nonblocking block changed unexpectedly")

path.write_text(text)
PY


for SOURCE in \
    vendor/libdatachannel/deps/libjuice/src/tcp.c \
    vendor/libdatachannel/deps/libjuice/src/udp.c
do
    test -f "$SOURCE"
    python3 - "$SOURCE" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
old = """\tctl_t nbio = 1;
\tif (ioctlsocket(sock, FIONBIO, &nbio)) {"""
new = """#if defined(__ORBIS__)
\tint flags = fcntl(sock, F_GETFL, 0);
\tif (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
#else
\tctl_t nbio = 1;
\tif (ioctlsocket(sock, FIONBIO, &nbio)) {
#endif"""
if old in text:
    text = text.replace(old, new)
elif new not in text:
    raise SystemExit(f"{path}: expected nonblocking socket block not found")
path.write_text(text)
PY
done
