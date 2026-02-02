/*
 * vice_stubs.c - Stub implementations of VICE functions for testing
 *
 * This file provides minimal stub implementations of VICE functions
 * needed to link MCP tests without requiring the full VICE runtime.
 *
 * NOTE: This uses forward declarations to avoid complex VICE header dependencies.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

/* Forward declarations - avoid including VICE headers */
typedef signed int log_t;

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
