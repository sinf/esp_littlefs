COMPONENT_ADD_INCLUDEDIRS := vfs
COMPONENT_PRIV_INCLUDEDIRS := .
COMPONENT_SRCDIRS := vfs littlefs esp_littlefs

CFLAGS += -DLFS_CONFIG=lfs_config.h
CFLAGS += -DLFS_YES_TRACE
CFLAGS += -std=c99 -Wall -pedantic
CFLAGS += -Wextra -Wshadow -Wjump-misses-init -Wundef

