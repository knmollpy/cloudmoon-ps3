#include "orbis_video.h"

#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#define CM_VIDEO_USER_MAIN       0xFF
#define CM_VIDEO_BUS_MAIN        0
#define CM_VIDEO_PIXEL_BGRA      0x80000000u
#define CM_VIDEO_TILING_LINEAR   1u
#define CM_VIDEO_ASPECT_16_9     0u
#define CM_VIDEO_FLIP_VSYNC      1u
#define CM_VIDEO_BUFFER_COUNT    3
#define CM_VIDEO_DMEM_ALIGN      0x200000u
#define CM_VIDEO_DMEM_GARLIC_WC  3
#define CM_VIDEO_DMEM_PROT_RW    0x33

typedef struct {
    int32_t format;
    int32_t tmode;
    int32_t aspect;
    uint32_t width;
    uint32_t height;
    uint32_t pixelPitch;
    uint64_t reserved[2];
} cm_video_buffer_attribute_t;

extern int32_t sceVideoOutOpen(int32_t userId, int32_t busType,
                               int32_t index, const void *param);
extern int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t flipRate);
extern int32_t sceVideoOutRegisterBuffers(int32_t handle, int32_t startIndex,
                                          void *const *addrs, int32_t count,
                                          const cm_video_buffer_attribute_t *attr);
extern int32_t sceVideoOutSubmitFlip(int32_t handle, int32_t bufferIndex,
                                     uint32_t flipMode, int64_t flipArg);
extern void sceVideoOutSetBufferAttribute(void *attr, uint32_t pixelFormat,
                                          uint32_t tilingMode, uint32_t aspectRatio,
                                          uint32_t width, uint32_t height,
                                          uint32_t pitchInPixel);
extern int32_t sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd,
                                             size_t len, size_t align,
                                             int32_t memoryType, off_t *physAddrOut);
extern int32_t sceKernelMapDirectMemory(void **addr, size_t len, int32_t prot,
                                        int32_t flags, off_t directMemoryStart,
                                        size_t align);
extern size_t sceKernelGetDirectMemorySize(void);
extern void sceGnmFlushGarlic(void);

static int s_video = -1;
static int s_width;
static int s_height;
static int s_next;
static off_t s_memory_offset;
static size_t s_memory_size;
static size_t s_frame_size;
static void *s_memory;
static void *s_buffers[CM_VIDEO_BUFFER_COUNT];
static cm_ui_surface_t s_surface;
static int s_ready;
static int s_allocated;
static int s_registered;

extern int32_t sceVideoOutClose(int32_t handle);
extern int32_t sceVideoOutUnregisterBuffers(int32_t handle, int32_t attributeIndex);
extern int32_t sceKernelMunmap(void *addr, size_t len);
extern int32_t sceKernelReleaseDirectMemory(off_t start, size_t len);

void cm_orbis_video_shutdown(void) {
    if (s_video >= 0) {
        if (s_registered) (void)sceVideoOutUnregisterBuffers(s_video, 0);
        (void)sceVideoOutClose(s_video);
    }
    if (s_memory) (void)sceKernelMunmap(s_memory, s_memory_size);
    if (s_allocated) (void)sceKernelReleaseDirectMemory(s_memory_offset, s_memory_size);
    s_video = -1;
    s_memory = NULL;
    s_memory_size = 0;
    s_allocated = 0;
    s_registered = 0;
    s_ready = 0;
    s_frame_size = 0;
    s_memory_offset = 0;
    memset(s_buffers, 0, sizeof(s_buffers));
    memset(&s_surface, 0, sizeof(s_surface));
}

int cm_orbis_video_init(int width, int height) {
    cm_video_buffer_attribute_t attr;
    int32_t user_id = CM_VIDEO_USER_MAIN;
    int rc;
    size_t frame_size, allocation_size;
    int i;

    if (s_ready) return 0;
    if (s_video >= 0) return -1;
    if (width < 640 || height < 360 || width > 1920 || height > 1080)
        return -1;

    s_width = width;
    s_height = height;
    s_surface.width = width;
    s_surface.height = height;
    s_surface.pitch_pixels = width;

    frame_size = (size_t)width * (size_t)height * 4u;
    s_frame_size = (frame_size + CM_VIDEO_DMEM_ALIGN - 1u) &
                   ~(size_t)(CM_VIDEO_DMEM_ALIGN - 1u);
    allocation_size = s_frame_size * CM_VIDEO_BUFFER_COUNT;
    s_memory_size = allocation_size;

    if (sceUserServiceGetInitialUser(&user_id) < 0)
        user_id = CM_VIDEO_USER_MAIN;
    s_video = sceVideoOutOpen(user_id, CM_VIDEO_BUS_MAIN, 0, NULL);
    if (s_video < 0)
        s_video = sceVideoOutOpen(CM_VIDEO_USER_MAIN, CM_VIDEO_BUS_MAIN, 0, NULL);
    if (!s_ready || s_video < 0) return -1;

    rc = sceKernelAllocateDirectMemory(0, (off_t)sceKernelGetDirectMemorySize(),
                                       s_memory_size, CM_VIDEO_DMEM_ALIGN,
                                       CM_VIDEO_DMEM_GARLIC_WC, &s_memory_offset);
    if (rc < 0) goto fail;
    s_allocated = 1;

    s_memory = NULL;
    rc = sceKernelMapDirectMemory(&s_memory, s_memory_size, CM_VIDEO_DMEM_PROT_RW,
                                  0, s_memory_offset, CM_VIDEO_DMEM_ALIGN);
    if (rc < 0 || !s_memory) goto fail;

    for (i = 0; i < CM_VIDEO_BUFFER_COUNT; ++i) {
        s_buffers[i] = (uint8_t *)s_memory + (size_t)i * s_frame_size;
        memset(s_buffers[i], 0, s_frame_size);
    }

    memset(&attr, 0, sizeof(attr));
    sceVideoOutSetBufferAttribute(&attr, CM_VIDEO_PIXEL_BGRA,
                                  CM_VIDEO_TILING_LINEAR, CM_VIDEO_ASPECT_16_9,
                                  (uint32_t)width, (uint32_t)height,
                                  (uint32_t)width);
    rc = sceVideoOutRegisterBuffers(s_video, 0, s_buffers,
                                    CM_VIDEO_BUFFER_COUNT, &attr);
    if (rc < 0) goto fail;
    s_registered = 1;

    (void)sceVideoOutSetFlipRate(s_video, 0);
    s_next = 0;
    s_surface.pixels = (uint32_t *)s_buffers[s_next];
    (void)sceGnmFlushGarlic();
    s_ready = 1;
    return 0;

fail:
    cm_orbis_video_shutdown();
    return -1;
}

cm_ui_surface_t *cm_orbis_video_surface(void) {
    if (!s_ready || s_video < 0) return NULL;
    s_surface.pixels = (uint32_t *)s_buffers[s_next];
    return &s_surface;
}

int cm_orbis_video_present(void) {
    int rc;
    if (s_video < 0) return -1;

    (void)sceGnmFlushGarlic();
    rc = sceVideoOutSubmitFlip(s_video, s_next, CM_VIDEO_FLIP_VSYNC, 0);
    if (rc < 0)
        rc = sceVideoOutSubmitFlip(s_video, s_next, 0, 0);
    if (rc < 0) return -1;

    s_next = (s_next + 1) % CM_VIDEO_BUFFER_COUNT;
    s_surface.pixels = (uint32_t *)s_buffers[s_next];
    return 0;
}
