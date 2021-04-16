#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_vfs.h"
#include "esp_log.h"
#include "littlefs/lfs.h"
#include "vfs/vfs_littlefs.h"

static const char *TAG = "lfs_vfs";

typedef struct vlfs_ctx_s {
    lfs_t lfs; // first member has to be lfs
} vlfs_ctx_t;

typedef struct vlfs_dir_s {
    DIR newlib_d;
    lfs_dir_t lfs_d;
    struct dirent de;
} vlfs_dir_t;

#define vlfs_file_p(ctx, fd) esp_lfs_fd_file(fd)

/* translate LFS error to stdlib */
static int vlfs_tr_error(enum lfs_error e) {
    switch(e) {
        case LFS_ERR_OK: return 0;
        case LFS_ERR_CORRUPT: return EIO; // ?
        case LFS_ERR_IO: return EIO;
        case LFS_ERR_NOENT: return ENOENT;
        case LFS_ERR_EXIST: return EEXIST;
        case LFS_ERR_NOTDIR: return ENOTDIR;
        case LFS_ERR_ISDIR: return EISDIR;
        case LFS_ERR_NOTEMPTY: return ENOTEMPTY;
        case LFS_ERR_BADF: return EBADF;
        case LFS_ERR_FBIG: return EFBIG;
        case LFS_ERR_INVAL: return EINVAL;
        case LFS_ERR_NOSPC: return ENOSPC;
        case LFS_ERR_NOMEM: return ENOMEM;
        case LFS_ERR_NOATTR: return EIO; // ?
        case LFS_ERR_NAMETOOLONG: return ENAMETOOLONG;
        default: return 2500; // now what?
    }
}

static int vlfs_set_errno(int e) {
    if (e < 0) {
        errno = vlfs_tr_error(e);
        return -1;
    }
    return 0;
}

static int vlfs_open(void *ctx, const char *path, int flags, int mode)
{
    int lflags = 0;
    switch(flags & O_ACCMODE) {
        case O_RDONLY: lflags = LFS_O_RDONLY; break;
        case O_WRONLY: lflags = LFS_O_WRONLY; break;
        case O_RDWR: lflags = LFS_O_RDWR; break;
        default: errno = EINVAL; return -1;
    }
    const int fd = esp_lfs_fd_new();
    if (fd < 0) {
        ESP_LOGE(TAG, "open: no free file descriptors");
        errno = ENFILE;
        return -1;
    }
    if (flags & O_CREAT) lflags |= LFS_O_CREAT;
    if (flags & O_EXCL) lflags |= LFS_O_EXCL;
    if (flags & O_TRUNC) lflags |= LFS_O_TRUNC;
    if (flags & O_APPEND) lflags |= LFS_O_APPEND;
    ESP_LOGI(TAG, "open(path=%s, flags=0x%x, mode=0x%0x) lflags=0x%x", path, flags, mode, lflags);
    int err = lfs_file_open(ctx, vlfs_file_p(ctx, fd), path, lflags);
    if (err) {
        errno = vlfs_tr_error(err);
        return -1;
    }
    return fd;
}

static int vlfs_close(void *ctx, int fd)
{
    lfs_file_t *f = vlfs_file_p(ctx, fd);
    if (f) {
        int err = lfs_file_close(ctx, f);
        memset(f, 0, sizeof(*f));
        return vlfs_set_errno(err);
    }
    return -1;
}

static int vlfs_fsync(void *ctx, int fd)
{
    lfs_file_t *f = vlfs_file_p(ctx, fd);
    return f ? vlfs_set_errno(lfs_file_sync(ctx, f)) : -1;
}

static ssize_t vlfs_read(void *ctx, int fd, void *dst, size_t size)
{
    lfs_file_t *f = vlfs_file_p(ctx, fd); if (!f) { return -1; }
    ssize_t bytes = lfs_file_read(ctx, f, dst, size);
    if (bytes < 0) { errno = vlfs_tr_error(bytes); return -1; }
    return bytes;
}

#ifndef LFS_READONLY
static ssize_t vlfs_write(void *ctx, int fd, const void *src, size_t size)
{
    lfs_file_t *f = vlfs_file_p(ctx, fd); if (!f) { return -1; }
    ssize_t bytes = lfs_file_write(ctx, f, src, size);
    if (bytes < 0) { errno = vlfs_tr_error(bytes); return -1; }
    return bytes;
}
#endif

#if 0
static off_t vlfs_tell(void *ctx, int fd)
{
    lfs_file_t *f = vlfs_file_p(ctx, fd); if (!f) { return -1; }
    lfs_soff_t pos = lfs_file_tell(ctx, f);
    if (pos < 0) {
        errno = vlfs_tr_error(pos);
        return -1;
    }
    return pos;
}
#endif

off_t vlfs_lseek(void *ctx, int fd, off_t offset, int mode)
{
    lfs_file_t *f = vlfs_file_p(ctx, fd); if (!f) { return -1; }
    int whence;
    switch(mode) {
        case SEEK_SET: whence = LFS_SEEK_SET; break;
        case SEEK_CUR: whence = LFS_SEEK_CUR; break;
        case SEEK_END: whence = LFS_SEEK_END; break;
        default: errno = EINVAL; return -1;
    }
    lfs_soff_t new_pos = lfs_file_seek(ctx, f, offset, whence);
    if (new_pos < 0) {
        errno = vlfs_tr_error(new_pos);
        return -1;
    }
    return new_pos;
}

static void fill_info(struct lfs_info *info, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_size = info->size;
    st->st_ino = 123;
    st->st_nlink = 1;
    //st->st_dev = 1234;
    //st->st_blksize = TODO;
    //st->st_blocks = TODO;
    st->st_mode = S_IRWXU | S_IRWXG | S_IRWXO;
    switch (info->type) {
        case LFS_TYPE_REG: st->st_mode |= S_IFREG; break;
        case LFS_TYPE_DIR: st->st_mode |= S_IFDIR; break;
        default: break;
    }
}

int vlfs_stat(void *ctx, const char *path, struct stat *st)
{
    struct lfs_info info;
    int err = lfs_stat(ctx, path, &info);
    if (err < 0) {
        errno = vlfs_tr_error(err);
        return -1;
    }
    fill_info(&info, st);
    return 0;
}

int vlfs_unlink(void *ctx, const char *path)
{
    /* should we bother checking if the file is open?
     * is this called with or without the mount point prefix? */
    return vlfs_set_errno(lfs_remove(ctx, path));
}

DIR* vlfs_opendir(void *ctx, const char *path)
{
    vlfs_dir_t *d = malloc(sizeof *d);
    if (!d) {
        errno = ENOMEM;
        return NULL;
    }
    int err = lfs_dir_open(ctx, &d->lfs_d, path);
    if (err < 0) {
        free(d);
        errno = vlfs_tr_error(err);
        return NULL;
    }
    return (DIR*) d;
}

struct dirent* vlfs_readdir(void *ctx, DIR *newlib_d)
{
    vlfs_dir_t *d = (vlfs_dir_t*) newlib_d;
    struct lfs_info info;
    int err = lfs_dir_read(ctx, &d->lfs_d, &info);
    if (err < 0) {
        errno = vlfs_tr_error(err);
        return NULL;
    }
    if (err == 0) {
        /* end of directory */
        return NULL;
    }
    d->de.d_ino = 0;
    d->de.d_type = info.type == LFS_TYPE_REG ? DT_REG : DT_DIR;
    strncpy(d->de.d_name, info.name, LFS_NAME_MAX < 255 ? LFS_NAME_MAX : 255);
    d->de.d_name[255] = '\0';
    return &d->de;
}

int vlfs_mkdir(void *ctx, const char *path, mode_t mode)
{
    (void) mode;
    return vlfs_set_errno(lfs_mkdir(ctx, path));
}

int vlfs_closedir(void *ctx, DIR* newlib_d)
{
    vlfs_dir_t *d = (vlfs_dir_t*) newlib_d;
    int err = lfs_dir_close(ctx, &d->lfs_d);
    free(d);
    return vlfs_set_errno(err);
}

static const esp_vfs_t the_littlefs_vfs_funcs = {
    .flags = ESP_VFS_FLAG_CONTEXT_PTR,
    .open_p = vlfs_open,
    .close_p = vlfs_close,
    .fsync_p = vlfs_fsync,
    .read_p = vlfs_read,
    .lseek_p = vlfs_lseek,

#if 0 && defined(CONFIG_VFS_SUPPORT_DIR)
    .truncate_p = vlfs_truncate, /* inside wrong ifdef in esp_vfs.h */
#endif

#ifndef LFS_READONLY
    .write_p = vlfs_write,
#ifdef CONFIG_VFS_SUPPORT_DIR
    .unlink_p = vlfs_unlink, /* inside wrong ifdef in esp_vfs.h */
#endif
#endif // LFS_READONLY

#ifdef CONFIG_VFS_SUPPORT_DIR
    .stat_p = vlfs_stat, /* inside wrong ifdef in esp_vfs.h */
    // .link_p = vlfs_link, /* inside wrong ifdef in esp_vfs.h */
    //.rename_p = vlfs_rename,
    .opendir_p = vlfs_opendir,
    .closedir_p = vlfs_closedir,
    .readdir_p = vlfs_readdir,
#ifndef LFS_READONLY
    .mkdir_p = vlfs_mkdir,
    .rmdir_p = vlfs_unlink,
#endif
//    .readdir_r_p = vlfs_readdir_r,
//    .telldir_p = vlfs_telldir,
//    .seekdir_p = vlfs_seekdir,
//    .access_p = vlfs_access,
//    .utime_p = vlfs_utime,
#endif
/* tell() not in vfs? */
//    .fcntl_p = vlfs_fcntl,
//    .ioctl_p = vlfs_ioctl,
    /* no TERMIOS */
    /* no SELECT */
};


/* only support one mount at a time */
static struct vlfs_ctx_s my_ctx = {0};
static int has_mounted = 0;

int esp_vfs_littlefs_mount(const char* base_path, const struct lfs_config *cfg, int flags)
{
    if (has_mounted) {
        ESP_LOGE(TAG, "can only support one mount at a time");
        return -1;
    }

    if (flags & LFS_FLAG_FORMAT) {
        ESP_LOGI(TAG, "formatting filesystem...");
        int err = lfs_format(&my_ctx.lfs, cfg);
        if (err < 0) {
            ESP_LOGE(TAG, "lfs_format fail");
            return -1;
        }
    }

    ESP_LOGI(TAG, "mount %s", base_path);
    int err = lfs_mount(&my_ctx.lfs, cfg);
    if (err < 0) {
        ESP_LOGE(TAG, "mount failed, error %d", err);
        return -1;
    }
    err = esp_vfs_register(base_path, &the_littlefs_vfs_funcs, &my_ctx);
    if (err) {
        ESP_LOGE(TAG, "esp_vfs_register failed, error %d", err);
        lfs_unmount(&my_ctx.lfs);
        return -1;
    }
    has_mounted = 1;
    return 0;
}

int esp_vfs_littlefs_unmount(const char* base_path)
{
    ESP_LOGI(TAG, "unmount %s", base_path);
    int err = lfs_unmount(&my_ctx.lfs);
    if (err < 0) {
        ESP_LOGE(TAG, "unmount failed, error=%d", err);
        return -1;
    }
    has_mounted = 0;
    return 0;
}

