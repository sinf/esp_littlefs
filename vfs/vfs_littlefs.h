#ifndef _VFS_LITTLEFS_H
#define _VFS_LITTLEFS_H
#include "driver/sdmmc_defs.h"
#include "driver/sdmmc_types.h"
#include "driver/sdmmc_host.h"

/* esp_lfs_sdmmc.c: provide lfs_config that has function pointers to sdmmc_read/write */
struct lfs_config;
struct lfs_config *lfs_setup_sdmmc(sdmmc_card_t *card);
void lfs_setup_sdmmc_cleanup(struct lfs_config *c);

/* esp_lfs_fd.c: map integer <---> lfs_file_t */
struct lfs_file;
int esp_lfs_fd_new(void);
struct lfs_file *esp_lfs_fd_file(int fd);
int esp_lfs_fd_close(int fd);

#define LFS_FLAG_FORMAT 1

/* esp_lfs_vfs.c: mount fs and register VFS functions as backend to the POSIX API */
int esp_vfs_littlefs_mount(const char* base_path, const struct lfs_config *cfg, int flags);
int esp_vfs_littlefs_unmount(const char* base_path);

/* esp_lfs_mount.c: the one user-facing API */
esp_err_t vfs_littlefs_sdmmc_mount(
        const char* base_path,
        const sdmmc_host_t* host_config,
        const sdmmc_slot_config_t* slot_config,
        int flags);

#define vfs_littlefs_unmount esp_vfs_littlefs_unmount

#endif

