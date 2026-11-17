#ifndef CLOUDMOON_PS3_SYS_UIO_H
#define CLOUDMOON_PS3_SYS_UIO_H
#include <sys/types.h>
#include <stddef.h>
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#endif
