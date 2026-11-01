#define _POSIX_C_SOURCE 200809L
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Linux reference service. All request paths are additionally validated by Buoy.
 * dirfd walks prevent symlink traversal, including concurrent path substitution.
 * The root lock excludes other registry service instances, not malicious local
 * writers with permission to alter the store. Keep the store service-owned. */
static int lockfd = -1;
static _Thread_local int last_error;
static _Thread_local int64_t last_size;

static int component(const char *s) {
    if (!s || !*s || !strcmp(s,".") || !strcmp(s,"..")) return 0;
    for (const unsigned char *p=(const unsigned char *)s; *p; ++p)
        if (*p < 32 || *p == '\\') return 0;
    return 1;
}
static int parent(int root, const char *relative, int create, char **leaf) {
    char *copy=strdup(relative), *save=NULL, *part;
    int fd=dup(root);
    if (!copy || fd<0 || relative[0]=='/') { free(copy); if(fd>=0)close(fd); return -1; }
    part=strtok_r(copy,"/",&save);
    if(!part) { free(copy);close(fd);return -1; }
    for (;;) {
        char *next=strtok_r(NULL,"/",&save);
        if(!component(part)) { free(copy);close(fd);errno=EINVAL;return -1; }
        if(!next) { *leaf=strdup(part);free(copy);if(!*leaf){close(fd);return -1;}return fd; }
        if(create && mkdirat(fd,part,0700)<0 && errno!=EEXIST) {free(copy);close(fd);return -1;}
        int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(child<0) {free(copy);close(fd);return -1;}
        close(fd);fd=child;part=next;
    }
}
int64_t bpr_open(const char *root) {
    /* Configured root must already exist. Reject symlinks in every component. */
    if(!root || root[0]!='/') return -1;
    int start=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC), fd=-1;
    char *leaf=NULL;
    if(start<0) return -1;
    if(!strcmp(root,"/")) { close(start);return -1; }
    int p=parent(start,root+1,0,&leaf);close(start);
    if(p<0) return -1;
    fd=openat(p,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);free(leaf);close(p);
    if(fd<0) return -1;
    lockfd=openat(fd,".registry.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lockfd<0 || flock(lockfd,LOCK_EX|LOCK_NB)<0) {if(lockfd>=0)close(lockfd);lockfd=-1;close(fd);return -1;}
    return fd;
}
void bpr_close(int64_t root) { if(root>=0)close((int)root);if(lockfd>=0){close(lockfd);lockfd=-1;} }
int64_t bpr_create(int64_t root,const char *relative,const void *bytes,int64_t length) {
    if(length<0 || (length && !bytes))return -1;
    char *leaf=NULL; int p=parent((int)root,relative,1,&leaf);
    if(p<0)return -1;
    char temporary[64];
    int fd=-1;
    /* PIDs and counters repeat after service/container restarts. Random names
     * avoid abandoned uploads; O_EXCL plus bounded retries still owns the file. */
    for(int attempt=0;attempt<32;++attempt) {
        unsigned char nonce[16];size_t offset=0;
        while(offset<sizeof nonce) {
            ssize_t n=getrandom(nonce+offset,sizeof nonce-offset,0);
            if(n<0 && errno==EINTR)continue;
            if(n<=0){free(leaf);close(p);return -1;}
            offset+=(size_t)n;
        }
        memcpy(temporary,".upload-",8);
        for(size_t i=0;i<sizeof nonce;++i)
            snprintf(temporary+8+i*2,3,"%02x",nonce[i]);
        fd=openat(p,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd>=0 || errno!=EEXIST)break;
    }
    if(fd<0){free(leaf);close(p);return -1;}
    int result=0; int64_t offset=0;
    while(offset<length) {
        ssize_t n=write(fd,(const char*)bytes+offset,(size_t)(length-offset));
        if(n<0 && errno==EINTR)continue;
        if(n<=0){result=-1;break;}
        offset+=n;
    }
    if(!result && fsync(fd)<0)result=-1;
    if(close(fd)<0)result=-1;
    if(!result && linkat(p,temporary,p,leaf,0)<0)result=errno==EEXIST ? 1 : -1;
    if(!result && fsync(p)<0)result=-1;
    unlinkat(p,temporary,0);free(leaf);close(p);
    return result;
}
void *bpr_read(int64_t root,const char *relative,int64_t maximum) {
    last_error=0;last_size=0;
    char *leaf=NULL;int p=parent((int)root,relative,0,&leaf);
    if(p<0){last_error=errno==ENOENT ? 404 : 500;return NULL;}
    /* A FIFO must not block open before fstat can reject non-regular files. */
    int fd=openat(p,leaf,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);free(leaf);close(p);
    if(fd<0){last_error=errno==ENOENT ? 404 : 500;return NULL;}
    struct stat st;
    if(fstat(fd,&st)<0 || !S_ISREG(st.st_mode)){close(fd);last_error=500;return NULL;}
    if(st.st_size<0 || st.st_size>maximum){close(fd);last_error=413;return NULL;}
    char *buf=malloc((size_t)st.st_size+1);
    if(!buf){close(fd);last_error=500;return NULL;}
    while(last_size<st.st_size) {
        ssize_t n=read(fd,buf+last_size,(size_t)(st.st_size-last_size));
        if(n<0 && errno==EINTR)continue;
        if(n<=0){free(buf);close(fd);last_error=500;return NULL;}
        last_size+=n;
    }
    char extra;ssize_t n=read(fd,&extra,1);close(fd);
    if(n!=0){free(buf);last_error=500;return NULL;}
    return buf;
}
int64_t bpr_read_size(void){return last_size;}
int64_t bpr_read_error(void){return last_error;}
void bpr_free(void *buffer){free(buffer);}
