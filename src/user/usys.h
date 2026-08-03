#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_READ  3
#define SYS_OPEN  4
#define SYS_FREAD 5
#define SYS_SBRK  6
#define SYS_GETCWD 7
#define SYS_TIME   8
#define SYS_GETFB  9
#define SYS_CLOSE  10
#define SYS_MOUSE  11
#define SYS_SLEEP  12
#define SYS_CLS    13
#define SYS_GFX_PUTC  14
#define SYS_GFX_PUTS  15
#define SYS_GFX_SET_FG 16
#define SYS_FAT_DIR    17
#define SYS_FAT_CD     18
#define SYS_FAT_MKDIR  19
#define SYS_FAT_RMDIR  20
#define SYS_FAT_DELETE 21
#define SYS_FAT_RENAME 22
#define SYS_FAT_WRITE  23
#define SYS_FORK       24
#define SYS_EXEC       25
#define SYS_WAITPID    26
#define SYS_GETPID     27
#define SYS_KILL       28
#define SYS_SIGACTION  29
#define SYS_SIGRETURN  30
#define SYS_STAT       31
#define SYS_LSEEK      32
#define SYS_DUP        33
#define SYS_DUP2       34
#define SYS_PIPE       35
#define SYS_FWRITE     36
#define SYS_SET_OUTFD  37
#define SYS_FSYNC      38
#define SYS_RD_CREATE  39
#define SYS_RD_READ    40
#define SYS_RD_LIST    41
#define SYS_RD_REMOVE  42
#define SYS_DROP_CAP   43
#define SYS_DISK_READ  44
#define SYS_DISK_WRITE 45
#define SYS_IOCTL      46
#define SYS_EXEC_FD    47

static inline int syscall4(int num, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static inline int syscall1(int num, int a1) { return syscall4(num, a1, 0, 0); }
static inline int syscall0(int num)          { return syscall4(num, 0, 0, 0); }

/* ---- graphics wrappers ---- */
static inline void gfx_putc(char c)        { syscall1(SYS_GFX_PUTC, (int)c); }
static inline void gfx_puts(const char *s) { syscall1(SYS_GFX_PUTS, (int)s); }
static inline void gfx_set_fg(int color)   { syscall1(SYS_GFX_SET_FG, color); }
static inline void gfx_cls(int color)      { syscall1(SYS_CLS, color); }

/* ---- input ---- */
static inline int  kb_read(char *buf, int max) { return syscall4(SYS_READ, (int)buf, max, 0); }

/* ---- FAT filesystem wrappers ---- */
static inline int  fat_dir(void)                           { return syscall0(SYS_FAT_DIR); }
static inline int  fat_cd(const char *name)                { return syscall1(SYS_FAT_CD, (int)name); }
static inline int  fat_mkdir(const char *name)             { return syscall1(SYS_FAT_MKDIR, (int)name); }
static inline int  fat_rmdir(const char *name)             { return syscall1(SYS_FAT_RMDIR, (int)name); }
static inline int  fat_delete(const char *name)            { return syscall1(SYS_FAT_DELETE, (int)name); }
static inline int  fat_rename(const char *old, const char *nw) { return syscall4(SYS_FAT_RENAME, (int)old, (int)nw, 0); }
static inline int  fat_write(const char *name, const char *data, int size) { return syscall4(SYS_FAT_WRITE, (int)name, (int)data, size); }
static inline int  fat_cwd(char *buf, int max)             { return syscall4(SYS_GETCWD, (int)buf, max, 0); }

/* Read file into user buffer (one-shot: open → read → close) */
static inline int fat_read(const char *name, char *buf, int max) {
    int fd = syscall1(SYS_OPEN, (int)name);
    if (fd < 0) return -1;
    int len = syscall4(SYS_FREAD, fd, (int)buf, max);
    syscall1(SYS_CLOSE, fd);
    return len;
}

/* ---- misc ---- */
static inline void proc_exit(void)              { syscall0(SYS_EXIT); }
static inline int  sys_fork(void)               { return syscall0(SYS_FORK); }
static inline int  sys_exec(const char *path)    { return syscall1(SYS_EXEC, (int)path); }
static inline int  sys_waitpid(void)             { return syscall0(SYS_WAITPID); }
static inline int  sys_getpid(void)              { return syscall0(SYS_GETPID); }
static inline int  sys_kill(int pid, int sig)    { return syscall4(SYS_KILL, pid, sig, 0); }
static inline int  sys_sigaction(int sig, int handler) { return syscall4(SYS_SIGACTION, sig, handler, 0); }
static inline void sys_sigreturn(void)           { syscall0(SYS_SIGRETURN); }
static inline int  sys_stat(const char *path, int *buf) { return syscall4(SYS_STAT, (int)path, (int)buf, 0); }
static inline int  sys_lseek(int fd, int off, int whence) { return syscall4(SYS_LSEEK, fd, off, whence); }
static inline int  sys_dup(int fd)               { return syscall1(SYS_DUP, fd); }
static inline int  sys_dup2(int old, int nw)     { return syscall4(SYS_DUP2, old, nw, 0); }
static inline int  sys_pipe(int fds[2])           { return syscall1(SYS_PIPE, (int)fds); }
static inline int  sys_fwrite(int fd, const char *s, int len) { return syscall4(SYS_FWRITE, fd, (int)s, len); }
static inline int  sys_set_outfd(int fd)          { return syscall1(SYS_SET_OUTFD, fd); }
static inline int  sys_fsync(int fd, const char *name) { return syscall4(SYS_FSYNC, fd, (int)name, 0); }
static inline int  sys_open(const char *name)     { return syscall1(SYS_OPEN, (int)name); }
static inline void sys_close(int fd)              { syscall1(SYS_CLOSE, fd); }
static inline void sys_time(char *buf)          { syscall1(SYS_TIME, (int)buf); }

/* Ramdisk */
static inline int  rd_create(const char *n, const char *d, int s) { return syscall4(SYS_RD_CREATE, (int)n, (int)d, s); }
static inline int  rd_read(const char *n, char *o, int m) { return syscall4(SYS_RD_READ, (int)n, (int)o, m); }
static inline int  rd_list(char *o, int m)       { return syscall4(SYS_RD_LIST, (int)o, m, 0); }
static inline int  rd_remove(const char *n)       { return syscall1(SYS_RD_REMOVE, (int)n); }
static inline int  sys_drop_cap(int cap)          { return syscall1(SYS_DROP_CAP, cap); }

#ifdef __cplusplus
}
#endif
