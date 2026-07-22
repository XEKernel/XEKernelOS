/* XEKernelOS 用户态 Shell — 裸 int $0x80，已验证可靠 */

#define COLOR_BLACK   0x00
#define COLOR_DGRAY   0x08
#define COLOR_LGRAY   0x07
#define COLOR_LRED    0x0C
#define COLOR_LGREEN  0x0A
#define COLOR_LCYAN   0x0B
#define COLOR_YELLOW  0x0E

/* 裸 syscall 宏 */
#define _S1(n,a)       __asm__ volatile("int $0x80" : : "a"(n), "b"(a) : "memory")
#define _S4(n,a,b,c)   __asm__ volatile("int $0x80" : : "a"(n), "b"(a), "c"(b), "d"(c) : "memory")
#define _CLS(c)        _S1(13, (int)(c))
#define _PUTC(c)       _S1(14, (int)(c))
#define _PUTS(s)       _S1(15, (int)(s))
#define _SETFG(c)      _S1(16, (int)(c))
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

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { char ca=*a,cb=*b; if(ca>='A'&&ca<='Z')ca+=32; if(cb>='A'&&cb<='Z')cb+=32; if(ca!=cb)return 0; a++;b++; }
    return *a==*b;
}
static const char *next_tok(const char *s, char *o) {
    while(*s==' ')s++; int i=0; while(*s&&*s!=' '&&i<31)o[i++]=*s++; o[i]=0; while(*s==' ')s++; return s;
}
static void prompt(void) {
    char w[64]; _CWD(w);
    _SETFG(COLOR_LGREEN); _PUTS(w);
    _PUTS("> ");
    _SETFG(COLOR_LGRAY);
}

static void ok(void)  { _SETFG(COLOR_LGREEN); _PUTS(" OK\n"); _SETFG(COLOR_LGRAY); }
static void err(const char *s) { _SETFG(COLOR_LRED); _PUTS(s); _SETFG(COLOR_LGRAY); }

static void cmd_help(void) {
    _SETFG(COLOR_LCYAN);
    _PUTS("═ XEKernelOS Ring3 Shell ═\n");
    _SETFG(COLOR_LGRAY);
    _PUTS("HELP     帮助\n");
    _PUTS("CLEAR    清屏\n");
    _PUTS("LS       列出文件\n");
    _PUTS("CD  dir  切换目录 (CD 回根)\n");
    _PUTS("ECHO msg 回显消息\n");
    _PUTS("MKDIR n  创建目录\n");
    _PUTS("RMDIR n  删除空目录\n");
    _PUTS("RM   f   删除文件\n");
    _PUTS("MV a b   重命名\n");
    _PUTS("CREATE n c 创建文件\n");
    _PUTS("TIME     显示时间\n");
    _PUTS("TMP  L   列出 /tmp\n");
    _PUTS("EXIT     返回内核 Shell\n");
}

static void cmd_ls(void)   { _DIR; }
static void cmd_cls(void)  { _CLS(COLOR_BLACK); }

static void cmd_cd(const char *a) {
    while(*a==' ')a++; if(!*a) { _CD("\\"); return; }
    _CD(a);
}

static void cmd_echo(const char *a) { while(*a==' ')a++; if(*a)_PUTS(a); _PUTC('\n'); }

static void cmd_mkdir(const char *a) {
    while(*a==' ')a++; if(!*a){err("用法: MKDIR <目录名>\n");return;}
    _MKDIR(a);
}

static void cmd_rmdir(const char *a) {
    while(*a==' ')a++; if(!*a){err("用法: RMDIR <目录名>\n");return;}
    _RMDIR(a);
}

static void cmd_rm(const char *a) {
    while(*a==' ')a++; if(!*a){err("用法: RM <文件名>\n");return;}
    _DEL(a);
}

static void cmd_mv(const char *a) {
    while(*a==' ')a++; if(!*a){err("用法: MV <原名> <新名>\n");return;}
    char s[32]; int i=0; while(*a&&*a!=' '&&i<31)s[i++]=*a++; s[i]=0;
    while(*a==' ')a++; if(!*a){err("用法: MV <原名> <新名>\n");return;}
    _REN(s,a);
}

static void cmd_create(const char *a) {
    while(*a==' ')a++; if(!*a){err("用法: CREATE <名> <内容>\n");return;}
    char n[32]; int i=0; while(*a&&*a!=' '&&i<31)n[i++]=*a++; n[i]=0;
    while(*a==' ')a++; if(!*a){err("用法: CREATE <名> <内容>\n");return;}
    int l=0; while(a[l])l++;
    _WRITE(n,(int)a,l);
}

static void cmd_time(void) { char t[9]; _TIME(t); _SETFG(COLOR_YELLOW); _PUTS(t); _PUTC('\n'); _SETFG(COLOR_LGRAY); }

static void cmd_tmp(const char *a) {
    while(*a==' ')a++; if(!*a){err("用法: TMP L\n");return;}
    char o[4];
    next_tok(a,o);
    if(str_eq(o,"L")) {
        static char lb[512] __attribute__((section(".data")));
        _RD_LIST((int)lb,511);
        int n=0; while(n<511&&lb[n])n++;
        if(!n) { _PUTS("  /tmp 为空\n"); return; }
        _PUTS(lb);
    } else {
        err("TMP 仅支持 L (列表)\n");
    }
}

extern "C" void _start(void) {
    _CLS(COLOR_BLACK);
    _SETFG(COLOR_LCYAN);
    _PUTS("XEKernelOS 用户态 Shell\n");
    _SETFG(COLOR_DGRAY);
    _PUTS("输入 HELP 查看命令\n\n");

    volatile char buf[128];
    char cmd[32];

    for (;;) {
        prompt();
        _READ((char*)buf,127);

        int n=0; while(n<127&&buf[n])n++;
        if(!n) continue;

        const char *a = next_tok((const char*)buf, cmd);

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
        else if (str_eq(cmd,"EXIT"))   break;
        else {
            _SETFG(COLOR_LRED);
            _PUTS("未知命令: ");
            _PUTS(cmd); _PUTC('\n');
            _SETFG(COLOR_LGRAY);
        }
    }
    _EXIT;
    for(;;){}
}
