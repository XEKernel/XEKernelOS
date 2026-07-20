#include "shell/shell.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/pic.h"
#include "drivers/ata.h"
#include "drivers/mouse.h"
#include "kernel/mm.h"
#include "kernel/user.h"
#include "kernel/loader.h"
#include "fs/fat12.h"
#include "lib/ports.h"
#include "lib/strutil.h"

#include "drivers/serial.h"

// (keep other includes)

static char buf[CMD_BUF];

static u32 parse_u32(const char *s) {
    u32 v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static u32 parse_hex(const char *s) {
    u32 v = 0;
    for (;;) {
        char c = *s;
        if      (c >= '0' && c <= '9') v = (v << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (c - 'A' + 10);
        else break;
        s++;
    }
    return v;
}

static char hex_buf[512];

static void hex_dump(u32 lba, const u8 *data) {
    gfx_puts("LBA "); gfx_put_hex_u32(lba); gfx_puts(":\n");
    for (int row = 0; row < 32; row++) {
        gfx_put_hex_byte((row * 16) >> 8);
        gfx_put_hex_byte((row * 16) & 0xFF);
        gfx_puts(": ");
        for (int col = 0; col < 16; col++) {
            gfx_put_hex_byte(data[row * 16 + col]);
            gfx_putc(' ');
            if (col == 7) gfx_putc(' ');
        }
        gfx_putc(' ');
        for (int col = 0; col < 16; col++) {
            char c = data[row * 16 + col];
            gfx_putc((c >= 32 && c < 127) ? c : '.');
        }
        gfx_putc('\n');
    }
}

static void cmd_help(void) {
    gfx_puts("Available commands:\n");
    gfx_puts("  HELP     - Show this help\n");
    gfx_puts("  CLEAR    - Clear screen\n");
    gfx_puts("  INFO     - System info\n");
    gfx_puts("  MEM      - Memory stats\n");
    gfx_puts("  DISK ID  - Identify ATA drive\n");
    gfx_puts("  DISK R <lba>  - Hex dump sector\n");
    gfx_puts("  DISK W <lba> <hex> - Write byte\n");
    gfx_puts("  LS       - List FAT12 files\n");
    gfx_puts("  CAT <f>  - Display file\n");
    gfx_puts("  CD <dir> - Change directory\n");
    gfx_puts("  DIR      - List current directory\n");
    gfx_puts("  MKDIR <n>- Create directory\n");
    gfx_puts("  MOUSE    - Show mouse pos\n");
    gfx_puts("  RUN <f>  - Load and run binary\n");
    gfx_puts("  CREATE <f> <c> - Create file\n");
    gfx_puts("  RM <f>   - Delete file\n");
    gfx_puts("  ECHO <t> - Print text\n");
    gfx_puts("  REBOOT   - Restart\n");
    gfx_puts("  SHUTDOWN - Halt CPU\n");
}

static void cmd_info(void) {
    gfx_puts("XEKernelOS v0.2.0 - XEKernel\n");
    gfx_puts("Architecture: x86 32-bit Protected Mode\n");
    gfx_puts("Boot: MBR -> Stage2(asm) -> Kernel(C)\n");
    gfx_puts("Paging: 4KB pages, identity mapping\n");
    gfx_puts("FS: FAT12 | Timer: PIT 100Hz\n");
    gfx_puts("Input: Keyboard IRQ + PS/2 Mouse\n");
    gfx_puts("Security: Ring 3 user mode + TSS\n");
}

static void cmd_mem(void) {
    char s[32];
    u32 total = mm_free_count();
    gfx_puts("Free pages: ");
    int n = 0, t = total;
    if (t == 0) { s[0] = '0'; n = 1; }
    else { while (t) { s[n++] = '0' + (t % 10); t /= 10; } }
    for (int i = 0; i < n/2; i++) { char c = s[i]; s[i] = s[n-1-i]; s[n-1-i] = c; }
    s[n] = '\n'; s[n+1] = 0;
    gfx_puts(s);
}

static void cmd_disk_id(void) {
    u16 ident[256];
    int r = ata_identify(ident);
    if (r == -1)      { gfx_puts("No ATA drive found.\n"); return; }
    if (r == -2)      { gfx_puts("ATA drive busy/timeout.\n"); return; }
    if (r == -3)      { gfx_puts("ATAPI device (not HDD).\n"); return; }
    if (r == -4)      { gfx_puts("ATA IDENTIFY error.\n"); return; }

    char model[41];
    for (int i = 0; i < 20; i++) {
        u16 w = ident[27 + i];
        model[i*2]     = (w >> 8) & 0xFF;
        model[i*2 + 1] = w & 0xFF;
    }
    model[40] = 0;
    for (int i = 39; i > 0 && model[i] == ' '; i--) model[i] = 0;

    char sn[21];
    for (int i = 0; i < 10; i++) {
        u16 w = ident[10 + i];
        sn[i*2]     = (w >> 8) & 0xFF;
        sn[i*2 + 1] = w & 0xFF;
    }
    sn[20] = 0;
    for (int i = 19; i > 0 && sn[i] == ' '; i--) sn[i] = 0;

    u32 sectors = ((u32)ident[61] << 16) | ident[60];

    gfx_puts("Model:    "); gfx_puts(model); gfx_putc('\n');
    gfx_puts("Serial:   "); gfx_puts(sn);    gfx_putc('\n');
    gfx_puts("Sectors:  "); gfx_put_hex_u32(sectors); gfx_putc('\n');
}

static void cmd_disk_read(const char *args) {
    u32 lba = parse_u32(args);
    int r = ata_read(lba, 1, (u16 *)hex_buf);
    if (r) { gfx_puts("ATA read error.\n"); return; }
    hex_dump(lba, (u8 *)hex_buf);
}

static void cmd_disk_write(const char *args) {
    while (*args == ' ') args++;
    u32 lba = parse_u32(args);
    while (*args >= '0' && *args <= '9') args++;
    while (*args == ' ') args++;
    u32 off = parse_hex(args);
    while ((*args >= '0' && *args <= '9') || (*args >= 'a' && *args <= 'f') || (*args >= 'A' && *args <= 'F')) args++;
    while (*args == ' ') args++;
    u8 val = (u8)parse_hex(args);
    if (off >= 512) { gfx_puts("Offset must be < 512.\n"); return; }

    int r = ata_read(lba, 1, (u16 *)hex_buf);
    if (r) { gfx_puts("ATA read error.\n"); return; }
    hex_buf[off] = val;
    r = ata_write(lba, 1, (const u16 *)hex_buf);
    if (r) { gfx_puts("ATA write error.\n"); return; }
    gfx_puts("Wrote 0x"); gfx_put_hex_byte(val); gfx_puts(" at LBA "); gfx_put_hex_u32(lba); gfx_puts("+0x"); gfx_put_hex_byte((u8)off); gfx_putc('\n');
}

static void cmd_echo(const char *s) { if (s) gfx_puts(s); gfx_putc('\n'); }

static void cmd_ls(void) {
    fat_dir();
}

static void cmd_dir(void) {
    fat_dir();
}

static void cmd_cd(const char *name) {
    while (*name == ' ') name++;
    if (!*name) { fat_cd("\\"); return; }
    int r = fat_cd(name);
    if (r == -1) { gfx_puts("Not found.\n"); return; }
    if (r == -2) { gfx_puts("Not a directory.\n"); return; }
}

static void cmd_mkdir(const char *name) {
    while (*name == ' ') name++;
    if (!*name) { gfx_puts("Usage: MKDIR <name>\n"); return; }
    int r = fat_mkdir(name);
    if (r == -1) { gfx_puts("Already exists.\n"); return; }
    if (r == -2) { gfx_puts("Disk full.\n"); return; }
    if (r)       { gfx_puts("MKDIR failed.\n"); }
}

static void cmd_cat(const char *name) {
    while (*name == ' ') name++;
    if (!*name) { gfx_puts("Usage: CAT <filename>\n"); return; }
    if (fat_cat(name) != 0) gfx_puts("File not found.\n");
}

static void cmd_mouse(void) {
    int x, y, btn;
    mouse_get(&x, &y, &btn);
    gfx_puts("Mouse: X="); gfx_put_hex_u32(x); 
    gfx_puts(" Y="); gfx_put_hex_u32(y);
    gfx_puts(" BTN="); gfx_put_hex_u32(btn); gfx_putc('\n');
}

static const char ring3_msg[] = "Ring3: Hello!";

static void user_loop(void) {
    u32 addr = (u32)ring3_msg;
    __asm__ volatile(
        "mov $1, %%eax\n"
        "mov %0, %%ebx\n"
        "mov $14, %%ecx\n"
        "int $0x80\n"
        : : "r"(addr) : "eax", "ebx", "ecx"
    );
    for (;;) __asm__ volatile("pause");
}

static void cmd_run(const char *args) {
    while (*args == ' ') args++;
    if (!*args) {
        gfx_puts("Usage: RUN <filename>\n");
        return;
    }
    if (load_binary(args) != 0)
        gfx_puts("Failed to load program.\n");
}

static void cmd_create(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { gfx_puts("Usage: CREATE <name> <content>\n"); return; }
    char fname[64];
    int i = 0;
    while (*args && *args != ' ' && i < 63) fname[i++] = *args++;
    fname[i] = 0;
    while (*args == ' ') args++;
    if (!*args) { gfx_puts("Usage: CREATE <name> <content>\n"); return; }
    u32 len = 0;
    while (args[len]) len++;
    int r = fat_write_file(fname, (const u8 *)args, len);
    if (r == 0) gfx_puts("OK\n");
    else        gfx_puts("FAIL\n");
}

static void cmd_rm(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { gfx_puts("Usage: RM <name>\n"); return; }
    if (fat_delete_file(args) == 0) gfx_puts("OK\n");
    else                            gfx_puts("Not found.\n");
}

static void cmd_reboot(void) {
    gfx_puts("Rebooting...\n");
    outb(0x64, 0xFE);
    __asm__ volatile("0: hlt; jmp 0b");
}

static void cmd_shutdown(void) {
    gfx_puts("Halted.\n");
    __asm__ volatile("0: hlt; jmp 0b");
}

void shell_loop(void) {
    gfx_puts("\n");
    for (;;) {
        gfx_puts("XEKernel> ");
        gfx_cursor_draw();
        kb_readline(buf, CMD_BUF);
        gfx_cursor_erase();
        if (!buf[0]) continue;

        if (!strcmp_ci(buf, "HELP"))
            cmd_help();
        else if (!strcmp_ci(buf, "CLEAR"))
            gfx_clear(0x00);
        else if (!strcmp_ci(buf, "INFO"))
            cmd_info();
        else if (!strcmp_ci(buf, "MEM"))
            cmd_mem();
        else if (!strcmp_ci(buf, "DISK ID"))
            cmd_disk_id();
        else if (!strncmp_ci(buf, "DISK R", 6))
            cmd_disk_read(buf + 6);
        else if (!strncmp_ci(buf, "DISK W", 6))
            cmd_disk_write(buf + 6);
        else if (!strcmp_ci(buf, "LS"))
            cmd_ls();
        else if (!strcmp_ci(buf, "DIR"))
            cmd_dir();
        else if (!strncmp_ci(buf, "CD ", 3))
            cmd_cd(buf + 3);
        else if (!strcmp_ci(buf, "CD"))
            cmd_cd("\\");
        else if (!strncmp_ci(buf, "MKDIR ", 6))
            cmd_mkdir(buf + 6);
        else if (!strncmp_ci(buf, "CAT ", 4))
            cmd_cat(buf + 4);
        else if (!strcmp_ci(buf, "MOUSE"))
            cmd_mouse();
        else if (!strncmp_ci(buf, "RUN ", 4))
            cmd_run(buf + 4);
        else if (!strcmp_ci(buf, "RUN"))
            cmd_run("");
        else if (!strncmp_ci(buf, "CREATE ", 7))
            cmd_create(buf + 7);
        else if (!strncmp_ci(buf, "RM ", 3))
            cmd_rm(buf + 3);
        else if (!strncmp_ci(buf, "ECHO", 4)) {
            const char *a = buf + 4;
            while (*a == ' ') a++;
            cmd_echo(a);
        }
        else if (!strcmp_ci(buf, "REBOOT"))
            cmd_reboot();
        else if (!strcmp_ci(buf, "SHUTDOWN"))
            cmd_shutdown();
        else
            gfx_puts("Unknown command. Type HELP.\n");
    }
}
