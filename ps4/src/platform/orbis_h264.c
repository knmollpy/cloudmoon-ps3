#include "orbis_h264.h"

#include "orbis_video.h"
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

#include <emmintrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define CM_DMEM_ALIGN 0x4000u
#define CM_DMEM_ONION 0
#define CM_DMEM_GARLIC 3
#define CM_DMEM_WB_GARLIC 10
#define CM_DMEM_PROT_RW 0x33
#define CM_VIDEO_DECODER_MAX_FB 3
#define CM_AU_CAPACITY (8u * 1024u * 1024u)
#define CM_VDEC_CODEC_AVC 1u
#define CM_VDEC_PROFILE_HIGH 100u
#define CM_VDEC_LEVEL_51 51u
#define CM_VDEC_RESOURCE_EMBEDDED 1u

typedef struct {
    uint64_t thisSize;
    uint16_t computePipeId;
    uint16_t computeQueueId;
    bool checkMemoryType;
    uint8_t reserved0;
    uint16_t reserved1;
} cm_vdec_compute_config_t;

typedef struct {
    uint64_t thisSize;
    uint64_t cpuGpuMemorySize;
    void *cpuGpuMemory;
} cm_vdec_compute_memory_t;

typedef struct {
    uint64_t thisSize;
    uint32_t resourceType;
    uint32_t codecType;
    uint32_t profile;
    uint32_t maxLevel;
    int32_t maxFrameWidth;
    int32_t maxFrameHeight;
    int32_t maxDpbFrameCount;
    uint32_t decodePipelineDepth;
    void *computeQueue;
    uint64_t cpuAffinityMask;
    int32_t cpuThreadPriority;
    bool optimizeProgressiveVideo;
    bool checkMemoryType;
    uint8_t reserved0;
    uint8_t reserved1;
    void *extraConfigInfo;
} cm_vdec_config_t;

typedef struct {
    uint64_t thisSize;
    uint64_t cpuMemorySize;
    void *cpuMemory;
    uint64_t gpuMemorySize;
    void *gpuMemory;
    uint64_t cpuGpuMemorySize;
    void *cpuGpuMemory;
    uint64_t maxFrameBufferSize;
    uint32_t frameBufferAlignment;
    uint32_t reserved0;
} cm_vdec_memory_t;

typedef struct {
    uint64_t thisSize;
    void *auData;
    uint64_t auSize;
    uint64_t ptsData;
    uint64_t dtsData;
    uint64_t attachedData;
} cm_vdec_input_t;

typedef struct {
    uint64_t thisSize;
    bool isValid;
    bool isErrorFrame;
    uint8_t pictureCount;
    uint32_t codecType;
    uint32_t frameWidth;
    uint32_t framePitch;
    uint32_t frameHeight;
    void *frameBuffer;
    uint64_t frameBufferSize;
    uint32_t frameFormat;
    uint32_t framePitchInBytes;
} cm_vdec_output_t;

typedef struct {
    uint64_t thisSize;
    void *frameBuffer;
    uint64_t frameBufferSize;
    bool isAccepted;
} cm_vdec_frame_buffer_t;

typedef void *cm_vdec_decoder_t;
typedef void *cm_vdec_queue_t;

typedef int32_t (*cm_vdec_query_compute_fn)(cm_vdec_compute_memory_t *);
typedef int32_t (*cm_vdec_alloc_queue_fn)(const cm_vdec_compute_config_t *,
                                           const cm_vdec_compute_memory_t *,
                                           cm_vdec_queue_t *);
typedef int32_t (*cm_vdec_release_queue_fn)(cm_vdec_queue_t);
typedef int32_t (*cm_vdec_query_memory_fn)(const cm_vdec_config_t *,
                                           cm_vdec_memory_t *);
typedef int32_t (*cm_vdec_create_fn)(const cm_vdec_config_t *,
                                      const cm_vdec_memory_t *,
                                      cm_vdec_decoder_t *);
typedef int32_t (*cm_vdec_delete_fn)(cm_vdec_decoder_t);
typedef int32_t (*cm_vdec_decode_fn)(cm_vdec_decoder_t, const cm_vdec_input_t *,
                                      cm_vdec_frame_buffer_t *, cm_vdec_output_t *);
typedef int32_t (*cm_vdec_reset_fn)(cm_vdec_decoder_t);

typedef struct {
    int module;
    cm_vdec_queue_t queue;
    cm_vdec_query_compute_fn query_compute;
    cm_vdec_alloc_queue_fn alloc_queue;
    cm_vdec_release_queue_fn release_queue;
    cm_vdec_query_memory_fn query_memory;
    cm_vdec_create_fn create;
    cm_vdec_delete_fn delete_decoder;
    cm_vdec_decode_fn decode;
    cm_vdec_reset_fn reset;
} cm_vdec_api_t;

typedef struct {
    void *ptr;
    off_t offset;
    size_t size;
} cm_dmem_t;

static cm_vdec_api_t s_api;
static cm_vdec_decoder_t s_decoder;
static cm_dmem_t s_compute_mem, s_cpu_mem, s_gpu_mem, s_cpu_gpu_mem;
static cm_dmem_t s_au_mem;
static cm_dmem_t s_frame_mem;
static void *s_frame_read_alias;
static size_t s_frame_stride;
static int s_frame_index;
static int s_width, s_height;
static int s_module = -1;
static int s_started;
static int s_error_count;

static size_t cm_align_up(size_t value, size_t alignment) {
    if (alignment < CM_DMEM_ALIGN) alignment = CM_DMEM_ALIGN;
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static int cm_dmem_alloc(cm_dmem_t *mem, size_t requested, int type) {
    int rc;
    if (!mem || requested == 0) return -1;
    memset(mem, 0, sizeof(*mem));
    mem->size = cm_align_up(requested, CM_DMEM_ALIGN);
    rc = sceKernelAllocateDirectMemory(0, (off_t)sceKernelGetDirectMemorySize(),
                                       mem->size, CM_DMEM_ALIGN, type,
                                       &mem->offset);
    if (rc < 0) return rc;
    rc = sceKernelMapDirectMemory(&mem->ptr, mem->size, CM_DMEM_PROT_RW,
                                  0, mem->offset, CM_DMEM_ALIGN);
    if (rc < 0 || !mem->ptr) {
        (void)sceKernelReleaseDirectMemory(mem->offset, mem->size);
        memset(mem, 0, sizeof(*mem));
        return rc < 0 ? rc : -1;
    }
    return 0;
}

static void cm_dmem_free(cm_dmem_t *mem) {
    if (!mem) return;
    if (mem->ptr) (void)sceKernelMunmap(mem->ptr, mem->size);
    if (mem->size) (void)sceKernelReleaseDirectMemory(mem->offset, mem->size);
    memset(mem, 0, sizeof(*mem));
}

static int cm_vdec_symbol(int module, const char *name, void **target) {
    *target = NULL;
    if (sceKernelDlsym(module, name, target) != 0 || !*target) return -1;
    return 0;
}

#define CM_RESOLVE(field, symbol) \
    do { if (cm_vdec_symbol(s_api.module, symbol, (void **)&s_api.field) != 0) return -1; } while (0)

static int cm_vdec_load(void) {
    static const char *dependencies[] = {
        "/system/common/lib/libSceVdecCore.sprx",
        "/system/common/lib/libSceVdecSavc.sprx",
        "/system/common/lib/libSceVdecSavc2.sprx",
        "/system/common/lib/libSceVdecwrap.sprx"
    };
    size_t i;
    int rc;

    if (s_module >= 0) return 0;
    for (i = 0; i < sizeof(dependencies) / sizeof(dependencies[0]); ++i)
        (void)sceKernelLoadStartModule(dependencies[i], 0, NULL, 0, NULL, NULL);
    (void)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VDECCORE);

    rc = sceKernelLoadStartModule("/system/common/lib/libSceVideodec2.sprx",
                                  0, NULL, 0, NULL, NULL);
    if (rc < 0)
        rc = sceKernelLoadStartModule("libSceVideodec2.sprx",
                                      0, NULL, 0, NULL, NULL);
    if (rc < 0) return -1;

    memset(&s_api, 0, sizeof(s_api));
    s_api.module = rc;
    CM_RESOLVE(query_compute, "sceVideodec2QueryComputeMemoryInfo");
    CM_RESOLVE(alloc_queue, "sceVideodec2AllocateComputeQueue");
    CM_RESOLVE(release_queue, "sceVideodec2ReleaseComputeQueue");
    CM_RESOLVE(query_memory, "sceVideodec2QueryDecoderMemoryInfo");
    CM_RESOLVE(create, "sceVideodec2CreateDecoder");
    CM_RESOLVE(delete_decoder, "sceVideodec2DeleteDecoder");
    CM_RESOLVE(decode, "sceVideodec2Decode");
    CM_RESOLVE(reset, "sceVideodec2Reset");
    s_module = rc;
    return 0;
}

static int cm_vdec_queue_init(void) {
    cm_vdec_compute_memory_t memory;
    static const uint16_t queues[][2] = {{0,0},{0,1},{1,0},{1,1},{2,0},{3,0},{4,0}};
    size_t i;
    int rc;

    memset(&memory, 0, sizeof(memory));
    memory.thisSize = sizeof(memory);
    rc = s_api.query_compute(&memory);
    if (rc < 0 || memory.cpuGpuMemorySize == 0) return -1;
    if (cm_dmem_alloc(&s_compute_mem, (size_t)memory.cpuGpuMemorySize,
                      CM_DMEM_ONION) != 0) return -1;
    memory.cpuGpuMemory = s_compute_mem.ptr;

    for (i = 0; i < sizeof(queues) / sizeof(queues[0]); ++i) {
        cm_vdec_compute_config_t config;
        memset(&config, 0, sizeof(config));
        config.thisSize = sizeof(config);
        config.computePipeId = queues[i][0];
        config.computeQueueId = queues[i][1];
        config.checkMemoryType = true;
        rc = s_api.alloc_queue(&config, &memory, &s_api.queue);
        if (rc == 0 && s_api.queue) return 0;
    }
    return -1;
}

static int cm_vdec_init_decoder(int width, int height) {
    cm_vdec_config_t config;
    cm_vdec_memory_t memory;
    size_t fb_align, frame_count = CM_VIDEO_DECODER_MAX_FB;
    int rc;

    memset(&config, 0, sizeof(config));
    config.thisSize = sizeof(config);
    config.resourceType = CM_VDEC_RESOURCE_EMBEDDED;
    config.codecType = CM_VDEC_CODEC_AVC;
    config.profile = CM_VDEC_PROFILE_HIGH;
    config.maxLevel = CM_VDEC_LEVEL_51;
    config.maxFrameWidth = width;
    config.maxFrameHeight = (height + 15) & ~15;
    config.maxDpbFrameCount = 4;
    config.decodePipelineDepth = 1;
    config.computeQueue = s_api.queue;
    config.cpuAffinityMask = 0x3Full;
    config.cpuThreadPriority = 700;
    config.optimizeProgressiveVideo = true;
    config.checkMemoryType = false;

    memset(&memory, 0, sizeof(memory));
    memory.thisSize = sizeof(memory);
    rc = s_api.query_memory(&config, &memory);
    if (rc < 0) return rc;

    if (memory.cpuMemorySize &&
        cm_dmem_alloc(&s_cpu_mem, (size_t)memory.cpuMemorySize,
                      CM_DMEM_ONION) != 0) return -1;
    memory.cpuMemory = s_cpu_mem.ptr;

    if (memory.gpuMemorySize &&
        cm_dmem_alloc(&s_gpu_mem, (size_t)memory.gpuMemorySize,
                      CM_DMEM_GARLIC) != 0) return -1;
    memory.gpuMemory = s_gpu_mem.ptr;

    if (memory.cpuGpuMemorySize &&
        cm_dmem_alloc(&s_cpu_gpu_mem, (size_t)memory.cpuGpuMemorySize,
                      CM_DMEM_ONION) != 0) return -1;
    memory.cpuGpuMemory = s_cpu_gpu_mem.ptr;

    fb_align = memory.frameBufferAlignment ? memory.frameBufferAlignment : 0x100u;
    s_frame_stride = cm_align_up((size_t)memory.maxFrameBufferSize, fb_align);
    if (s_frame_stride == 0) return -1;

    if (cm_dmem_alloc(&s_frame_mem, s_frame_stride * frame_count,
                      CM_DMEM_GARLIC) != 0) return -1;

    /* Same physical framebuffer, CPU-cacheable read alias for NV12 conversion. */
    rc = sceKernelMapDirectMemory2(&s_frame_read_alias,
                                   s_frame_mem.size,
                                   CM_DMEM_WB_GARLIC,
                                   CM_DMEM_PROT_RW,
                                   0,
                                   s_frame_mem.offset,
                                   CM_DMEM_ALIGN);
    if (rc < 0) s_frame_read_alias = s_frame_mem.ptr;

    if (cm_dmem_alloc(&s_au_mem, CM_AU_CAPACITY * frame_count,
                      CM_DMEM_ONION) != 0) return -1;

    rc = s_api.create(&config, &memory, &s_decoder);
    if (rc < 0 || !s_decoder) return rc < 0 ? rc : -1;
    s_frame_index = 0;
    return 0;
}

static uint8_t cm_clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static void cm_cache_invalidate(const uint8_t *data, size_t length) {
    uintptr_t begin, end, p;
    if (!data || !length) return;
    begin = (uintptr_t)data & ~(uintptr_t)63u;
    end = ((uintptr_t)data + length + 63u) & ~(uintptr_t)63u;
    for (p = begin; p < end; p += 64u)
        _mm_clflush((const void *)p);
    _mm_mfence();
}

static int cm_present_nv12(const uint8_t *base,
                           int width, int height, int pitch) {
    cm_ui_surface_t *surface = cm_orbis_video_surface();
    const uint8_t *uv;
    int x, y, target_w, target_h;
    if (!surface || !base || width <= 0 || height <= 0 ||
        pitch < width) return -1;
    uv = base + (size_t)pitch * (size_t)height;
    target_w = surface->width;
    target_h = surface->height;

    for (y = 0; y < target_h; ++y) {
        int sy = (int)((int64_t)y * height / target_h);
        uint32_t *dst = surface->pixels + (size_t)y * (size_t)surface->pitch_pixels;
        const uint8_t *src_y = base + (size_t)sy * (size_t)pitch;
        const uint8_t *src_uv = uv + (size_t)(sy / 2) * (size_t)pitch;
        for (x = 0; x < target_w; ++x) {
            int sx = (int)((int64_t)x * width / target_w);
            int yy = (int)src_y[sx] - 16;
            int uu = (int)src_uv[sx & ~1] - 128;
            int vv = (int)src_uv[(sx & ~1) + 1] - 128;
            int c = yy < 0 ? 0 : yy;
            int red = (298 * c + 459 * vv + 128) >> 8;
            int green = (298 * c - 55 * uu - 136 * vv + 128) >> 8;
            int blue = (298 * c + 541 * uu + 128) >> 8;
            dst[x] = 0xFF000000u |
                     ((uint32_t)cm_clamp_u8(red) << 16) |
                     ((uint32_t)cm_clamp_u8(green) << 8) |
                     (uint32_t)cm_clamp_u8(blue);
        }
    }
    return cm_orbis_video_present();
}

int cm_orbis_h264_start(int width, int height) {
    if (s_started) return 0;
    if (width < 320 || height < 240 || width > 1920 || height > 1088)
        return -1;
    s_width = width;
    s_height = height;

    if (cm_orbis_video_init(1280, 720) != 0)
        return -1;
    if (cm_vdec_load() != 0 || cm_vdec_queue_init() != 0 ||
        cm_vdec_init_decoder(width, height) != 0) {
        cm_orbis_h264_stop();
        return -1;
    }
    s_error_count = 0;
    s_started = 1;
    return 0;
}

int cm_orbis_h264_submit(const uint8_t *annex_b, size_t length) {
    cm_vdec_input_t input;
    cm_vdec_frame_buffer_t frame_buffer;
    cm_vdec_output_t output;
    uint8_t *au;
    size_t slot_size;
    int rc;
    const uint8_t *read_ptr;
    ptrdiff_t offset;
    int pitch, frame_width, frame_height;

    if (!s_started || !s_decoder || !annex_b || length == 0 ||
        length > CM_AU_CAPACITY) return -1;

    slot_size = s_au_mem.size / CM_VIDEO_DECODER_MAX_FB;
    au = (uint8_t *)s_au_mem.ptr + (size_t)s_frame_index * slot_size;
    memcpy(au, annex_b, length);

    memset(&input, 0, sizeof(input));
    input.thisSize = sizeof(input);
    input.auData = au;
    input.auSize = length;
    input.ptsData = 0;
    input.dtsData = 0;

    memset(&frame_buffer, 0, sizeof(frame_buffer));
    frame_buffer.thisSize = sizeof(frame_buffer);
    frame_buffer.frameBuffer = (uint8_t *)s_frame_mem.ptr +
                               (size_t)s_frame_index * s_frame_stride;
    frame_buffer.frameBufferSize = s_frame_stride;

    memset(&output, 0, sizeof(output));
    output.thisSize = sizeof(output);

    rc = s_api.decode(s_decoder, &input, &frame_buffer, &output);
    s_frame_index = (s_frame_index + 1) % CM_VIDEO_DECODER_MAX_FB;
    if (rc < 0) {
        ++s_error_count;
        if (s_error_count >= 30) {
            (void)s_api.reset(s_decoder);
            s_error_count = 0;
        }
        return -1;
    }
    if (!output.isValid || !output.frameBuffer)
        return 0;
    s_error_count = 0;

    (void)sceGnmFlushGarlic();
    read_ptr = (const uint8_t *)output.frameBuffer;
    offset = (ptrdiff_t)((uintptr_t)read_ptr - (uintptr_t)s_frame_mem.ptr);
    if (s_frame_read_alias && offset >= 0 &&
        (size_t)offset < s_frame_mem.size)
        read_ptr = (const uint8_t *)s_frame_read_alias + offset;
    pitch = output.framePitchInBytes ? (int)output.framePitchInBytes :
                                       (int)output.framePitch;
    frame_width = output.frameWidth ? (int)output.frameWidth : s_width;
    frame_height = output.frameHeight ? (int)output.frameHeight :
                                         ((s_height + 15) & ~15);
    cm_cache_invalidate(read_ptr, (size_t)pitch * (size_t)frame_height * 3u / 2u);
    return cm_present_nv12(read_ptr, frame_width, frame_height, pitch);
}

void cm_orbis_h264_stop(void) {
    s_started = 0;
    if (s_decoder && s_api.delete_decoder)
        (void)s_api.delete_decoder(s_decoder);
    s_decoder = NULL;
    if (s_api.queue && s_api.release_queue)
        (void)s_api.release_queue(s_api.queue);
    s_api.queue = NULL;
    if (s_frame_read_alias && s_frame_read_alias != s_frame_mem.ptr)
        (void)sceKernelMunmap(s_frame_read_alias, s_frame_mem.size);
    s_frame_read_alias = NULL;
    cm_dmem_free(&s_frame_mem);
    cm_dmem_free(&s_au_mem);
    cm_dmem_free(&s_cpu_gpu_mem);
    cm_dmem_free(&s_gpu_mem);
    cm_dmem_free(&s_cpu_mem);
    cm_dmem_free(&s_compute_mem);
    s_frame_stride = 0;
    s_frame_index = 0;
    memset(&s_api, 0, sizeof(s_api));
    if (s_module >= 0) {
        s_module = -1;
    }
    s_width = s_height = 0;
    cm_orbis_video_shutdown();
}
