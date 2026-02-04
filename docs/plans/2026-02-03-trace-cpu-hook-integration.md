# Trace CPU Hook Integration Design

## Overview

This document describes the VICE-side integration needed to make the MCP trace tools (`vice.trace.start`, `vice.trace.stop`) fully functional. The MCP side stores trace configuration; the VICE side needs CPU hooks to perform actual instruction recording.

## Current State (MCP-side Complete)

The MCP tools manage trace configuration:

```c
typedef struct {
    char trace_id[32];              /* Unique trace identifier */
    char output_file[256];          /* Output file path */
    uint16_t pc_filter_start;       /* Start address for PC filter */
    uint16_t pc_filter_end;         /* End address for PC filter */
    int max_instructions;           /* Maximum instructions to record */
    int include_registers;          /* Include register state in output */
    int instructions_recorded;      /* Counter */
    unsigned long start_cycles;     /* Cycle count at trace start */
    int active;                     /* 1 if trace is active */
} mcp_trace_config_t;
```

## VICE Integration Required

### 1. CPU Execution Hook

Add a hook in the main CPU execution loop (e.g., in `maincpu.c`):

```c
/* In maincpu_mainloop() or equivalent */
static void mcp_trace_instruction(uint16_t pc, uint8_t opcode) {
    /* Called for each instruction if tracing is active */
    if (!mcp_trace_active()) return;

    mcp_trace_config_t *config = mcp_trace_get_active_config();
    if (config == NULL) return;

    /* PC filter check */
    if (pc < config->pc_filter_start || pc > config->pc_filter_end) return;

    /* Max instructions check */
    if (config->instructions_recorded >= config->max_instructions) {
        mcp_trace_stop_internal(config->trace_id);
        return;
    }

    /* Write instruction to file */
    mcp_trace_write_instruction(config, pc, opcode);
    config->instructions_recorded++;
}
```

### 2. Disassembly Integration

Use VICE's existing disassembler to format instructions:

```c
static void mcp_trace_write_instruction(mcp_trace_config_t *config,
                                        uint16_t pc, uint8_t opcode) {
    FILE *f = fopen(config->output_file, "a");
    if (!f) return;

    /* Use mon_disassemble_to_string_ex() or similar */
    char disasm[64];
    mon_disassemble_opcode(e_comp_space, pc, disasm, sizeof(disasm));

    if (config->include_registers) {
        /* Format: $C000: LDA #$00    [A=00 X=FF Y=00 SP=FF P=32] */
        fprintf(f, "$%04X: %-16s [A=%02X X=%02X Y=%02X SP=%02X P=%02X]\n",
                pc, disasm,
                MOS6510_REGS_GET_A(&maincpu_regs),
                MOS6510_REGS_GET_X(&maincpu_regs),
                MOS6510_REGS_GET_Y(&maincpu_regs),
                MOS6510_REGS_GET_SP(&maincpu_regs),
                MOS6510_REGS_GET_FLAGS(&maincpu_regs));
    } else {
        /* Format: $C000: LDA #$00 */
        fprintf(f, "$%04X: %s\n", pc, disasm);
    }

    fclose(f);
}
```

### 3. Hook Points

The hook needs to be called from the CPU execution loop. Possible locations:

1. **maincpu.c** - Main CPU emulation loop
2. **6502core.c** - Core 6502/6510 emulation
3. **interrupt.c** - Could use IK_MONITOR style interrupt

Recommended approach: Add a callback mechanism similar to how the monitor breakpoint system works:

```c
/* In monitor-related code */
void mcp_trace_cpu_hook(uint16_t pc, uint8_t opcode) {
    /* Check if any traces are active */
    if (mcp_trace_any_active()) {
        mcp_trace_instruction(pc, opcode);
    }
}
```

### 4. File I/O Considerations

For performance, consider:
- Buffered writes (don't fopen/fclose each instruction)
- File handle caching in trace config
- Async I/O if available
- Memory-mapped files for very large traces

### 5. Thread Safety

If MCP runs in a separate thread:
- Use mutex protection for trace_configs array
- Use atomic operations for instructions_recorded counter
- Consider lock-free queue for instruction writes

## API Functions Needed

```c
/* Query if any trace is active (fast check) */
int mcp_trace_any_active(void);

/* Get active trace config for current PC (may return NULL) */
mcp_trace_config_t* mcp_trace_get_config_for_pc(uint16_t pc);

/* Internal stop (when max_instructions reached) */
void mcp_trace_stop_internal(const char *trace_id);

/* Write instruction to trace file */
void mcp_trace_write_instruction(mcp_trace_config_t *config,
                                 uint16_t pc, uint8_t opcode);
```

## Output Format Examples

### Basic trace (include_registers=false):
```
$C000: LDA #$00
$C002: STA $D020
$C005: LDX #$10
$C007: DEX
$C008: BNE $C007
```

### With registers (include_registers=true):
```
$C000: LDA #$00    [A=00 X=FF Y=00 SP=FF P=32]
$C002: STA $D020   [A=00 X=FF Y=00 SP=FF P=32]
$C005: LDX #$10    [A=00 X=10 Y=00 SP=FF P=30]
$C007: DEX         [A=00 X=0F Y=00 SP=FF P=30]
$C008: BNE $C007   [A=00 X=0F Y=00 SP=FF P=30]
```

## Performance Impact

Tracing has significant overhead:
- File I/O for each instruction
- Disassembly string formatting
- Register state capture

Mitigations:
- PC filter to limit scope
- max_instructions limit
- Buffered I/O
- Consider binary format for high-speed tracing

## Implementation Priority

1. Basic hook integration (required)
2. File output with buffering (required)
3. PC filtering (required - already in config)
4. Register capture (optional - already in config)
5. Binary format option (future enhancement)

## Related Files

- `/vice/src/mcp/mcp_tools.c` - MCP trace tool implementations
- `/vice/src/mcp/mcp_tools.h` - MCP trace declarations
- `/vice/src/maincpu.c` - Main CPU loop (hook location)
- `/vice/src/monitor/mon_disassemble.c` - Disassembly functions
