/*
 * vice_stubs.c - Stub implementations of VICE functions for testing
 *
 * This file provides minimal stub implementations of VICE functions
 * needed to link MCP tests without requiring the full VICE runtime.
 *
 * NOTE: This uses forward declarations to avoid complex VICE header dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

/* Forward declarations - avoid including VICE headers */
typedef signed int log_t;

/* Joystick constants from joyport/joystick.h */
#define JOYSTICK_DIRECTION_UP    1
#define JOYSTICK_DIRECTION_DOWN  2
#define JOYSTICK_DIRECTION_LEFT  4
#define JOYSTICK_DIRECTION_RIGHT 8

/* VHK key codes from arch/shared/hotkeys/vhkkeysyms.h */
#define VHK_KEY_BackSpace               0xff08
#define VHK_KEY_Tab                     0xff09
#define VHK_KEY_Return                  0xff0d
#define VHK_KEY_Escape                  0xff1b
#define VHK_KEY_Delete                  0xffff
#define VHK_KEY_Home                    0xff50
#define VHK_KEY_Left                    0xff51
#define VHK_KEY_Up                      0xff52
#define VHK_KEY_Right                   0xff53
#define VHK_KEY_Down                    0xff54
#define VHK_KEY_End                     0xff57
#define VHK_KEY_F1                      0xffbe
#define VHK_KEY_F2                      0xffbf
#define VHK_KEY_F3                      0xffc0
#define VHK_KEY_F4                      0xffc1
#define VHK_KEY_F5                      0xffc2
#define VHK_KEY_F6                      0xffc3
#define VHK_KEY_F7                      0xffc4
#define VHK_KEY_F8                      0xffc5

/* VHK modifiers from arch/shared/hotkeys/vhkkeysyms.h */
#define VHK_MOD_NONE    0x0000
#define VHK_MOD_ALT     0x0001
#define VHK_MOD_COMMAND 0x0002
#define VHK_MOD_CONTROL 0x0004
#define VHK_MOD_SHIFT   0x0040
#define VHK_MOD_META    0x0010

/* MOS6510 register structure (minimal definition for stubs) */
struct mos6510_regs_s {
    unsigned int pc;
    uint8_t a, x, y, sp, p, n, z;
};

struct mos6510_regs_s maincpu_regs;

/* Log system stubs */
log_t log_open(const char *id)
{
    (void)id;
    return 0;
}

int log_message(log_t log, const char *format, ...)
{
    (void)log;
    (void)format;
    return 0;
}

int log_error(log_t log, const char *format, ...)
{
    (void)log;
    (void)format;
    return 0;
}

int log_warning(log_t log, const char *format, ...)
{
    (void)log;
    (void)format;
    return 0;
}

/* Machine name stub */
const char *machine_get_name(void)
{
    return "TEST-MACHINE";
}

/* Memory stubs */
uint8_t mem_read(uint16_t addr)
{
    (void)addr;
    return 0;
}

void mem_store(uint16_t addr, uint8_t value)
{
    (void)addr;
    (void)value;
}

/* Memory bank stubs */
static const char *test_bank_names[] = { "default", "ram", "rom", "io", NULL };
static const int test_bank_numbers[] = { 0, 1, 2, 3 };

const char **mem_bank_list(void)
{
    return test_bank_names;
}

const int *mem_bank_list_nos(void)
{
    return test_bank_numbers;
}

int mem_bank_from_name(const char *name)
{
    int i;
    for (i = 0; test_bank_names[i] != NULL; i++) {
        if (strcmp(test_bank_names[i], name) == 0) {
            return test_bank_numbers[i];
        }
    }
    return -1;  /* Not found */
}

uint8_t mem_bank_peek(int bank, uint16_t addr, void *context)
{
    (void)bank;
    (void)addr;
    (void)context;
    return 0xAA;  /* Return test pattern */
}

/* CPU register stubs */
unsigned int maincpu_get_pc(void) { return 0; }
unsigned int maincpu_get_a(void) { return 0; }
unsigned int maincpu_get_x(void) { return 0; }
unsigned int maincpu_get_y(void) { return 0; }
unsigned int maincpu_get_sp(void) { return 0; }

void maincpu_set_pc(int val) { (void)val; }
void maincpu_set_a(int val) { (void)val; }
void maincpu_set_x(int val) { (void)val; }
void maincpu_set_y(int val) { (void)val; }
void maincpu_set_sp(int val) { (void)val; }
void maincpu_set_sign(int val) { (void)val; }
void maincpu_set_zero(int val) { (void)val; }
void maincpu_set_carry(int val) { (void)val; }
void maincpu_set_interrupt(int val) { (void)val; }
void maincpu_set_overflow(int val) { (void)val; }
void maincpu_set_break(int val) { (void)val; }
void maincpu_set_decimal(int val) { (void)val; }

/* Phase 3.1: Keyboard and Joystick stubs */
int kbdbuf_feed_string(const char *s)
{
    (void)s;
    return 0;  /* Return 0 on success, -1 on failure */
}

void keyboard_key_pressed(signed long key, int mod)
{
    (void)key;
    (void)mod;
}

void keyboard_key_released(signed long key, int mod)
{
    (void)key;
    (void)mod;
}

void joystick_set_value_absolute(unsigned int joyport, uint16_t value)
{
    (void)joyport;
    (void)value;
}

/* Phase 2.4: Disk management stubs */
int file_system_attach_disk(unsigned int unit, unsigned int drive, const char *filename)
{
    (void)unit;
    (void)drive;
    (void)filename;
    return 0;
}

void file_system_detach_disk(unsigned int unit, unsigned int drive)
{
    (void)unit;
    (void)drive;
}

void *file_system_get_vdrive(unsigned int unit)
{
    (void)unit;
    return NULL;
}

void *diskcontents_block_read(void *vdrive, unsigned int track)
{
    (void)vdrive;
    (void)track;
    return NULL;
}

char *image_contents_to_string(void *contents, char charset)
{
    (void)contents;
    (void)charset;
    return NULL;
}

char *image_contents_filename_to_string(void *file, char charset)
{
    (void)file;
    (void)charset;
    return NULL;
}

char *image_contents_filetype_to_string(void *file, char charset)
{
    (void)file;
    (void)charset;
    return NULL;
}

void image_contents_destroy(void *contents)
{
    (void)contents;
}

void lib_free(void *ptr)
{
    free(ptr);
}

void *lib_malloc(size_t size)
{
    return malloc(size);
}

char *lib_strdup(const char *str)
{
    if (str == NULL) return NULL;
    size_t len = strlen(str) + 1;
    char *dup = malloc(len);
    if (dup != NULL) {
        memcpy(dup, str, len);
    }
    return dup;
}

int vdrive_read_sector(void *vdrive, uint8_t *buf, unsigned int track, unsigned int sector)
{
    (void)vdrive;
    (void)buf;
    (void)track;
    (void)sector;
    return 0;
}

/* Phase 2.5: Display capture stubs */
void *machine_video_canvas_get(int num)
{
    (void)num;
    return NULL;
}

int screenshot_save(const char *drvname, const char *filename, void *canvas)
{
    (void)drvname;
    (void)filename;
    (void)canvas;
    return 0;
}

/* Phase 2.1: Checkpoint/Breakpoint stubs */
void *mon_breakpoint_add_checkpoint(unsigned int start, unsigned int end, int stop, int enabled, int operation, int temporary)
{
    (void)start;
    (void)end;
    (void)stop;
    (void)enabled;
    (void)operation;
    (void)temporary;
    return NULL;
}

void mon_breakpoint_delete_checkpoint(unsigned int id)
{
    (void)id;
}

void *mon_breakpoint_find_checkpoint(unsigned int id)
{
    (void)id;
    return NULL;
}

void *mon_breakpoint_checkpoint_list_get(void)
{
    return NULL;
}

void mon_breakpoint_switch_checkpoint(unsigned int id, int enabled)
{
    (void)id;
    (void)enabled;
}

void mon_breakpoint_set_ignore_count(unsigned int id, unsigned int count)
{
    (void)id;
    (void)count;
}

void mon_breakpoint_set_checkpoint_condition(int brk_num, void *cnode)
{
    (void)brk_num;
    (void)cnode;
}

/* Autostart stubs */
int autostart_autodetect(const char *file_name, const char *program_name,
                         unsigned int program_number, unsigned int runmode)
{
    (void)file_name;
    (void)program_name;
    (void)program_number;
    (void)runmode;
    return 0;  /* Success */
}

/* Phase 2: Execution control stubs */
enum {
    exit_mon_no = 0,
    exit_mon_continue = 1,
    exit_mon_change_flow = 2,
    exit_mon_quit_vice = 3
};

int exit_mon = 0;

void monitor_startup_trap(void)
{
    /* Stub: would trigger monitor entry */
}

void mon_instructions_step(int count)
{
    (void)count;
}

void mon_instructions_next(int count)
{
    (void)count;
}

/* Phase 4: Advanced debugging stubs */
const char *mon_disassemble_to_string_ex(int memspace, unsigned int addr,
    unsigned int opc, unsigned int p1, unsigned int p2, unsigned int p3,
    int hex_mode, unsigned *opc_size)
{
    (void)memspace;
    (void)addr;
    (void)opc;
    (void)p1;
    (void)p2;
    (void)p3;
    (void)hex_mode;
    if (opc_size) *opc_size = 1;
    return "NOP";  /* Return simple instruction for testing */
}

uint8_t mon_get_mem_val(int mem, uint16_t addr)
{
    (void)mem;
    (void)addr;
    return 0xEA;  /* NOP opcode */
}

/* Symbol table storage for testing - mimics real VICE behavior of storing pointers */
#define MAX_TEST_SYMBOLS 64
static struct {
    unsigned int addr;
    char *name;  /* Stores pointer directly, like real VICE */
} test_symbol_table[MAX_TEST_SYMBOLS];
static int test_symbol_count = 0;

void test_symbol_table_clear(void)
{
    /* Note: We don't free names - real VICE doesn't either (expects caller to manage) */
    test_symbol_count = 0;
}

int test_symbol_table_get_count(void)
{
    return test_symbol_count;
}

/* Get stored symbol by index - returns name pointer that was passed in */
const char *test_symbol_table_get_name(int index)
{
    if (index < 0 || index >= test_symbol_count) return NULL;
    return test_symbol_table[index].name;
}

unsigned int test_symbol_table_get_addr(int index)
{
    if (index < 0 || index >= test_symbol_count) return 0;
    return test_symbol_table[index].addr;
}

char *mon_symbol_table_lookup_name(int mem, uint16_t addr)
{
    int i;
    (void)mem;
    /* Search the test symbol table for matching address */
    for (i = 0; i < test_symbol_count; i++) {
        if (test_symbol_table[i].addr == addr) {
            return test_symbol_table[i].name;
        }
    }
    return NULL;  /* No symbol */
}

int mon_symbol_table_lookup_addr(int mem, char *name)
{
    int i;
    (void)mem;
    /* Search the test symbol table for matching name */
    for (i = 0; i < test_symbol_count; i++) {
        if (test_symbol_table[i].name != NULL &&
            strcmp(test_symbol_table[i].name, name) == 0) {
            return (int)test_symbol_table[i].addr;
        }
    }
    return -1;  /* Not found */
}

void mon_add_name_to_symbol_table(unsigned int addr, char *name)
{
    /* Store pointer directly like real VICE does - this exposes use-after-free bugs */
    if (test_symbol_count < MAX_TEST_SYMBOLS) {
        test_symbol_table[test_symbol_count].addr = addr;
        test_symbol_table[test_symbol_count].name = name;  /* Store pointer, don't copy */
        test_symbol_count++;
    }
}

/* Machine control stubs */
void machine_trigger_reset(unsigned int reset_mode)
{
    (void)reset_mode;
}

/* Keyboard matrix stubs */
void keyboard_set_keyarr(int row, int col, int value)
{
    (void)row;
    (void)col;
    (void)value;
}

/* Alarm system stubs */
typedef unsigned long CLOCK;
struct alarm_context_s;
struct alarm_s;
typedef void (*alarm_callback_t)(CLOCK offset, void *data);

CLOCK maincpu_clk = 0;
struct alarm_context_s *maincpu_alarm_context = NULL;

struct alarm_s *alarm_new(struct alarm_context_s *context, const char *name,
                          alarm_callback_t callback, void *data)
{
    (void)context;
    (void)name;
    (void)callback;
    (void)data;
    return NULL;  /* Return NULL - alarm system not available in tests */
}

void alarm_set(struct alarm_s *alarm, CLOCK cpu_clk)
{
    (void)alarm;
    (void)cpu_clk;
}

void alarm_log_too_many_alarms(void)
{
    /* Stub - should never be called in tests */
}

/* D3: Trap-based dispatch stubs */
#include <stdbool.h>

bool monitor_is_inside_monitor(void)
{
    /* For tests, always return true so direct dispatch is used
     * (trap-based dispatch requires a running emulator main loop) */
    return true;
}

void interrupt_maincpu_trigger_trap(void (*trap_func)(uint16_t, void *data), void *data)
{
    /* For tests, execute the trap handler immediately (simulates main thread)
     * This makes the trap-based dispatch work in test environment */
    if (trap_func != NULL) {
        trap_func(0, data);
    }
}

/* Snapshot stubs for testing */
static char test_snapshot_last_saved[256] = "";
static char test_snapshot_last_loaded[256] = "";
static int test_snapshot_save_result = 0;
static int test_snapshot_load_result = 0;

int machine_write_snapshot(const char *name, int save_roms, int save_disks, int event_mode)
{
    (void)save_roms;
    (void)save_disks;
    (void)event_mode;
    if (name) {
        strncpy(test_snapshot_last_saved, name, sizeof(test_snapshot_last_saved) - 1);
        test_snapshot_last_saved[sizeof(test_snapshot_last_saved) - 1] = '\0';
    }
    return test_snapshot_save_result;
}

int machine_read_snapshot(const char *name, int event_mode)
{
    (void)event_mode;
    if (name) {
        strncpy(test_snapshot_last_loaded, name, sizeof(test_snapshot_last_loaded) - 1);
        test_snapshot_last_loaded[sizeof(test_snapshot_last_loaded) - 1] = '\0';
    }
    return test_snapshot_load_result;
}

/* Test helpers for snapshot stubs */
const char *test_snapshot_get_last_saved(void) { return test_snapshot_last_saved; }
const char *test_snapshot_get_last_loaded(void) { return test_snapshot_last_loaded; }
void test_snapshot_set_save_result(int result) { test_snapshot_save_result = result; }
void test_snapshot_set_load_result(int result) { test_snapshot_load_result = result; }
void test_snapshot_reset(void) {
    test_snapshot_last_saved[0] = '\0';
    test_snapshot_last_loaded[0] = '\0';
    test_snapshot_save_result = 0;
    test_snapshot_load_result = 0;
}

/* archdep stub for config path */
const char *archdep_user_config_path(void)
{
    return "/tmp/vice-test-config";
}
