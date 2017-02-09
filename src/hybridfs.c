
#include "params.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"

//  All the paths I see are relative to the root of the mounted
//  filesystem.  In order to get to the underlying filesystem, I need to
//  have the mountpoint.  I'll save it away early on in main(), and then
//  whenever I need a path for something I'll call this to construct
//  it.

struct mutex_node* hb_retMutex(const char *path);

struct mutex_node *mutex_head = NULL;
struct ulog_file_node *ulog_file_head = NULL;

static void hb_fullpath_ssd(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, HB_DATA->dir_ssd);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will be break here
}

static void hb_fullpath_hdd(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, HB_DATA->dir_hdd);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will be break here
}

//// Parameter 'rpath' is relative path.
//// It is directory path, and doesn't include '/' at last.
void hb_recurMig (char *fpath_for_ssd, char *fpath_for_hdd, char *rpath)
{
    struct stat st;

    DIR *dir;
    struct dirent *ent;

    mkdir(fpath_for_hdd, S_IFDIR | 0775);

    if ( (dir = opendir(fpath_for_ssd)) != NULL ) {
        while ( (ent = readdir(dir)) != NULL ) {
            char fpath_file[PATH_MAX];
            char spath_file[PATH_MAX] = "/";
            char rpath_file[PATH_MAX] = {0,};
            char target_path[PATH_MAX] = {0,};
            int ret;

            memset(fpath_file, 0, PATH_MAX);

            memcpy(spath_file + 1, ent->d_name, strlen(ent->d_name));

            memcpy(fpath_file, fpath_for_ssd, strlen(fpath_for_ssd));
            memcpy(fpath_file + strlen(fpath_file), spath_file, strlen(spath_file)); // full path of current file
            
            memcpy(rpath_file, rpath, strlen(rpath));       //// relative path of file
            memcpy(rpath_file + strlen(rpath_file), spath_file, strlen(spath_file));    //// doesn't include '/' in last.

            ret = lstat(fpath_file, &st);

            if ( strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 )
                continue;

            memcpy(target_path, fpath_for_hdd, strlen(fpath_for_hdd));
            memcpy(target_path + strlen(target_path), spath_file, strlen(spath_file));  // full path of hdd side's path

            if (S_ISDIR(st.st_mode)) {
                hb_recurMig(fpath_file, target_path, rpath_file);    // case : fpath_file is directory
            }

            if (st.st_size >= HB_DATA->mig_threshold) {
                //pthread_mutex_lock(&(hb_retMutex(rpath_file)->wrt));    /////// here to fix

                rename(fpath_file, target_path);

                //pthread_mutex_unlock(&(hb_retMutex(rpath_file)->wrt));    ///// here to fix
                //printf("In here, ssd : %s, hdd : %s\n",fpath_file, target_path);
            }

        }
    }

}

int hb_isroot(const char *path)
{
    if (strcmp(path, "/") == 0)
        return 1;
    else
        return 0;
}

int hb_findMutex(const char *path)
{
    struct mutex_node *ptr = mutex_head;

    if (ptr == NULL)
        return 0;

    while (ptr != NULL) {
        if (strcmp(ptr->rel_path, path) == 0)
            return 1;

        ptr = ptr->link;
    }
    return 0;
}

void hb_createMutex(const char *path, ino_t fi)
{
    struct mutex_node *node, *ptr = mutex_head;

    node = (struct mutex_node*) malloc(sizeof(struct mutex_node));
    strcpy(node->rel_path, path);
    node->inode = fi;   //// fi : file index
    pthread_mutex_init(&(node->mutex), NULL);
    pthread_mutex_init(&(node->wrt), NULL);
    node->read_count = 0;
    node->write_count = 0;
    node->link = NULL;

    if (ptr == NULL) {
        mutex_head = node;
    }
    else {
        while (ptr->link != NULL)
            ptr = ptr->link;
        ptr->link = node;
    }
}

struct mutex_node* hb_retMutex(const char *path)
{
    struct mutex_node *ptr = mutex_head;

    if (ptr == NULL)
        return NULL;

    while (ptr != NULL) {
        if (strcmp(ptr->rel_path, path) == 0)
            return ptr;

        ptr = ptr->link;
    }
    return NULL;
}

void hb_printMutex()
{
    struct mutex_node *ptr = mutex_head;
    int count = 0;
    printf("---Mutex print starts---\n\n");
    while (ptr != NULL) {
        printf("[%d node]\n",count++);
        printf("relative path : %s, ",ptr->rel_path);
        printf("inode : %lu, ",ptr->inode);
        printf("read count : %d\n", ptr->read_count);
        ptr = ptr->link;
    }
    printf("\n\n");
}

void hb_freeMutex()
{
    struct mutex_node *ptr;
    if (mutex_head == NULL)
        return;

    while (mutex_head->link != NULL) {
        ptr = mutex_head;
        while(ptr->link->link != NULL) {
            ptr = ptr->link;
        }
        free(ptr->link);
        ptr->link = NULL;
    }
    free(mutex_head);
}

int hb_createUfile(const char *path)
{
    struct ulog_file_node *node, *ptr = ulog_file_head;
    struct ulog_node *head;
    node = (struct ulog_file_node*)malloc(sizeof(struct ulog_file_node));
    if (node == NULL)
        return 0;

    strcpy(node->rel_path, path);
    node->link = NULL;

    head = (struct ulog_node*)malloc(sizeof(struct ulog_node));
    if (head == NULL)
        return 0;
    head->size = -1;
    head->offset = 0;
    head->link = NULL;
    
    node->ulog = head;

    if (ptr == NULL) {
        ulog_file_head = node;
        return 1;
    }

    while(ptr->link != NULL) {
        ptr = ptr->link;
    }
    ptr->link = node;

    return 1;
}

struct ulog_node* hb_findUfile(const char *path) 
{
    struct ulog_file_node *ptr = ulog_file_head;

    while (ptr != NULL) {
        if (strcmp(ptr->rel_path, path) == 0)
            return ptr->ulog;
        ptr = ptr->link;
    }
    return NULL;
}

int hb_deleteUfile(const char *path)
{
    struct ulog_file_node *ptr = ulog_file_head, *ptr_link;

    if (ptr == NULL) {
        return 0;
    }

    else if (ptr->link == NULL) {
        if (strcmp(ptr->rel_path, path) == 0) {
            free (ptr);
            ulog_file_head = NULL;
            return 1;
        }
        else {
            return 0;
        }
    }

    ptr_link = ptr->link;
    while (ptr_link != NULL) {
        if (strcmp(ptr->rel_path, path) == 0) {
            ptr->link = ptr_link->link;
            free(ptr_link);
            return 1;
        }
        ptr = ptr->link;
        ptr_link = ptr->link;
    }

    return 0;
}

int hb_insertUlog(struct ulog_node *log_head, int size, int offset) 
{
    struct ulog_node *node, *ptr = log_head;

    if (ptr->size == -1) {
        ptr->size = size;
        ptr->offset = offset;
        return 1;
    }

    node = (struct ulog_node*)malloc(sizeof(struct ulog_node));
    node->size = size;
    node->offset = offset;
    node->link = NULL;

    while (ptr->link != NULL)
        ptr = ptr->link;

    ptr->link = node;

    return 1;
}

int hb_finishedUlog(struct ulog_node *log_head) 
{
    struct ulog_node *ptr = log_head;

    if (ptr == NULL)
        return 0;

    while (ptr != NULL) {
        if (ptr->size != 0)
            return 0;
        ptr = ptr->link;
    }

    return 1;
}

void hb_freeUlog(struct ulog_node *log_head)
{
    struct ulog_node *ptr = log_head;
    if (ptr == NULL)
        return;

    while (ptr->link != NULL) {
        ptr = log_head;
        while( ptr->link->link != NULL) {
            ptr = ptr->link;
        }
        free(ptr->link);
        ptr->link = NULL;
    }

    free(log_head);
}

void hb_printUlog(struct ulog_node *log_head)
{
    struct ulog_node *ptr = log_head;

    printf("--- updated logs here ---\n");
    if (ptr == NULL)
        printf("  There's no log\n");
    while (ptr != NULL) {
        printf("size : %d, offset : %d\n",ptr->size, ptr->offset);
        ptr = ptr->link;
    }
    
}

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come from /usr/include/fuse.h
//
/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int hb_getattr(const char *path, struct stat *statbuf)
{
    int retstat;
    char fpath_ssd[PATH_MAX];   // Basically, fpath is a full path of ssd
    char fpath_hdd[PATH_MAX];   // full path of hdd.

    ////log_msg("\nhb_getattr(path=\"%s\", statbuf=0x%08x)\n", path, statbuf);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    retstat = log_syscall("lstat", lstat(fpath_ssd, statbuf), 0);

    if (retstat != 0) {
        retstat = log_syscall("lstat", lstat(fpath_hdd, statbuf), 0);
    }
    //printf("[getattr] %s's size is : %d\n", path, statbuf->st_size);
    ////log_stat(statbuf);
    
    //// Initiate files' mutex
    ////// It needs to be implemented recursively
    /*if (!S_ISDIR(statbuf->st_mode)) {
        if (!hb_findMutex(path)) {
            printf("[getattr] path : %s, inode : %lu\n", path, statbuf->st_ino);    //// inode may be not used
            hb_createMutex(path, statbuf->st_ino);
            //hb_printMutex();
        }
    }
    */
    //// Find large file in directories recursively.
    //// And then, migrate to hdd.
    
    /*
    if (!hb_isroot(path) && S_ISDIR(statbuf->st_mode)) {
        //printf("%s\n",fpath_ssd);
        hb_recurMig(fpath_ssd, fpath_hdd, "");
    }
    */

    /*
    if (statbuf->st_size >= HB_DATA->mig_threshold) {
        //strcat(fpath_hdd, path);
        //memcpy(fpath_hdd + len_fpath_hdd, path, strlen(path));
        //printf("[getattr] %s\n", fpath_hdd);
        //pthread_mutex_lock(&(hb_retMutex(path)->wrt));    ///////// here to fix
        
        rename(fpath_ssd, fpath_hdd);   //// CS

        //pthread_mutex_unlock(&(hb_retMutex(path)->wrt));  ///////// here to fix
        //symlink(fpath_hdd, fpath_ssd);    ///// have to restore
    }
    */

    return retstat;
}

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */
// Note the system readlink() will truncate and lose the terminating
// null.  So, the size passed to to the system readlink() must be one
// less than the size passed to hb_readlink()
// hb_readlink() code by Bernardo F Costa (thanks!)
int hb_readlink(const char *path, char *link, size_t size)
{
    int retstat;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    
    ////log_msg("hb_readlink(path=\"%s\", link=\"%s\", size=%d)\n", path, link, size);
    hb_fullpath_ssd(fpath_ssd, path);

    retstat = log_syscall("fpath", readlink(fpath_ssd, link, size - 1), 0);
    if (retstat >= 0) {
        link[retstat] = '\0';
        retstat = 0;
    }
    else {
        hb_fullpath_hdd(fpath_hdd, path);
        retstat = log_syscall("fpath", readlink(fpath_hdd, link, size - 1), 0);
        if (retstat >= 0) {
            link[retstat] = '\0';
            retstat = 0;
        }
    }
    
    return retstat;
}

/** Create a file node
 *
 * There is no create() operation, mknod() will be called for
 * creation of all non-directory, non-symlink nodes.
 */
// shouldn't that comment be "if" there is no.... ?
int hb_mknod(const char *path, mode_t mode, dev_t dev)
{
    int retstat;
    char fpath_ssd[PATH_MAX];
    //char fpath_hdd[PATH_MAX];
    
    //log_msg("\nhb_mknod(path=\"%s\", mode=0%3o, dev=%lld)\n", path, mode, dev);
    hb_fullpath_ssd(fpath_ssd, path);
    
    // On Linux this could just be 'mknod(path, mode, dev)' but this
    // tries to be be more portable by honoring the quote in the Linux
    // mknod man page stating the only portable use of mknod() is to
    // make a fifo, but saying it should never actually be used for
    // that.
    if (S_ISREG(mode)) {
        retstat = log_syscall("open", open(fpath_ssd, O_CREAT | O_EXCL | O_WRONLY, mode), 0);
        if (retstat >= 0)
            retstat = log_syscall("close", close(retstat), 0);
    } 
    else {
        if (S_ISFIFO(mode))
            retstat = log_syscall("mkfifo", mkfifo(fpath_ssd, mode), 0);
        else
            retstat = log_syscall("mknod", mknod(fpath_ssd, mode, dev), 0);
    }
    
    return retstat;
}

/** Create a directory */
int hb_mkdir(const char *path, mode_t mode)
{
    char fpath_ssd[PATH_MAX];
    
    ////log_msg("\nhb_mkdir(path=\"%s\", mode=0%3o)\n", path, mode);
    hb_fullpath_ssd(fpath_ssd, path);

    return log_syscall("mkdir", mkdir(fpath_ssd, mode), 0);
}

/** Remove a file */
int hb_unlink(const char *path)
{
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    int ret;
    
    //log_msg("hb_unlink(path=\"%s\")\n", path);
    hb_fullpath_ssd(fpath_ssd, path);
    ret = log_syscall("unlink", unlink(fpath_ssd), 0);

    if (ret != 0) {
        hb_fullpath_hdd(fpath_hdd, path);
        ret = log_syscall("unlink", unlink(fpath_hdd), 0);
    }

    return ret;
}

/** Remove a directory */
int hb_rmdir(const char *path)
{
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    int ret;
    
    ////log_msg("hb_rmdir(path=\"%s\")\n", path);
    hb_fullpath_ssd(fpath_ssd, path);
    ret = log_syscall("rmdir", rmdir(fpath_ssd), 0);

    if(ret != 0) {
        hb_fullpath_hdd(fpath_hdd, path);
        ret = log_syscall("rmdir", rmdir(fpath_hdd), 0);
    }

    return ret;
}

/** Create a symbolic link */
// The parameters here are a little bit confusing, but do correspond
// to the symlink() system call.  The 'path' is where the link points,
// while the 'link' is the link itself.  So we need to leave the path
// unaltered, but insert the link into the mounted directory.
int hb_symlink(const char *path, const char *link)
{
    char flink_ssd[PATH_MAX];
    char flink_hdd[PATH_MAX];
    int ret;
    
    ////log_msg("\nhb_symlink(path=\"%s\", link=\"%s\")\n", path, link);
    hb_fullpath_ssd(flink_ssd, link);
    ret = log_syscall("symlink", symlink(path, flink_ssd), 0);

    if (ret != 0) {
        hb_fullpath_hdd(flink_hdd, link);
        ret = log_syscall("symlink", symlink(path, flink_hdd), 0);
    }
    return ret;
}

/** Rename a file */
// both path and newpath are fs-relative
int hb_rename(const char *path, const char *newpath)
{
    int ret;
    char fpath_ssd[PATH_MAX];
    char fnewpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    char fnewpath_hdd[PATH_MAX];
    
    ////log_msg("\nhb_rename(fpath=\"%s\", newpath=\"%s\")\n", path, newpath);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_ssd(fnewpath_ssd, newpath);

    ret = log_syscall("rename", rename(fpath_ssd, fnewpath_ssd), 0);

    if (ret != 0) {
        hb_fullpath_hdd(fpath_hdd, path);
        hb_fullpath_hdd(fnewpath_hdd, newpath);
        
        ret = log_syscall("rename", rename(fpath_hdd, fnewpath_hdd), 0);
    }

    return ret;
}

/** Create a hard link to a file */
int hb_link(const char *path, const char *newpath)
{
    char fpath_ssd[PATH_MAX], fnewpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX], fnewpath_hdd[PATH_MAX];
    int ret;
    
    //log_msg("\nhb_link(path=\"%s\", newpath=\"%s\")\n", path, newpath);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_ssd(fnewpath_ssd, newpath);
    ret = log_syscall("link", link(fpath_ssd, fnewpath_ssd), 0);

    if (ret != 0) {
        hb_fullpath_hdd(fpath_hdd, path);
        hb_fullpath_hdd(fnewpath_hdd, newpath);
        ret = log_syscall("link", link(fpath_hdd, fnewpath_hdd), 0);
    }

    return ret;
}

/** Change the permission bits of a file */
int hb_chmod(const char *path, mode_t mode)
{
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    int ret;
    
    ////log_msg("\nhb_chmod(fpath=\"%s\", mode=0%03o)\n", path, mode);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    if ( (ret = log_syscall("chmod", chmod(fpath_ssd, mode), 0)) != 0)
        return log_syscall("chmod", chmod(fpath_hdd, mode), 0);

    return ret;
}

/** Change the owner and group of a file */
int hb_chown(const char *path, uid_t uid, gid_t gid)
  
{
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    int ret;
    
    ////log_msg("\nhb_chown(path=\"%s\", uid=%d, gid=%d)\n", path, uid, gid);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    if ( (ret = log_syscall("chown", chown(fpath_ssd, uid, gid), 0)) != 0)
        return log_syscall("chown", chown(fpath_hdd, uid, gid), 0);

    return ret;
}

/** Change the size of a file */
int hb_truncate(const char *path, off_t newsize)
{
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    int ret;
    
    ////log_msg("\nhb_truncate(path=\"%s\", newsize=%lld)\n", path, newsize);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    if ( (ret = log_syscall("truncate", truncate(fpath_ssd, newsize), 0)) != 0)
        return log_syscall("truncate", truncate(fpath_hdd, newsize), 0);

    return ret;
}

/** Change the access and/or modification times of a file */
/* note -- I'll want to change this as soon as 2.6 is in debian testing */
int hb_utime(const char *path, struct utimbuf *ubuf)
{
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    int ret;
    
    ////log_msg("\nhb_utime(path=\"%s\", ubuf=0x%08x)\n", path, ubuf);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    if ( (ret = log_syscall("utime", utime(fpath_ssd, ubuf), 0)) != 0)
        return log_syscall("utime", utime(fpath_hdd, ubuf), 0);

    return ret;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int hb_open(const char *path, struct fuse_file_info *fi)
{
    int retstat, ret=0;
    int fd;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    struct stat st;
    struct mutex_node *temp;
    
    //log_msg("\nhb_open(path\"%s\", fi=0x%08x)\n", path, fi);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    retstat = lstat(fpath_ssd, &st);
    if (retstat != 0)
        retstat = lstat(fpath_hdd, &st);
    if (retstat != 0)
        printf("[Error] no exist\n");

    // if the open call succeeds, my retstat is the file descriptor,
    // else it's -errno.  I'm making sure that in that case the saved
    // file descriptor is exactly -1.
    fd = log_syscall("open", open(fpath_ssd, fi->flags), 0);
    if (fd < 0) {
        fd = log_syscall("open", open(fpath_hdd, fi->flags), 0);
        //retstat = log_error("open");
    }
    
    if (fd >= 0) {
        int mode_value = fi->flags & 0xFFFF;
        //// Initiate files' mutex
        if (!hb_findMutex(path)) {
            printf("[hb_open] creation path : %s tid : %lu, mode_value : %d\n", path, pthread_self(), mode_value);
            hb_createMutex(path, st.st_ino);    //// Case of first open

        }
        if (mode_value == 32768) {
            hb_retMutex(path)->read_count++;
            printf("[hb_open] path : %s, tid : %lu, read_count : %d\n", path, pthread_self(), hb_retMutex(path)->read_count);
        }

        //// 33793 for append mode
        //// 32800 for client.c using mode
        else if (mode_value == 32769 || mode_value == 32770 || mode_value == 33793 || mode_value == 32800) {
            hb_retMutex(path)->write_count++;
            printf("[hb_open] path : %s, tid : %lu, write_count : %d\n", path, pthread_self(), hb_retMutex(path)->write_count);
        }
        /*
        else {
            int mode_value = fi->flags & 0xFFFF;

                //printf("[hb_open read CS without create] mode %d\n",mode_value);
                /////// read case
                pthread_mutex_lock(&(hb_retMutex(path)->mutex));
                hb_retMutex(path)->read_count++;
                if (hb_retMutex(path)->read_count == 1)
                    pthread_mutex_lock(&(hb_retMutex(path)->wrt));
                pthread_mutex_unlock(&(hb_retMutex(path)->mutex));

                temp = hb_retMutex(path);
                printf("(read)name : %s\n(read)read_count : %d\n", temp->rel_path, temp->read_count);

                ///// From here, CS for read begins.
            }
            //else if (fi->flags & 0xFFFF == 0x8001 || fi->flags & 0xFFFF == 0x8002) {
                /////// write case
                pthread_mutex_lock(&(hb_retMutex(path)->wrt));

                printf("[hb_open write CS without create] mode %d\n", mode_value);
                temp = hb_retMutex(path);
                printf("(write)name : %s\n(write)read_count : %d\n", temp->rel_path, temp->read_count);

                ///// From here, CS for write begins.
            }
        }
        */

    }
	
    fi->fh = fd;    //// It is file handler (fd)
    //log_fi(fi);
    
    return ret;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
// I don't fully understand the documentation above -- it doesn't
// match the documentation for the read() system call which says it
// can return with anything up to the amount of data requested. nor
// with the fusexmp code which returns the amount of data also
// returned by read.
int hb_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = -1, ret;
    struct stat st;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    struct mutex_node *temp;
    
    //log_msg("\nhb_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n", path, buf, size, offset, fi);
    // no need to get fpath on this one, since I work from fi->fh not the path
    //log_fi(fi);
    //int ret = symlink("/home/taeugi323/Desktop/hybridFS/hdd/large_file.txt", "/home/taeugi323/Desktop/hybridFS/workspace/large_file.txt");
    //printf("[read] %d\n", ret);
    /*
        DIR *dir;
        struct dirent *ent;
        //cihar temp1[PATH_MAX] = "/.";
        //char temp2[PATH_MAX] = "/..";

        if ( (dir = opendir(HB_DATA->dir_hdd)) != NULL ) {
            while ( (ent = readdir(dir)) != NULL ) {
                char path_hdd[PATH_MAX];
                char link[PATH_MAX] = "/";
                char link_path[PATH_MAX] = "/home/taeugi323/Desktop/hybridFS";
                //char link_path[PATH_MAX] = "workspace";
                int ret=0;

                memcpy(link+1, ent->d_name, strlen(ent->d_name));
                hb_fullpath_hdd(path_hdd, link);

                if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                    printf("[read] file : %s, link : %s, ", path_hdd, link);
                    memcpy(link_path+strlen(link_path), link, strlen(link));

                    ret = symlink(path_hdd, link_path);
                    char cwd[1024];
                    getcwd(cwd, sizeof(cwd));
                    printf("link_path : %s, ret : %d, current dir : %s\n",link_path, ret, cwd);
                }

            }
        }
    */
    
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);
    //printf("[read] %s %s\n", fpath_ssd, fpath_hdd);
    //
    retstat = lstat(fpath_ssd, &st);
    if (retstat != 0)
        retstat = lstat(fpath_hdd, &st);
    if (retstat != 0)
        printf("[Error] no exist\n");

    /*
    pthread_mutex_lock(&(hb_retMutex(path)->mutex));
    hb_retMutex(path)->read_count++;
    if (hb_retMutex(path)->read_count == 1)
        pthread_mutex_lock(&(hb_retMutex(path)->wrt));
    pthread_mutex_unlock(&(hb_retMutex(path)->mutex));

    printf("[hb_read in CS] %s\n", path);
    temp = hb_retMutex(path);
    printf("inode : %lu\nread count : %d\n", temp->inode, temp->read_count);
    */

    ret = log_syscall("pread", pread(fi->fh, buf, size, offset), 0);    //// CS
    
    /*
    pthread_mutex_lock(&(hb_retMutex(path)->mutex));
    hb_retMutex(path)->read_count--;
    if (hb_retMutex(path)->read_count == 0)
        pthread_mutex_unlock(&(hb_retMutex(path)->wrt));
    pthread_mutex_unlock(&(hb_retMutex(path)->mutex));

    printf("[hb_read out CS] Out~\n");
    */

    return ret;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
// As  with read(), the documentation above is inconsistent with the
// documentation for the write() system call.
int hb_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = -1, ret;
    struct stat st;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    struct mutex_node *temp;
    struct ulog_node *log_head;
    
    //log_msg("\nhb_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n", path, buf, size, offset, fi);
    // no need to get fpath on this one, since I work from fi->fh not the path
    //log_fi(fi);

    //printf("[hb_write in] size : %lu, offset : %li\n", size, offset);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);
    //printf("[read] %s %s\n", fpath_ssd, fpath_hdd);
    retstat = lstat(fpath_ssd, &st);
    if (retstat != 0)
        retstat = lstat(fpath_hdd, &st);
    if (retstat != 0)
        printf("[Error] No exist\n");

    /*
    printf("[hb_write in CS] %s\n", path);
    temp = hb_retMutex(st.st_ino);
    printf("inode : %lu\nread count : %d\n", temp->inode, temp->read_count);
    */


    /*printf("[hb_write in CS] %s\n", path);
    temp = hb_retMutex(path);
    printf("inode : %lu\nread count : %d\n", temp->inode, temp->read_count);*/
    pthread_mutex_lock(&(hb_retMutex(path)->wrt));

    ret = log_syscall("pwrite", pwrite(fi->fh, buf, size, offset), 0);

    if ( (log_head = hb_findUfile(path)) != NULL ) {
        hb_insertUlog(log_head, size, offset);
        printf("[hb_write] tid : %lu, size : %lu, offset : %li\n",pthread_self(), size, offset);
        //hb_printUlog(log_head);        
    }

    pthread_mutex_unlock(&(hb_retMutex(path)->wrt));

    return ret;
}

/** Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
int hb_statfs(const char *path, struct statvfs *statv)
{
    int retstat = 0;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    
    //log_msg("\nhb_statfs(path=\"%s\", statv=0x%08x)\n", path, statv);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);
    
    // get stats for underlying filesystem
    retstat = log_syscall("statvfs", statvfs(fpath_ssd, statv), 0);
    if (retstat != 0)
        retstat = log_syscall("statvfs", statvfs(fpath_hdd, statv), 0);
    
    //log_statvfs(statv);
    
    return retstat;
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
// this is a no-op in BBFS.  It just logs the call and returns success
int hb_flush(const char *path, struct fuse_file_info *fi)
{
    ////log_msg("\nhb_flush(path=\"%s\", fi=0x%08x)\n", path, fi);
    // no need to get fpath on this one, since I work from fi->fh not the path
    ////log_fi(fi);
	
    return 0;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int hb_release(const char *path, struct fuse_file_info *fi)
{
    int retstat, ret;
    struct mutex_node *temp;
    char fpath_ssd[PATH_MAX];   // Basically, fpath is a full path of ssd
    char fpath_hdd[PATH_MAX];   // full path of hdd.
    struct stat st;
    
    //log_msg("\nhb_release(path=\"%s\", fi=0x%08x)\n", path, fi);
    //log_fi(fi);

    // We need to close the file.  Had we allocated any resources
    // (buffers etc) we'd need to free them here as well.
    
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    retstat = log_syscall("close", close(fi->fh), 0);

    if (retstat == 0) {
        int mode_value = fi->flags & 0xFFFF;

        if (mode_value == 32768) {
            hb_retMutex(path)->read_count--;
        }

        else if (mode_value == 32769 || mode_value == 32770 || mode_value == 33793 || mode_value == 32800) {
            ///// 32769 is 0x8001 and 32770 is 0x8002.
            ///// 0x8001 : WRONLY, 0x8002 : RDWR
            ///// 33793 is APPEND case
            ///// => Write case
            
            struct ulog_node *log_head, *ptr;
            char cp_buffer[MAX_BUFFER_SIZE] = {0,};
            int cp_blocksize = 4096; //// Basic block size
            int cp_offset = 0;
            int fd_read, fd_write;
            lstat(fpath_ssd, &st);

            if (st.st_size >= HB_DATA->mig_threshold && hb_retMutex(path)->write_count == 1) {

                printf("[hb_release] big migration, tid : %lu\n",pthread_self());

                hb_createUfile(path);

                fd_read = open(fpath_ssd, O_RDONLY);
                mknod (fpath_hdd, 0x81B4, 0);
                fd_write = open(fpath_hdd, O_WRONLY);
                
                while (1) {
                    pthread_mutex_lock(&(hb_retMutex(path)->wrt));

                    ret = pread(fd_read, cp_buffer, cp_blocksize, cp_offset);
                    if (ret == 0) {
                        pthread_mutex_unlock(&(hb_retMutex(path)->wrt));
                        break;
                    }
                    pwrite(fd_write, cp_buffer, cp_blocksize, cp_offset);
                    printf("...(migration, path : %s, tid : %lu)...\n", path, pthread_self());

                    cp_offset += cp_blocksize;
                    pthread_mutex_unlock(&(hb_retMutex(path)->wrt));
                }

                close(fd_read);
                close(fd_write);

                log_head = hb_findUfile(path);
                if (log_head != NULL && log_head->size > 0) {
                    fd_read = open(fpath_ssd, O_RDONLY);
                    fd_write = open(fpath_hdd, O_RDWR);

                    //printf("[log_head] size : %d, offset : %d\n",log_head->size, log_head->offset);
                    while (!hb_finishedUlog(log_head)) {
                        ptr = log_head;
                        while (ptr != NULL) {
                            cp_offset = ptr->offset;

                            while (ptr->size > 0) {
                                pthread_mutex_lock(&(hb_retMutex(path)->wrt));
                                pread(fd_read, cp_buffer, cp_blocksize, cp_offset);
                                pwrite(fd_write, cp_buffer, cp_blocksize, cp_offset);
                                printf("...(update log, path : %s, tid : %lu)...\n", path, pthread_self());

                                cp_offset += cp_blocksize;
                                ptr->size -= cp_blocksize;
                                pthread_mutex_unlock(&(hb_retMutex(path)->wrt));
                            }
                            ptr->size = 0;
                            ptr = ptr->link;
                        }
                    }
                    close(fd_read);
                    close(fd_write);

                }

                printf("[hb_release] before free, path : %s, tid : %lu\n",path, pthread_self());
                hb_freeUlog(log_head);
                hb_printUlog(log_head);
                hb_deleteUfile(path);
                remove(fpath_ssd);
                printf("[hb_release] finally passed free and remove\n");
                //rename(fpath_ssd, fpath_hdd);

            }
            printf("[hb_release] end here, path : %s, tid : %lu\n",path, pthread_self());
            hb_retMutex(path)->write_count--;
            
        }
        /*
        if (mode_value == 32768) {
            ///// 32768 is 0x8000.
            ///// 0x8000 : RDONLY
            ///// => Read case

            ////// CS up to here. End of CS.
            //printf("[hb_release] mode %d\n",mode_value);
            pthread_mutex_lock(&(hb_retMutex(path)->mutex));
            hb_retMutex(path)->read_count--;
            if (hb_retMutex(path)->read_count == 0)
                pthread_mutex_unlock(&(hb_retMutex(path)->wrt));
            pthread_mutex_unlock(&(hb_retMutex(path)->mutex));

            printf("[hb_release out of CS] Out of reading\n");
            temp = hb_retMutex(path);
            printf("(read)name : %s\n(read)read_count : %d\n", temp->rel_path, temp->read_count);
        }
        else if (mode_value == 32769 || mode_value == 32770) {
            ///// 32769 is 0x8001 and 32770 is 0x8002.
            ///// 0x8001 : WRONLY, 0x8002 : RDWR
            ///// => Write case

            lstat(fpath_ssd, &st);
            //printf("[hb_release] mode %d\n",mode_value);

            if (!hb_isroot(path) && S_ISDIR(st.st_mode)) {
                hb_recurMig(fpath_ssd, fpath_hdd, "");
            }

            if (st.st_size >= HB_DATA->mig_threshold) {
                rename(fpath_ssd, fpath_hdd);
            }

            ////// CS up to here. End of CS.
            pthread_mutex_unlock(&(hb_retMutex(path)->wrt));

            printf("[hb_release out of CS] Out of writing\n");
            temp = hb_retMutex(path);
            printf("(write)name : %s\n(write)read_count : %d\n", temp->rel_path, temp->read_count);
        }
        */
    }

    return retstat;

}

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
int hb_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    //log_msg("\nhb_fsync(path=\"%s\", datasync=%d, fi=0x%08x)\n", path, datasync, fi);
    //log_fi(fi);
    
    // some unix-like systems (notably freebsd) don't have a datasync call
#ifdef HAVE_FDATASYNC
    if (datasync)
	return log_syscall("fdatasync", fdatasync(fi->fh), 0);
    else
#endif	
	return log_syscall("fsync", fsync(fi->fh), 0);
}

#ifdef HAVE_SYS_XATTR_H
/** Set extended attributes */
int hb_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    int ret;
    
    ////log_msg("\nhb_setxattr(path=\"%s\", name=\"%s\", value=\"%s\", size=%d, flags=0x%08x)\n", path, name, value, size, flags);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    if ( (ret = log_syscall("lsetxattr", lsetxattr(fpath, name, value, size, flags), 0) ) != 0) 
        return log_syscall("lsetxattr", lsetxattr(fpath, name, value, size, flags), 0);

    return ret;
}

/** Get extended attributes */
int hb_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int retstat = 0;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    
    ////log_msg("\nhb_getxattr(path = \"%s\", name = \"%s\", value = 0x%08x, size = %d)\n", path, name, value, size);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    retstat = log_syscall("lgetxattr", lgetxattr(fpath_ssd, name, value, size), 0);
    if (retstat != 0)
        retstat = log_syscall("lgetxattr", lgetxattr(fpath_hdd, name, value, size), 0);
	////log_msg("    value = \"%s\"\n", value);
    
    return retstat;
}

/** List extended attributes */
int hb_listxattr(const char *path, char *list, size_t size)
{
    int retstat = 0;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    char *ptr;
    
    ////log_msg("hb_listxattr(path=\"%s\", list=0x%08x, size=%d)\n", path, list, size);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    retstat = log_syscall("llistxattr", llistxattr(fpath_ssd, list, size), 0);
    if (retstat != 0) {
        retstat = log_syscall("llistxattr", llistxattr(fpath_hdd, list, size), 0);
	////log_msg("    returned attributes (length %d):\n", retstat);
        //for (ptr = list; ptr < list + retstat; ptr += strlen(ptr)+1)
	        ////log_msg("    \"%s\"\n", ptr);
    }
    
    return retstat;
}

/** Remove extended attributes */
int hb_removexattr(const char *path, const char *name)
{
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
    int ret;
    
    ////log_msg("\nhb_removexattr(path=\"%s\", name=\"%s\")\n", path, name);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    if ( (ret = log_syscall("lremovexattr", lremovexattr(fpath_ssd, name), 0)) != 0) 
        return log_syscall("lremovexattr", lremovexattr(fpath_hdd, name), 0);

    return ret;
}
#endif

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int hb_opendir(const char *path, struct fuse_file_info *fi)
{
    DIR *dp;
    int retstat = 0;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];

    //log_msg("\nhb_opendir(path=\"%s\", fi=0x%08x)\n", path, fi);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);

    // since opendir returns a pointer, takes some custom handling of
    // return status.
    dp = opendir(fpath_ssd);
    //log_msg("    opendir returned 0x%p\n", dp);
    if (dp == NULL) {
        dp = opendir(fpath_hdd);
	    //retstat = log_error("hb_opendir opendir");
    }
    
    fi->fh = (intptr_t) dp;
    
    //log_fi(fi);
    
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */

int hb_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	       struct fuse_file_info *fi)
{
    char fpath_for_hdd[PATH_MAX];
    int retstat = 0;
    DIR *dp;
    struct dirent *de;
    
    //log_msg("\nhb_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n", path, buf, filler, offset, fi);
    // once again, no need for fullpath -- but note that I need to cast fi->fh
    dp = (DIR *) (uintptr_t) fi->fh;

    // Every directory contains at least two entries: . and ..  If my
    // first call to the system readdir() returns NULL I've got an
    // error; near as I can tell, that's the only condition under
    // which I can get an error from readdir()
    de = readdir(dp);
    //log_msg("    readdir returned 0x%p\n", de);
    /*
    if (de == 0) {
        retstat = log_error("hb_readdir readdir");
        return retstat;
    }*/

    // This will copy the entire directory into the buffer.  The loop exits
    // when either the system readdir() returns NULL, or filler()
    // returns something non-zero.  The first case just means I've
    // read the whole directory; the second means the buffer is full.
    do {
        if (filler(buf, de->d_name, NULL, 0) != 0) {
            return -ENOMEM;
        }
    } while ((de = readdir(dp)) != NULL);

    //// In above, just ssd's files are fillered.
    //// In here, hdd's files will be fillered, too.
    //// Directory can be duplicated, so I need to deal with that.
    hb_fullpath_hdd(fpath_for_hdd, path);
    if ( (dp = opendir(fpath_for_hdd)) != NULL ) {
        de = readdir(dp);
        do {
            char fpath_hdd[PATH_MAX] = {0,};
            char spath[PATH_MAX] = "/";
            struct stat st;

            if (hb_isroot(path))
                memcpy(spath, de->d_name, strlen(de->d_name));
            else
                memcpy(spath+1, de->d_name, strlen(de->d_name));

            memcpy(fpath_hdd, fpath_for_hdd, strlen(fpath_for_hdd));
            memcpy(fpath_hdd + strlen(fpath_hdd), spath, strlen(spath));

            lstat(fpath_hdd, &st);

            if (S_ISDIR(st.st_mode))    /// eliminate directories duplication
                continue;

            if (filler(buf, de->d_name, NULL, 0) != 0) {
                return -ENOMEM;
            }
        } while ((de = readdir(dp)) != NULL);

    }
   
    //log_fi(fi);
    
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int hb_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    //log_msg("\nhb_releasedir(path=\"%s\", fi=0x%08x)\n", path, fi);
    //log_fi(fi);
    
    closedir((DIR *) (uintptr_t) fi->fh);
    
    return retstat;
}

/** Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data
 *
 * Introduced in version 2.3
 */
// when exactly is this called?  when a user calls fsync and it
// happens to be a directory? ??? >>> I need to implement this...
int hb_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    ////log_msg("\nhb_fsyncdir(path=\"%s\", datasync=%d, fi=0x%08x)\n", path, datasync, fi);
    ////log_fi(fi);
    
    return retstat;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
void *hb_init(struct fuse_conn_info *conn)
{
    //log_msg("\nhb_init()\n");
    //log_conn(conn);
   /*   //////// have to restore 
    DIR *dir;
    struct dirent *ent;
    //cihar temp1[PATH_MAX] = "/.";
    //char temp2[PATH_MAX] = "/..";

    if ( (dir = opendir(HB_DATA->dir_hdd)) != NULL ) {
        while ( (ent = readdir(dir)) != NULL ) {
            char fpath_hdd[PATH_MAX];
            char link[PATH_MAX] = "/";
            char link_path[PATH_MAX] = {0,};
            int ret=0;

            memcpy(link+1, ent->d_name, strlen(ent->d_name));
            strcpy(link_path, HB_DATA->dir_ssd);

            hb_fullpath_hdd(fpath_hdd, link);

            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                printf("[init] path : %s, file : %s, link : %s, ", HB_DATA->dir_hdd, fpath_hdd, link);
                memcpy(link_path+strlen(link_path), link, strlen(link));

                ret = symlink(fpath_hdd, link_path);
                printf("link_path : %s, ret : %d\n",link_path, ret);
            }

        }
    }
    */
    //log_fuse_context(fuse_get_context());
    
    return HB_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void hb_destroy(void *userdata)
{
    //log_msg("\nhb_destroy(userdata=0x%08x)\n", userdata);
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 *
 * Introduced in version 2.5
 */
int hb_access(const char *path, int mask)
{
    int retstat = 0;
    char fpath_ssd[PATH_MAX];
    char fpath_hdd[PATH_MAX];
   
    //log_msg("\nhb_access(path=\"%s\", mask=0%o)\n", path, mask);
    hb_fullpath_ssd(fpath_ssd, path);
    hb_fullpath_hdd(fpath_hdd, path);
    
    retstat = access(fpath_ssd, mask);
    
    if (retstat < 0)
        retstat = access(fpath_hdd, mask);
        //retstat = log_error("hb_access access");
    
    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
// Not implemented.  I had a version that used creat() to create and
// open the file, which it turned out opened the file write-only.

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 */
int hb_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    //log_msg("\nhb_ftruncate(path=\"%s\", offset=%lld, fi=0x%08x)\n", path, offset, fi);
    //log_fi(fi);
    
    retstat = ftruncate(fi->fh, offset);
    if (retstat < 0)
	    retstat = log_error("hb_ftruncate ftruncate");
    
    return retstat;
}

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 */
int hb_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    //log_msg("\nhb_fgetattr(path=\"%s\", statbuf=0x%08x, fi=0x%08x)\n", path, statbuf, fi);
    //log_fi(fi);

    // On FreeBSD, trying to do anything with the mountpoint ends up
    // opening it, and then using the FD for an fgetattr.  So in the
    // special case of a path of "/", I need to do a getattr on the
    // underlying root directory instead of doing the fgetattr().
    if (!strcmp(path, "/"))
        return hb_getattr(path, statbuf);
    
    retstat = fstat(fi->fh, statbuf);
    if (retstat < 0)
        retstat = log_error("hb_fgetattr fstat");
    
    //log_stat(statbuf);
    
    return retstat;
}

struct fuse_operations hb_oper = {
  .getattr = hb_getattr,
  .readlink = hb_readlink,
  // no .getdir -- that's deprecated
  .getdir = NULL,
  .mknod = hb_mknod,
  .mkdir = hb_mkdir,
  .unlink = hb_unlink,
  .rmdir = hb_rmdir,
  .symlink = hb_symlink,
  .rename = hb_rename,
  .link = hb_link,
  .chmod = hb_chmod,
  .chown = hb_chown,
  .truncate = hb_truncate,
  .utime = hb_utime,
  .open = hb_open,
  .read = hb_read,
  .write = hb_write,
  /** Just a placeholder, don't set */ // huh???
  .statfs = hb_statfs,
  .flush = hb_flush,
  .release = hb_release,
  .fsync = hb_fsync,
  
#ifdef HAVE_SYS_XATTR_H
  .setxattr = hb_setxattr,
  .getxattr = hb_getxattr,
  .listxattr = hb_listxattr,
  .removexattr = hb_removexattr,
#endif
  
  .opendir = hb_opendir,
  .readdir = hb_readdir,
  .releasedir = hb_releasedir,
  .fsyncdir = hb_fsyncdir,
  .init = hb_init,
  .destroy = hb_destroy,
  .access = hb_access,
  .ftruncate = hb_ftruncate,
  .fgetattr = hb_fgetattr
};

void hb_usage()
{
    fprintf(stderr, "usage:  bbfs [FUSE and mount options] rootDir mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct hb_state *hb_data;

    // bbfs doesn't do any access checking on its own (the comment
    // blocks in fuse.h mention some of the functions that need
    // accesses checked -- but note there are other functions, like
    // chown(), that also need checking!).  Since running bbfs as root
    // will therefore open Metrodome-sized holes in the system
    // security, we'll check if root is trying to mount the filesystem
    // and refuse if it is.  The somewhat smaller hole of an ordinary
    // user doing it with the allow_other flag is still there because
    // I don't want to parse the options string.
    if ((getuid() == 0) || (geteuid() == 0)) {
        fprintf(stderr, "Running BBFS as root opens unnacceptable security holes\n");
        return 1;
    }

    // See which version of fuse we're running
    fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);
    
    // Perform some sanity checking on the command line:  make sure
    // there are enough arguments, and that neither of the last two
    // start with a hyphen (this will break if you actually have a
    // rootpoint or mountpoint whose name starts with a hyphen, but so
    // will a zillion other programs)
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
        hb_usage();

    hb_data = malloc(sizeof(struct hb_state));
    if (hb_data == NULL) {
        perror("main calloc");
        abort();
    }

    // Pull the rootdir out of the argument list and save it in my
    // internal data
    hb_data->dir_ssd = realpath(argv[argc-3], NULL);
    hb_data->dir_hdd = realpath(argv[argc-2], NULL);
    hb_data->dir_workspace = realpath(argv[argc-1], NULL);
    hb_data->mig_threshold = atoi(argv[argc-4]);

    argv[argc-4] = argv[argc-1];
    argv[argc-1] = NULL;
    argc -= 3;
    
    hb_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &hb_oper, hb_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    hb_freeMutex();    
    
    return fuse_stat;
}
