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

/* Memory search test support - forward declaration and storage */
static void test_memory_init(void);
static uint8_t test_memory_buffer[65536];
static int test_memory_buffer_initialized = 0;

/* Memory stubs */
uint8_t mem_read(uint16_t addr)
{
    if (!test_memory_buffer_initialized) {
        test_memory_init();
    }
    return test_memory_buffer[addr];
}

void mem_store(uint16_t addr, uint8_t value)
{
    if (!test_memory_buffer_initialized) {
        test_memory_init();
    }
    test_memory_buffer[addr] = value;
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
    (void)context;
    if (!test_memory_buffer_initialized) {
        test_memory_init();
    }
    return test_memory_buffer[addr];
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

/* Vsync callback stub - used for keyboard auto-release */
typedef void (*vsync_callback_func_t)(void *param);
void vsync_on_vsync_do(vsync_callback_func_t callback_func, void *callback_param)
{
    (void)callback_func;
    (void)callback_param;
    /* In tests, we don't actually call the callback - just record that it was scheduled */
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

int util_strncasecmp(const char *s1, const char *s2, size_t n)
{
    return strncasecmp(s1, s2, n);
}

/* MCP step mode stubs - actual implementations since tests verify flag behavior */
static int test_mcp_step_active = 0;

int mcp_is_step_active(void)
{
    return test_mcp_step_active;
}

void mcp_clear_step_active(void)
{
    test_mcp_step_active = 0;
}

void mcp_set_step_active(int active)
{
    test_mcp_step_active = active;
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

/* =============================================================================
 * Checkpoint/Breakpoint Test Support
 *
 * Provides simulated checkpoint functionality for testing watch_add
 * and other checkpoint-related tools.
 * ============================================================================= */

/* Track checkpoint state for testing */
static int test_checkpoint_counter = 0;
static int test_checkpoint_last_num = -1;
static int test_checkpoint_last_has_condition = 0;

/* Simulated checkpoint structure for testing
 * MUST match layout of mon_checkpoint_t from mon_breakpoint.h exactly!
 *
 * struct mon_checkpoint_s {
 *     int checknum;           // offset 0
 *     MON_ADDR start_addr;    // offset 4 (MON_ADDR = unsigned int)
 *     MON_ADDR end_addr;      // offset 8
 *     int hit_count;          // offset 12
 *     int ignore_count;       // offset 16
 *     cond_node_t *condition; // offset 20 (24 on 64-bit due to alignment)
 *     char *command;          // offset 28 (32 on 64-bit)
 *     bool stop;              // offset 36 (40 on 64-bit)
 *     bool enabled;           // offset 37 (41 on 64-bit)
 *     bool check_load;
 *     bool check_store;
 *     bool check_exec;
 *     bool temporary;
 * };
 */
typedef struct {
    int checknum;
    unsigned int start_addr;
    unsigned int end_addr;
    int hit_count;
    int ignore_count;
    void *condition;       /* cond_node_t* */
    char *command;
    unsigned char stop;    /* bool - 1 byte */
    unsigned char enabled; /* bool - 1 byte */
    unsigned char check_load;
    unsigned char check_store;
    unsigned char check_exec;
    unsigned char temporary;
} test_checkpoint_t;

/* Storage for simulated checkpoints */
#define MAX_TEST_CHECKPOINTS 64
static test_checkpoint_t test_checkpoints[MAX_TEST_CHECKPOINTS];

/* Reset checkpoint test state */
void test_checkpoint_reset(void)
{
    int i;
    test_checkpoint_counter = 0;
    test_checkpoint_last_num = -1;
    test_checkpoint_last_has_condition = 0;
    for (i = 0; i < MAX_TEST_CHECKPOINTS; i++) {
        test_checkpoints[i].checknum = 0;
        test_checkpoints[i].enabled = 1;  /* Default to enabled */
    }
}

/* Note: test_checkpoint_groups_reset() is defined in test_mcp_tools.c
 * because it needs to call mcp_checkpoint_groups_reset() from libmcp.a,
 * and test_mcp_transport doesn't link libmcp.a. */

/* Get the last checkpoint number created */
int test_checkpoint_get_last_num(void)
{
    return test_checkpoint_last_num;
}

/* Check if last checkpoint has a condition set */
int test_checkpoint_has_condition(void)
{
    return test_checkpoint_last_has_condition;
}

/* Phase 2.1: Checkpoint/Breakpoint stubs */
int mon_breakpoint_add_checkpoint(unsigned int start, unsigned int end, int stop, int operation, int temporary, int do_print)
{
    (void)do_print;
    /* Return incrementing checkpoint number for testing */
    test_checkpoint_last_num = ++test_checkpoint_counter;
    test_checkpoint_last_has_condition = 0;  /* Reset condition flag for new checkpoint */

    /* Store checkpoint in our test array */
    if (test_checkpoint_last_num > 0 && test_checkpoint_last_num <= MAX_TEST_CHECKPOINTS) {
        int idx = test_checkpoint_last_num - 1;
        test_checkpoints[idx].checknum = test_checkpoint_last_num;
        test_checkpoints[idx].start_addr = start;
        test_checkpoints[idx].end_addr = end;
        test_checkpoints[idx].stop = stop;
        test_checkpoints[idx].check_exec = (operation & 4) != 0;  /* e_exec */
        test_checkpoints[idx].check_load = (operation & 1) != 0;  /* e_load */
        test_checkpoints[idx].check_store = (operation & 2) != 0; /* e_store */
        test_checkpoints[idx].temporary = temporary;
        test_checkpoints[idx].enabled = 1;  /* Default enabled */
    }

    return test_checkpoint_last_num;
}

void mon_breakpoint_delete_checkpoint(unsigned int id)
{
    /* Mark checkpoint as deleted by setting checknum to 0 */
    if (id > 0 && id <= MAX_TEST_CHECKPOINTS) {
        test_checkpoints[id - 1].checknum = 0;
    }
}

void *mon_breakpoint_find_checkpoint(unsigned int id)
{
    /* Return pointer to our test checkpoint structure */
    if (id > 0 && id <= (unsigned int)test_checkpoint_counter) {
        int idx = id - 1;
        if (test_checkpoints[idx].checknum == (int)id) {
            return &test_checkpoints[idx];
        }
    }
    return NULL;
}

void *mon_breakpoint_checkpoint_list_get(void)
{
    return NULL;
}

void mon_breakpoint_switch_checkpoint(int op, unsigned int id)
{
    /* op=1 for enable, op=2 for disable */
    if (id > 0 && id <= MAX_TEST_CHECKPOINTS) {
        int idx = id - 1;
        if (test_checkpoints[idx].checknum == (int)id) {
            test_checkpoints[idx].enabled = (op == 1) ? 1 : 0;
        }
    }
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
    /* Track that a condition was set */
    if (cnode != NULL) {
        test_checkpoint_last_has_condition = 1;
    }
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

void machine_set_restore_key(int value)
{
    (void)value;
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

/* archdep_mkdir stub - just succeeds */
int archdep_mkdir(const char *pathname, int mode)
{
    (void)pathname;
    (void)mode;
    return 0;  /* Success */
}

/* util_join_paths stub - simple concatenation for testing */
char *util_join_paths(const char *path, ...)
{
    va_list args;
    const char *component;
    size_t total_len = 0;
    char *result;
    char *p;

    /* Calculate total length needed */
    total_len = strlen(path);
    va_start(args, path);
    while ((component = va_arg(args, const char *)) != NULL) {
        total_len += 1 + strlen(component);  /* +1 for '/' */
    }
    va_end(args);

    /* Allocate and build result */
    result = lib_malloc(total_len + 1);
    if (result == NULL) {
        return NULL;
    }
    strcpy(result, path);
    p = result + strlen(result);

    va_start(args, path);
    while ((component = va_arg(args, const char *)) != NULL) {
        *p++ = '/';
        strcpy(p, component);
        p += strlen(component);
    }
    va_end(args);

    return result;
}

/* =============================================================================
 * Memory search test support
 *
 * Provides a configurable memory buffer that mem_read() and mem_bank_peek()
 * can use to simulate specific memory patterns for testing memory.search.
 * The static variables are declared above near mem_read().
 * ============================================================================= */

/* Initialize test memory buffer with default pattern */
static void test_memory_init(void)
{
    int i;
    for (i = 0; i < 65536; i++) {
        test_memory_buffer[i] = (uint8_t)(i & 0xFF);  /* Simple pattern */
    }
    test_memory_buffer_initialized = 1;
}

/* Write bytes to test memory at specified address - available to tests */
void test_memory_set(uint16_t addr, const uint8_t *data, size_t len)
{
    size_t i;
    if (!test_memory_buffer_initialized) {
        test_memory_init();
    }
    for (i = 0; i < len && (addr + i) < 65536; i++) {
        test_memory_buffer[addr + i] = data[i];
    }
}

/* Write a single byte to test memory */
void test_memory_set_byte(uint16_t addr, uint8_t value)
{
    if (!test_memory_buffer_initialized) {
        test_memory_init();
    }
    test_memory_buffer[addr] = value;
}

/* Clear test memory to all zeros */
void test_memory_clear(void)
{
    memset(test_memory_buffer, 0, sizeof(test_memory_buffer));
    test_memory_buffer_initialized = 1;
}

/* Get a single byte from test memory */
uint8_t test_memory_get_byte(uint16_t addr)
{
    if (!test_memory_buffer_initialized) {
        test_memory_init();
    }
    return test_memory_buffer[addr];
}

/* =============================================================================
 * Cycles stopwatch test support
 *
 * Simulates the monitor stopwatch functionality for testing the
 * vice.cycles.stopwatch tool without requiring the full VICE runtime.
 * ============================================================================= */

/* Simulated stopwatch state */
static unsigned long test_stopwatch_start = 0;
static unsigned long test_stopwatch_current_clock = 0;

/* Reset test stopwatch to initial state */
void test_stopwatch_reset(void)
{
    test_stopwatch_start = 0;
    test_stopwatch_current_clock = 0;
}

/* Set the elapsed cycles for testing */
void test_stopwatch_set_cycles(unsigned long cycles)
{
    /* Set current clock so that elapsed = cycles */
    test_stopwatch_current_clock = test_stopwatch_start + cycles;
}

/* Get the current elapsed cycles */
unsigned long test_stopwatch_get_cycles(void)
{
    return test_stopwatch_current_clock - test_stopwatch_start;
}

/* Monitor stopwatch stubs - called by the actual tool implementation */
void mon_stopwatch_reset(void)
{
    /* Reset: set start to current clock, so elapsed becomes 0 */
    test_stopwatch_start = test_stopwatch_current_clock;
}

unsigned long mon_stopwatch_get_elapsed(void)
{
    return test_stopwatch_current_clock - test_stopwatch_start;
}

/* =============================================================================
 * Snapshot Memory Test Support
 *
 * Provides test support for the vice.memory.compare snapshot mode.
 * Allows tests to create mock VSF (VICE Snapshot Format) files with
 * specific memory contents for comparing against live memory.
 * ============================================================================= */

/* Storage for mock snapshot memory - 64KB for C64 memory */
static uint8_t test_snapshot_memory[65536];
static int test_snapshot_memory_valid = 0;

/* Set snapshot memory contents for testing */
void test_snapshot_memory_set(uint16_t addr, const uint8_t *data, size_t len)
{
    size_t i;
    for (i = 0; i < len && (addr + i) < 65536; i++) {
        test_snapshot_memory[addr + i] = data[i];
    }
    test_snapshot_memory_valid = 1;
}

/* Set a single byte in snapshot memory */
void test_snapshot_memory_set_byte(uint16_t addr, uint8_t value)
{
    test_snapshot_memory[addr] = value;
    test_snapshot_memory_valid = 1;
}

/* Clear snapshot memory to all zeros */
void test_snapshot_memory_clear(void)
{
    memset(test_snapshot_memory, 0, sizeof(test_snapshot_memory));
    test_snapshot_memory_valid = 1;
}

/* Get a single byte from snapshot memory */
uint8_t test_snapshot_memory_get_byte(uint16_t addr)
{
    return test_snapshot_memory[addr];
}

/* Invalidate snapshot memory (simulate file not found) */
void test_snapshot_memory_invalidate(void)
{
    test_snapshot_memory_valid = 0;
}

/* Check if snapshot memory is valid */
int test_snapshot_memory_is_valid(void)
{
    return test_snapshot_memory_valid;
}

/* Create a mock VSF file at the given path with the current test_snapshot_memory contents.
 * This creates a minimal valid VSF file structure that the real parser can read.
 *
 * VSF format:
 *   - Magic: "VICE Snapshot File\032" (19 bytes)
 *   - Version: major (1), minor (1)
 *   - Machine name: "C64" padded to 16 bytes
 *   - VICE version magic: "VICE Version\032" (13 bytes)
 *   - VICE version: 4 bytes + revision 4 bytes
 *   - C64MEM module:
 *     - Name: "C64MEM" padded to 16 bytes
 *     - Major/minor version: 2 bytes
 *     - Size: 4 bytes (little-endian, includes header = 22 + data size)
 *     - pport.data, pport.dir, exrom, game: 4 bytes
 *     - RAM: 65536 bytes
 *     - More port state data (14 bytes for version 0.1)
 *
 * Returns 0 on success, -1 on failure.
 */
int test_create_mock_vsf(const char *path)
{
    FILE *f;
    static const char magic[] = "VICE Snapshot File\032";
    static const char version_magic[] = "VICE Version\032";
    static const char machine_name[16] = "C64\0\0\0\0\0\0\0\0\0\0\0\0\0";
    static const char module_name[16] = "C64MEM\0\0\0\0\0\0\0\0\0\0";
    uint8_t header[4] = {0, 0, 0, 0};  /* pport.data, pport.dir, exrom, game */
    uint8_t port_state[14] = {0};  /* Additional port state for v0.1 */
    uint32_t module_size;
    uint8_t size_bytes[4];

    f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }

    /* Write file header */
    fwrite(magic, 1, 19, f);  /* Magic string */
    fputc(1, f); fputc(0, f);  /* Version 1.0 */
    fwrite(machine_name, 1, 16, f);  /* Machine name */

    /* Write VICE version info */
    fwrite(version_magic, 1, 13, f);
    fputc(3, f); fputc(9, f); fputc(0, f); fputc(0, f);  /* VICE 3.9.0.0 */
    fputc(0, f); fputc(0, f); fputc(0, f); fputc(0, f);  /* Revision 0 */

    /* Write C64MEM module */
    fwrite(module_name, 1, 16, f);  /* Module name */
    fputc(0, f);  /* Major version 0 */
    fputc(1, f);  /* Minor version 1 */

    /* Module size: header(22) + pport(4) + RAM(65536) + port_state(14) */
    module_size = 22 + 4 + 65536 + 14;
    size_bytes[0] = module_size & 0xFF;
    size_bytes[1] = (module_size >> 8) & 0xFF;
    size_bytes[2] = (module_size >> 16) & 0xFF;
    size_bytes[3] = (module_size >> 24) & 0xFF;
    fwrite(size_bytes, 1, 4, f);

    /* Module data */
    fwrite(header, 1, 4, f);  /* pport.data, pport.dir, exrom, game */
    fwrite(test_snapshot_memory, 1, 65536, f);  /* RAM */
    fwrite(port_state, 1, 14, f);  /* Additional port state */

    fclose(f);
    return 0;
}

/* =============================================================================
 * UI Pause API stubs
 *
 * Simulates the UI pause functionality for testing execution control tools.
 * ============================================================================= */

static int test_ui_pause_state = 0;

int ui_pause_active(void)
{
    return test_ui_pause_state;
}

void ui_pause_enable(void)
{
    test_ui_pause_state = 1;
}

void ui_pause_disable(void)
{
    test_ui_pause_state = 0;
}

/* Test helper to reset UI pause state */
void test_ui_pause_reset(void)
{
    test_ui_pause_state = 0;
}

/* Test helper to set UI pause state directly */
void test_ui_pause_set(int paused)
{
    test_ui_pause_state = paused;
}
