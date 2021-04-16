#include <sys/errno.h>
#include <sys/lock.h>
#include "littlefs/lfs.h" 
#include "vfs/vfs_littlefs.h" 

#define MAX_FILES 64
// make sure 0 is invalid index and also don't confuse these fd's with stdout etc...
#define ENC(i) ((i)+64)
#define DEC(i) ((i)-64)
#define VALID(i) ((i) >= 0 && (i) < MAX_FILES)

static char used[MAX_FILES] = {0};
static struct lfs_file files[MAX_FILES] = {0};
static _lock_t lock = {0};
static char has_init = 0;

int esp_lfs_fd_new(void)
{
    if (!has_init) {
        has_init = 1;
        _lock_init(&lock); // possible to have 2 threads init the lock. oh well
    }
    _lock_acquire(&lock);
    for(int i=0; i<MAX_FILES; ++i) {
        if (!used[i]) {
            used[i] = 1;
            _lock_release(&lock);
            return ENC(i);
        }
    }
    _lock_release(&lock);
    errno = ENFILE;
    return -1;
}

struct lfs_file *esp_lfs_fd_file(int fd)
{
    int i = DEC(fd);
    if (VALID(i)) {
        return &files[i];
    }
    errno = EBADF;
    return NULL;
}

int esp_lfs_fd_close(int fd)
{
    int i = DEC(fd);
    if (has_init && VALID(i)) {
        _lock_acquire(&lock);
        used[i] = 0;
        _lock_release(&lock);
        return 0;
    }
    errno = EBADF;
    return -1;
}

