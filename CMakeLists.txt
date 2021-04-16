set(srcs
"littlefs/lfs.c"
"esp_littlefs/esp_lfs_fd.c"
"esp_littlefs/esp_lfs_mount.c"
"esp_littlefs/esp_lfs_sdmmc.c"
"esp_littlefs/esp_lfs_vfs.c"
"esp_littlefs/esp_lfs_crc.c"
)

idf_component_register(SRCS ${srcs}
INCLUDE_DIRS vfs esp_littlefs littlefs .
REQUIRES esp32 esp_common sdmmc vfs log newlib driver
)

