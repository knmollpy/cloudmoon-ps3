#!/bin/sh
# Apply the small POSIX-to-PSL1GHT differences required by the pinned
# libjuice and usrsctp submodules. Keep ICE and SCTP enabled.
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
JUICE="$ROOT_DIR/vendor/libdatachannel/deps/libjuice/src/tcp.c"
SCTP="$ROOT_DIR/vendor/libdatachannel/deps/usrsctp/usrsctplib/netinet/sctp_bsd_addr.c"

[ -f "$JUICE" ] || { echo "libjuice tcp.c not found: $JUICE" >&2; exit 1; }
[ -f "$SCTP" ] || { echo "usrsctp sctp_bsd_addr.c not found: $SCTP" >&2; exit 1; }

# PSL1GHT exposes FNONBIO (not the POSIX/Windows FIONBIO ioctl request).
if grep -q 'ioctlsocket(sock, FIONBIO, &nbio)' "$JUICE"; then
  sed -i 's/ioctlsocket(sock, FIONBIO, \&nbio)/ioctlsocket(sock, FNONBIO, \&nbio)/' "$JUICE"
fi

# The PS3 socket API has no MSG_NOSIGNAL; it does not need this POSIX flag.
# Retain the original behavior on every other target.
sed -i 's/defined(__APPLE__) || defined(_WIN32)/defined(__APPLE__) || defined(_WIN32) || defined(__CELLOS_LV2__)/g' "$JUICE"

# pthread_t is a structure in the PSL1GHT pthread implementation. The
# upstream code already maintains an integer started flag for this thread;
# use that flag rather than treating pthread_t as a scalar.
if grep -q 'if (sctp_it_ctl.thread_proc)' "$SCTP"; then
  sed -i 's/if (sctp_it_ctl.thread_proc)/if (SCTP_BASE_VAR(iterator_thread_started))/' "$SCTP"
fi

grep -q 'ioctlsocket(sock, FNONBIO, \&nbio)' "$JUICE"
grep -q 'SCTP_BASE_VAR(iterator_thread_started)' "$SCTP"
echo "Applied PS3 libjuice/usrsctp compatibility patches."
