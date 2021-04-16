#include <string.h>
#include <sys/errno.h>
#include "esp_types.h"
#include "esp_log.h"
#include "esp_compiler.h"
#include "esp_heap_caps.h"
#include "driver/sdmmc_types.h"
#include "sdmmc_cmd.h" // components/sdmmc/include/sdmmc_cmd.h
#include "littlefs/lfs.h"
#include "vfs/vfs_littlefs.h" 
#include "lfs_config.h"
#ifdef LFS_THREADSAFE
#include <sys/lock.h>
#endif

#ifndef SOC_SDMMC_HOST_SUPPORTED
#error "you can not enable SDMMC support when SOC does not have it"
#endif

static const char* TAG = "lfs_sdmmc";

typedef struct lfs_sdmmc_ctx_s {
#ifdef LFS_THREADSAFE
    _lock_t lock;
#endif
    sdmmc_card_t *card;
} lfs_sdmmc_ctx_t;

static enum lfs_error conv_err(esp_err_t e)
{
    switch(e) {
        case ESP_OK: return LFS_ERR_OK;
        case ESP_ERR_NO_MEM: return LFS_ERR_NOMEM;
        case ESP_ERR_INVALID_ARG: return LFS_ERR_INVAL;
        default: return LFS_ERR_IO;
    }
}

typedef struct {
    size_t start;
    size_t count;
} sectorpos_t;

static sectorpos_t get_sector(const struct lfs_config *c,
        lfs_block_t block, lfs_off_t off, lfs_size_t size)
{
    lfs_sdmmc_ctx_t *ctx = c->context;
    size_t sector_size = ctx->card->csd.sector_size;
    sectorpos_t s;
    s.start = ( block * c->block_size + off ) / sector_size;
    s.count = size / sector_size;
    //s.count = ( size + sector_size - 1 ) / csd.sector_size;
    return s;
}

// Read a region in a block. Negative error codes are propogated
// to the user.
int lfs_sdmmc_read(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size)
{
    lfs_sdmmc_ctx_t *ctx = c->context;
    sectorpos_t sec = get_sector(c, block, off, size);
    ESP_LOGV(TAG, "read block=%d offset=%d size=%d", (int) block, (int) off, (int) size);
    /* would like a DMA-capable buffer at hand to avoid sdmmc_read_sectors malloc'ing it */
    esp_err_t e = sdmmc_read_sectors(ctx->card, buffer, sec.start, sec.count);
    return conv_err(e);
}

// Program a region in a block. The block must have previously
// been erased. Negative error codes are propogated to the user.
// May return LFS_ERR_CORRUPT if the block should be considered bad.
int lfs_sdmmc_prog(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size)
{
    lfs_sdmmc_ctx_t *ctx = c->context;
    sectorpos_t sec = get_sector(c, block, off, size);
    ESP_LOGV(TAG, "prog block=%d offset=%d size=%d", (int) block, (int) off, (int) size);
    esp_err_t e = sdmmc_write_sectors(ctx->card, buffer, sec.start, sec.count);
    /* read it back and verify the write went OK? */
    return conv_err(e);
}

// Erase a block. A block must be erased before being programmed.
// The state of an erased block is undefined. Negative error codes
// are propogated to the user.
// May return LFS_ERR_CORRUPT if the block should be considered bad.
int lfs_sdmmc_erase(const struct lfs_config *c, lfs_block_t block)
{
    // microsd FTL deals with erasing
    (void) c;
    (void) block;
    ESP_LOGV(TAG, "erase block=%d", block);
    return 0;
}

// Sync the state of the underlying block device. Negative error codes
// are propogated to the user.
int lfs_sdmmc_sync(const struct lfs_config *c)
{
    (void) c;
    ESP_LOGV(TAG, "sync");
    return 0;
}

#ifdef LFS_THREADSAFE
int lfs_sdmmc_lock(const struct lfs_config *c)
{
    lfs_sdmmc_ctx_t *ctx = c->context;
    _lock_acquire(&ctx->lock);
    return 0;
}

int lfs_sdmmc_unlock(const struct lfs_config *c)
{
    lfs_sdmmc_ctx_t *ctx = c->context;
    _lock_release(&ctx->lock);
    return 0;
}
#endif

static size_t find_gcd(size_t a, size_t b)
{
    while (a != b) {
        if (a > b) a -= b;
        else b -= a;
    }
    return a;
}

struct lfs_config *lfs_setup_sdmmc(sdmmc_card_t *card)
{
    struct lfs_config *c = malloc(sizeof(*c));
    lfs_sdmmc_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!c || !ctx) {
        errno = ENOMEM;
        if (c) free(c);
        if (ctx) free(ctx);
        return NULL;
    }

    memset(c, 0, sizeof(*c));
    memset(ctx, 0, sizeof(*ctx));
    ctx->card = card;

    c->context = ctx;
    c->read = lfs_sdmmc_read;
    c->prog = lfs_sdmmc_prog;
    c->erase = lfs_sdmmc_erase;
    c->sync = lfs_sdmmc_sync;

#ifdef LFS_THREADSAFE
    c->lock = lfs_sdmmc_lock;
    c->unlock = lfs_sdmmc_unlock;
    _lock_init(&ctx->lock);
#endif

    size_t rd = 1 << card->csd.read_block_len;
    size_t pg = card->csd.sector_size;

    while (rd < 128) rd <<= 1;
    while (pg < 128) pg <<= 1;

    size_t ideal_block_size = 8192;
    size_t bs = rd * pg / find_gcd(rd, pg);
    while (bs < ideal_block_size) { bs <<= 1; }

    c->read_size = rd;
    c->prog_size = pg;
    c->block_size = bs;
    c->block_count = card->csd.capacity * card->csd.sector_size / bs;
    c->block_cycles = 347; // block-level wear leveling parameter
    c->cache_size = bs;
    c->lookahead_size = 256; // multiple of 8

    if (1) {
        c->read_buffer = heap_caps_malloc(c->cache_size, MALLOC_CAP_DMA);
        c->prog_buffer = heap_caps_malloc(c->cache_size, MALLOC_CAP_DMA);
        if (c->read_buffer) ESP_LOGI(TAG, "Alloc'd a DMA-capable read cache");
        if (c->prog_buffer) ESP_LOGI(TAG, "Alloc'd a DMA-capable write cache");
    }

    ESP_LOGI(TAG, "Read size: %d", (int) rd);
    ESP_LOGI(TAG, "Program size: %d", (int) pg);
    ESP_LOGI(TAG, "Block size: %d", (int) bs);
    ESP_LOGI(TAG, "Block count: %d", (int) c->block_count);

    return c;
}

void lfs_setup_sdmmc_cleanup(struct lfs_config *c)
{
#ifdef LFS_THREADSAFE
    lfs_sdmmc_ctx_t *ctx = c->context;
    _lock_close(&ctx->lock);
#endif
    free(c);
}

