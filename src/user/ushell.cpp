/* XEKernelOS Ring3 Shell — 准则五合规: FS 操作走用户态 ufs.cpp */

#include "ufs.h"

#define COLOR_BLACK   0x00
#define COLOR_DGRAY   0x08
#define COLOR_LGRAY   0x07
#define COLOR_LRED    0x0C
#define COLOR_LGREEN  0x0A
#define COLOR_LCYAN   0x0B
#define COLOR_YELLOW  0x0E

#define _S1(n,a)       __asm__ volatile("int $0x80" : : "a"(n), "b"(a) : "memory")
#define _S4(n,a,b,c)   __asm__ volatile("int $0x80" : : "a"(n), "b"(a), "c"(b), "d"(c) : "memory")

/* 准则一: GFX operations through fd 0 (stdout framebuffer)
   SYS_FWRITE(36): ebx=fd, ecx=str, edx=len
   SYS_IOCTL(46):  ebx=fd, ecx=cmd, edx=arg */
#define FB_FD       0
#define IO_FG       1       /* IOCTL_GFX_SET_FG */
#define IO_CLS      2       /* IOCTL_GFX_CLS */

static void _PUTC(char c) {
    char tmp[2] = {c, 0};
    __asm__ volatile("int $0x80" : : "a"(36), "b"(FB_FD), "c"((int)tmp), "d"(1) : "memory");
}
static void _PUTS(const char *s) {
    int l=0; while(s[l]) l++;
    __asm__ volatile("int $0x80" : : "a"(36), "b"(FB_FD), "c"((int)s), "d"(l) : "memory");
}
static void _SETFG(int c) {
    _S4(46, FB_FD, IO_FG, c);
}
static void _CLS(int c) {
    _S4(46, FB_FD, IO_CLS, c);
}
#define _DIR           _S1(17, 0)
#define _CD(p)         _S1(18, (int)(p))
#define _MKDIR(p)      _S1(19, (int)(p))
#define _RMDIR(p)      _S1(20, (int)(p))
#define _DEL(p)        _S1(21, (int)(p))
#define _REN(o,n)      _S4(22, (int)(o), (int)(n), 0)
#define _WRITE(n,d,s)  _S4(23, (int)(n), (int)(d), s)
#define _CWD(b)        _S4(7, (int)(b), 64, 0)
#define _TIME(b)       _S1(8, (int)(b))
#define _EXIT          _S1(2, 0)
#define _READ(b,m)     _S4(3, (int)(b), m, 0)
#define _RD_LIST(b,m)  _S4(41, (int)(b), (int)(m), 0)
#define _CLOSE(f)      _S1(10, f)

/* sys_open: returns fd or -1 */
static int _OPEN(const char *p) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(4), "b"((int)p) : "memory");
    return ret;
}

static int str_eq(const char *a, const char *b) {
    while(*a&&*b){char ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb)return 0;a++;b++;}
    return *a==*b;
}
static const char *next_tok(const char *s, char *o) {
    while(*s==' ')s++;int i=0;while(*s&&*s!=' '&&i<31)o[i++]=*s++;o[i]=0;while(*s==' ')s++;return s;
}
static void prompt(void) {
    char w[64]; ufs_cwd(w, 64);
    _PUTC('\n');
    _SETFG(COLOR_LGREEN);
    _PUTS("XEKernel@Xek");
    _SETFG(COLOR_LCYAN);
    _PUTS(w);
    _SETFG(COLOR_LGREEN);
    _PUTS("> ");
    _SETFG(COLOR_LGRAY);
}

static void cmd_help(void) {
    _SETFG(COLOR_LCYAN);
    _PUTS("\n=== XEKernelOS \347\224\250\346\210\267 Shell ===\n\n");
    _SETFG(COLOR_YELLOW);
    _PUTS("  HELP    "); _SETFG(COLOR_LGRAY); _PUTS("\346\230\276\347\244\272\345\270\256\345\212\251");
    _SETFG(COLOR_YELLOW); _PUTS("    CLEAR   "); _SETFG(COLOR_LGRAY); _PUTS("\346\270\205\345\261\217\n");
    _SETFG(COLOR_YELLOW);
    _PUTS("  LS      "); _SETFG(COLOR_LGRAY); _PUTS("\345\210\227\345\207\272\346\226\207\344\273\266");
    _SETFG(COLOR_YELLOW); _PUTS("    CD      "); _SETFG(COLOR_LGRAY); _PUTS("\345\210\207\346\215\242\347\233\256\345\275\225\n");
    _SETFG(COLOR_YELLOW);
    _PUTS("  ECHO    "); _SETFG(COLOR_LGRAY); _PUTS("\345\233\236\346\230\276\346\226\207\346\234\254");
    _SETFG(COLOR_YELLOW); _PUTS("    MKDIR   "); _SETFG(COLOR_LGRAY); _PUTS("\345\210\233\345\273\272\347\233\256\345\275\225\n");
    _SETFG(COLOR_YELLOW);
    _PUTS("  RMDIR   "); _SETFG(COLOR_LGRAY); _PUTS("\345\210\240\351\231\244\347\233\256\345\275\225");
    _SETFG(COLOR_YELLOW); _PUTS("    RM      "); _SETFG(COLOR_LGRAY); _PUTS("\345\210\240\351\231\244\346\226\207\344\273\266\n");
    _SETFG(COLOR_YELLOW);
    _PUTS("  MV      "); _SETFG(COLOR_LGRAY); _PUTS("\351\207\215\345\221\275\345\220\215");
    _SETFG(COLOR_YELLOW); _PUTS("      CREATE  "); _SETFG(COLOR_LGRAY); _PUTS("\345\210\233\345\273\272\346\226\207\344\273\266\n");
    _SETFG(COLOR_YELLOW);
    _PUTS("  TIME    "); _SETFG(COLOR_LGRAY); _PUTS("\346\230\276\347\244\272\346\227\266\351\227\264");
    _SETFG(COLOR_YELLOW); _PUTS("    TMP     "); _SETFG(COLOR_LGRAY); _PUTS("\344\270\264\346\227\266\346\226\207\344\273\266\n");
    _SETFG(COLOR_YELLOW);
    _PUTS("  EXIT    "); _SETFG(COLOR_LGRAY); _PUTS("\351\200\200\345\207\272Shell\n");
    _SETFG(COLOR_YELLOW);
    _PUTS("  RUN     "); _SETFG(COLOR_LGRAY); _PUTS("\350\277\220\350\241\214\347\250\213\345\272\217\n");
    _SETFG(COLOR_DGRAY);
    _PUTS("\n\350\276\223\345\205\245\345\221\275\344\273\244\345\220\215\347\247\260\346\211\247\350\241\214\n\n");
    _SETFG(COLOR_LGRAY);
}

static void cmd_cls(void)     { _CLS(COLOR_BLACK); }
static void cmd_ls(void)      { ufs_ls(); }

static void cmd_cd(const char *a) {
    while(*a==' ')a++; if(!*a){ufs_cd("\\");return;}
    ufs_cd(a);
}

static void cmd_echo(const char *a) { while(*a==' ')a++; if(*a)_PUTS(a); _PUTC('\n'); }

static void cmd_mkdir(const char *a) { while(*a==' ')a++; if(!*a)return; ufs_mkdir(a); }
static void cmd_rmdir(const char *a) { while(*a==' ')a++; if(!*a)return; ufs_rmdir(a); }
static void cmd_rm(const char *a)    { while(*a==' ')a++; if(!*a)return; ufs_rm(a); }

static void cmd_mv(const char *a) {
    while(*a==' ')a++; if(!*a)return;
    char s[32];int i=0;while(*a&&*a!=' '&&i<31)s[i++]=*a++;s[i]=0;
    while(*a==' ')a++; if(!*a)return;
    ufs_mv(s,a);
}

static void cmd_create(const char *a) {
    while(*a==' ')a++; if(!*a)return;
    char n[32];int i=0;while(*a&&*a!=' '&&i<31)n[i++]=*a++;n[i]=0;
    while(*a==' ')a++; if(!*a)return;
    int l=0;while(a[l])l++;
    ufs_create(n,(const char*)a,l);
}

static void cmd_time(void) {
    char t[9];_TIME(t);
    _SETFG(COLOR_YELLOW); _PUTS(t); _PUTC('\n');
    _SETFG(COLOR_LGRAY);
}

static void cmd_tmp(const char *a) {
    while(*a==' ')a++; if(!*a)return;
    char o[4]; next_tok(a,o);
    if(str_eq(o,"L")) {
        static char lb[512] __attribute__((section(".data")));
        _RD_LIST((int)lb,511);
        int n=0;while(n<511&&lb[n])n++;
        _PUTS(n?lb:"/tmp empty\n");
    }
}

static void cmd_run(const char *a) {
    while(*a==' ')a++; if(!*a)return;
    int fd = _OPEN(a);
    if (fd < 0) {
        _SETFG(COLOR_LRED);
        _PUTS("\346\226\207\344\273\266\346\234\252\346\211\276\345\210\260\n");
        _SETFG(COLOR_LGRAY);
        return;
    }
    /* exec_fd replaces this process — no return on success */
    _S1(47, fd);  /* SYS_EXEC_FD */
    /* On failure: */
    _CLOSE(fd);
    _SETFG(COLOR_LRED);
    _PUTS("\346\211\247\350\241\214\345\244\261\350\264\245\n");
    _SETFG(COLOR_LGRAY);
}

extern "C" void _start(void) {
    _CLS(COLOR_BLACK);

    /* Initialize user-space FAT12 driver — reads BPB, computes geometry.
       Must be called before any ufs_* function. */
    if (ufs_init() != 0) {
        _SETFG(COLOR_LRED);
        _PUTS("FAT12 \345\210\235\345\247\213\345\214\226\345\244\261\350\264\245\n");
        _SETFG(COLOR_LGRAY);
    }

    _SETFG(COLOR_LCYAN);
    _PUTS("XEKernelOS \347\224\250\346\210\267 Shell\n");
    _SETFG(COLOR_DGRAY);
    _PUTS("\350\276\223\345\205\245 HELP \346\237\245\347\234\213\345\221\275\344\273\244\n\n");

    volatile char buf[128];
    char cmd[32];

    for(;;){
        prompt();
        _READ((char*)buf,127);
        int n=0;while(n<127&&buf[n])n++;
        if(!n)continue;
        const char *a=next_tok((const char*)buf,cmd);

        if      (str_eq(cmd,"HELP"))   cmd_help();
        else if (str_eq(cmd,"CLEAR"))  cmd_cls();
        else if (str_eq(cmd,"LS"))     cmd_ls();
        else if (str_eq(cmd,"CD"))     cmd_cd(a);
        else if (str_eq(cmd,"ECHO"))   cmd_echo(a);
        else if (str_eq(cmd,"MKDIR"))  cmd_mkdir(a);
        else if (str_eq(cmd,"RMDIR"))  cmd_rmdir(a);
        else if (str_eq(cmd,"RM"))     cmd_rm(a);
        else if (str_eq(cmd,"MV"))     cmd_mv(a);
        else if (str_eq(cmd,"CREATE")) cmd_create(a);
        else if (str_eq(cmd,"TIME"))   cmd_time();
        else if (str_eq(cmd,"TMP"))    cmd_tmp(a);
        else if (str_eq(cmd,"RUN"))    cmd_run(a);
        else if (str_eq(cmd,"EXIT"))   break;
        else {
            _SETFG(COLOR_LRED);
            _PUTS("\346\234\252\347\237\245\345\221\275\344\273\244: ");
            _PUTS(cmd);
            _PUTS("  (\350\276\223\345\205\245 HELP \346\237\245\347\234\213\345\270\256\345\212\251)\n");
            _SETFG(COLOR_LGRAY);
        }
    }
    _EXIT;
    for(;;){}
}
