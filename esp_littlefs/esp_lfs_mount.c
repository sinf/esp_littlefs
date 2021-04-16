#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_types.h"
#include "esp_log.h"
#include "esp_compiler.h"
#include "driver/sdmmc_defs.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h" // components/sdmmc/include/sdmmc_cmd.h
#include "littlefs/lfs.h"
#include "vfs/vfs_littlefs.h" 

static const char *TAG = "lfs_mount";

static void call_host_deinit(const sdmmc_host_t *host_config)
{   
    if (host_config->flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
        host_config->deinit_p(host_config->slot);
    } else {
        host_config->deinit();
    }
}

static void print_card_infos(sdmmc_card_t *card)
{
    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
    printf("Max freq %u kHz\n", (unsigned) card->max_freq_khz);
    printf("log_bus_width: %d\n", (int) card->log_bus_width);
    printf("is_ddr: %d\n", (int) card->is_ddr);
    printf("csd.sector_size: %d\n", (int) card->csd.sector_size);
    printf("csd.read_block_len: %d\n", (int) card->csd.read_block_len);
    printf("csd.card_command_class: %d\n", (int) card->csd.card_command_class);
    printf("csd.tr_speed: %d\n", (int) card->csd.tr_speed);
    printf("cid.name: %.8s\n", card->cid.name);
    printf("cid.serial: %d\n", card->cid.serial);
    printf("scr.sd_spec: %d\n", card->scr.sd_spec);
    printf("scr.bus_width: %d\n", card->scr.bus_width);
}

esp_err_t vfs_littlefs_sdmmc_mount(
        const char* base_path,
        const sdmmc_host_t* host_config,
        const sdmmc_slot_config_t* slot_config,
        int flags)
{
    esp_err_t err;
    bool host_inited = false;
    sdmmc_card_t *card = malloc(sizeof(*card));
    struct lfs_config *cfg = NULL;

    if (!card) {
        errno = ENOMEM;
        return ESP_ERR_NO_MEM;
    }
    memset(card, 0, sizeof(*card)); // just in case

    err = (*host_config->init)();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "host init failed");
        return err;
    }
    host_inited = true; //deinit() needs to be called to revert the init

    // configure GPIO pins and SDMMC controller
    err = sdmmc_host_init_slot(host_config->slot, slot_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init_sdmmc_host failed");
        goto cleanup;
    }

    // probe and initialize card
    err = sdmmc_card_init(host_config, card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init failed. retrying...");
        err = sdmmc_card_init(host_config, card);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "sdmmc_card_init failed again");
            goto cleanup;
        }
    }

    print_card_infos(card);

    cfg = lfs_setup_sdmmc(card);
    if (!cfg) {
        ESP_LOGE(TAG, "lfs_setup_sdmmc fail");
        err = -1;
        goto cleanup;
    }

    err = esp_vfs_littlefs_mount(base_path, cfg, flags);
    if (err < 0) {
        ESP_LOGE(TAG, "esp_vfs_littlefs_mount fail");
        err = -1;
        goto cleanup;
    }

    return ESP_OK;
cleanup:
    if (host_inited) { call_host_deinit(host_config); }
    if (card) { free(card); }
    if (cfg) { lfs_setup_sdmmc_cleanup(cfg); }
    return err;
}

