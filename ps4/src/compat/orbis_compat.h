#ifndef CLOUDMOON_ORBIS_COMPAT_H
#define CLOUDMOON_ORBIS_COMPAT_H

#include <stddef.h>
#include <stdint.h>

/* FreeBSD userspace SCTP uses _ALIGN from sys/param.h; OpenOrbis omits it. */
#ifndef _ALIGNBYTES
#define _ALIGNBYTES (sizeof(long) - 1u)
#endif
#ifndef _ALIGN
#define _ALIGN(value) (((uintptr_t)(value) + _ALIGNBYTES) & ~(uintptr_t)_ALIGNBYTES)
#endif

#ifdef __cplusplus
extern "C" {
#endif
int timingsafe_bcmp(const void *left, const void *right, size_t length);
void arc4random_buf(void *buffer, size_t length);
#ifdef __cplusplus
}
#endif

#endif
