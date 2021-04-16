#ifndef LFS_UTIL_H
#define LFS_UTIL_H

#include <string.h>
#include "esp_types.h"
#include "esp_log.h"
#include "endian.h" /* newlib/platform_include */
#include "sdkconfig.h"

#if CONFIG_LFS_THREADSAFE_DEF
#define LFS_THREADSAFE
#else
#error "want thread safety"
#endif

#ifndef LFS_NO_MALLOC
#include <stdlib.h>
#else
#error "want malloc"
#endif

//#define LFS_NO_ASSERT
#define LFS_YES_TRACE

#ifndef LFS_NO_ASSERT
#include "esp_assert.h" /* esp_common/esp_assert.h */
#define LFS_ASSERT(test) TRY_STATIC_ASSERT((test), "msg")
#else
#define LFS_ASSERT(test)
#endif

#define LFS_TRACE(fmt, ...) ESP_LOGV("littlefs", (fmt) __VA_OPT__(,) __VA_ARGS__)
#define LFS_DEBUG(fmt, ...) ESP_LOGD("littlefs", (fmt) __VA_OPT__(,) __VA_ARGS__)
#define LFS_WARN(fmt, ...) ESP_LOGW("littlefs", (fmt) __VA_OPT__(,) __VA_ARGS__)
#define LFS_ERROR(fmt, ...) ESP_LOGE("littlefs", (fmt) __VA_OPT__(,) __VA_ARGS__)

// Min/max functions for unsigned 32-bit numbers
static inline uint32_t lfs_max(uint32_t a, uint32_t b) { return (a > b) ? a : b; }
static inline uint32_t lfs_min(uint32_t a, uint32_t b) { return (a < b) ? a : b; }

// Align to nearest multiple of a size
static inline uint32_t lfs_aligndown(uint32_t a, uint32_t alignment) {
    return a - (a % alignment);
}

static inline uint32_t lfs_alignup(uint32_t a, uint32_t alignment) {
    return lfs_aligndown(a + alignment-1, alignment);
}

static inline uint32_t lfs_npw2(uint32_t a) { return 32 - __builtin_clz(a-1); }
static inline uint32_t lfs_ctz(uint32_t a) { return __builtin_ctz(a); }
static inline uint32_t lfs_popc(uint32_t a) { return __builtin_popcount(a); }

// Find the sequence comparison of a and b, this is the distance
// between a and b ignoring overflow
static inline int lfs_scmp(uint32_t a, uint32_t b) {
    return (int)(unsigned)(a - b);
}

// Convert between 32-bit little-endian and native order
static inline uint32_t lfs_fromle32(uint32_t a) { return le32toh(a); }
static inline uint32_t lfs_tole32(uint32_t a) { return htole32(a); }
static inline uint32_t lfs_frombe32(uint32_t a) { return be32toh(a); }
static inline uint32_t lfs_tobe32(uint32_t a) { return htobe32(a); }

uint32_t lfs_crc(uint32_t crc, const void *buffer, size_t size);

// Allocate memory, only used if buffers are not provided to littlefs
// Note, memory must be 64-bit aligned
static inline void *lfs_malloc(size_t size) {
#ifndef LFS_NO_MALLOC
    return malloc(size);
#else
    (void)size;
    return NULL;
#endif
}

// Deallocate memory, only used if buffers are not provided to littlefs
static inline void lfs_free(void *p) {
#ifndef LFS_NO_MALLOC
    free(p);
#else
    (void)p;
#endif
}

#endif
