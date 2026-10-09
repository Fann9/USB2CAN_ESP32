#include "flash_files.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ESP-IDF VFS owns FATFS and wear levelling. Shell operations use POSIX calls.
 * Limit them to /flash; a virtual root exposes just that mount point. */
static bool allowed(const char *p)
{
    if (strcmp(p,FLASH_FILES_ROOT) && strncmp(p,FLASH_FILES_ROOT "/",sizeof(FLASH_FILES_ROOT))) return false;
    const char *s=p;
    while (*s) {
        while (*s=='/') ++s;
        const char *end=strchr(s,'/');
        size_t n=end ? (size_t)(end-s) : strlen(s);
        if (n==2 && !strncmp(s,"..",2)) return false;
        s+=n;
    }
    return true;
}
static int fs_open(const char *p,int flags)
{
    if (!allowed(p)) return -1;
    int mode=flags&CTSHELL_O_TRUNC ? O_WRONLY|O_CREAT|O_TRUNC :
        flags&CTSHELL_O_APPEND ? O_WRONLY|O_CREAT|O_APPEND : O_RDONLY;
    return open(p,mode,0600);
}
static int fs_read(int fd,void *p,uint32_t n) { return (int)read(fd,p,n); }
static int fs_write(int fd,const void *p,uint32_t n)
{
    uint32_t done=0;
    while (done<n) {
        ssize_t r=write(fd,(const char *)p+done,n-done);
        if (r<0 && errno==EINTR) continue;
        if (r<=0) return -1;
        done+=(uint32_t)r;
    }
    return (int)done;
}
static int fs_seek(int fd,long n,int whence) { return lseek(fd,n,whence)<0 ? -1 : 0; }
static int fs_stat(const char *p,ctshell_dirent_t *entry)
{
    if (!strcmp(p,"/")) { entry->type=CTSHELL_FS_TYPE_DIR; entry->size=0; return 0; }
    struct stat s;
    if (!allowed(p) || stat(p,&s)) return -1;
    entry->type=S_ISDIR(s.st_mode)?CTSHELL_FS_TYPE_DIR:CTSHELL_FS_TYPE_FILE;
    entry->size=(uint32_t)s.st_size;
    return 0;
}
typedef struct { DIR *dir; bool root_done; char path[CONFIG_CTSHELL_FS_PATH_MAX]; } shell_dir_t;
static int fs_opendir(const char *p,void **out)
{
    bool root=!strcmp(p,"/");
    if (!root && !allowed(p)) return -1;
    shell_dir_t *d=calloc(1,sizeof(*d));
    if (!d) return -1;
    if (strlen(p)>=sizeof(d->path)) { free(d); return -1; }
    strcpy(d->path,p);
    if (!root && !(d->dir=opendir(p))) { free(d); return -1; }
    *out=d; return 0;
}
static int fs_readdir(void *arg,ctshell_dirent_t *entry)
{
    shell_dir_t *d=arg;
    if (!d->dir) {
        if (d->root_done) return -1;
        d->root_done=true;
        strcpy(entry->name,"flash"); entry->type=CTSHELL_FS_TYPE_DIR; entry->size=0;
        return 0;
    }
    struct dirent *e;
    do { e=readdir(d->dir); } while (e && (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")));
    if (!e) return -1;
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
    if (snprintf(path,sizeof(path),"%s/%s",d->path,e->d_name)>=(int)sizeof(path)) return -1;
    size_t length=strlen(e->d_name);
    if (length>=sizeof(entry->name)) return -1;
    memcpy(entry->name,e->d_name,length+1);
    return fs_stat(path,entry);
}
static int fs_closedir(void *arg)
{
    shell_dir_t *d=arg;
    int result=d->dir?closedir(d->dir):0;
    free(d); return result;
}
static int fs_mkdir(const char *p) { return allowed(p)?mkdir(p,0700):-1; }
static int fs_remove(const char *p)
{
    struct stat s;
    if (!allowed(p) || !strcmp(p,FLASH_FILES_ROOT) || !strcmp(p,FLASH_FILES_ROOT "/") || stat(p,&s)) return -1;
    return S_ISDIR(s.st_mode)?rmdir(p):unlink(p);
}
static const ctshell_fs_drv_t driver={.open=fs_open,.close=close,.read=fs_read,.write=fs_write,
    .opendir=fs_opendir,.readdir=fs_readdir,.closedir=fs_closedir,.stat=fs_stat,
    .unlink=fs_remove,.mkdir=fs_mkdir,.lseek=fs_seek};
void flash_files_attach(ctshell_ctx_t *ctx)
{
    if (!flash_files_ready()) return;
    ctshell_fs_init(ctx,&driver);
    strcpy(ctx->cwd,FLASH_FILES_ROOT);
}
