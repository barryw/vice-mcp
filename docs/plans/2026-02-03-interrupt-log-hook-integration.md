# Interrupt Log Hook Integration Design

## Overview

This document describes the VICE-side integration needed to make the MCP interrupt log tools (`vice.interrupt.log.start`, `vice.interrupt.log.stop`, `vice.interrupt.log.read`) fully functional. The MCP side stores log configuration and entries; the VICE side needs interrupt dispatch hooks to capture interrupt events.

## Current State (MCP-side Complete)

The MCP tools manage interrupt log configuration and storage:

```c
typedef struct {
    uint8_t type;              /* MCP_INTERRUPT_TYPE_IRQ|NMI|BRK */
    unsigned long cycle;       /* CPU cycle when interrupt occurred */
    uint16_t pc;               /* Program counter when interrupted */
    uint16_t vector_address;   /* Vector address ($FFFE, $FFFA) */
    uint16_t handler_address;  /* What the vector pointed to */
} mcp_interrupt_entry_t;

typedef struct {
    char log_id[32];                   /* Unique log identifier */
    uint8_t type_filter;               /* Bitmask of types to log */
    int max_entries;                   /* Maximum entries (default 1000) */
    int entry_count;                   /* Current entry count */
    mcp_interrupt_entry_t *entries;    /* Dynamic array of entries */
    unsigned long start_cycles;        /* Cycle count at log start */
    int active;                        /* 1 if log is active */
} mcp_interrupt_log_config_t;
```

## VICE Integration Required

### 1. Interrupt Dispatch Hook

Add hooks in the interrupt handling code to capture interrupt events:

```c
/* Types for interrupt logging */
#define MCP_INTERRUPT_TYPE_IRQ 0x01
#define MCP_INTERRUPT_TYPE_NMI 0x02
#define MCP_INTERRUPT_TYPE_BRK 0x04

/* Hook function to call from interrupt dispatch */
void mcp_interrupt_log_event(uint8_t type, uint16_t pc,
                             uint16_t vector_address,
                             uint16_t handler_address) {
    int i;

    /* Check all active logs */
    for (i = 0; i < MCP_MAX_INTERRUPT_LOGS; i++) {
        if (!interrupt_log_configs[i].active) continue;

        /* Filter by type */
        if (!(interrupt_log_configs[i].type_filter & type)) continue;

        /* Check if we have room */
        if (interrupt_log_configs[i].entry_count >=
            interrupt_log_configs[i].max_entries) continue;

        /* Record the entry */
        int idx = interrupt_log_configs[i].entry_count++;
        interrupt_log_configs[i].entries[idx].type = type;
        interrupt_log_configs[i].entries[idx].cycle = maincpu_clk;
        interrupt_log_configs[i].entries[idx].pc = pc;
        interrupt_log_configs[i].entries[idx].vector_address = vector_address;
        interrupt_log_configs[i].entries[idx].handler_address = handler_address;
    }
}
```

### 2. IRQ Hook Location

In the IRQ handling code (likely in `interrupt.c` or CPU-specific files):

```c
/* When IRQ is triggered */
static void handle_irq(void) {
    uint16_t pc = MOS6510_REGS_GET_PC(&maincpu_regs);
    uint16_t handler = mem_read(0xFFFE) | (mem_read(0xFFFF) << 8);

    /* Log the interrupt if any logs are active */
    mcp_interrupt_log_event(MCP_INTERRUPT_TYPE_IRQ, pc, 0xFFFE, handler);

    /* ... existing IRQ handling ... */
}
```

### 3. NMI Hook Location

```c
/* When NMI is triggered */
static void handle_nmi(void) {
    uint16_t pc = MOS6510_REGS_GET_PC(&maincpu_regs);
    uint16_t handler = mem_read(0xFFFA) | (mem_read(0xFFFB) << 8);

    /* Log the interrupt if any logs are active */
    mcp_interrupt_log_event(MCP_INTERRUPT_TYPE_NMI, pc, 0xFFFA, handler);

    /* ... existing NMI handling ... */
}
```

### 4. BRK Hook Location

BRK is handled as part of instruction execution:

```c
/* When BRK instruction is executed */
static void execute_brk(void) {
    uint16_t pc = MOS6510_REGS_GET_PC(&maincpu_regs);
    uint16_t handler = mem_read(0xFFFE) | (mem_read(0xFFFF) << 8);

    /* Log the BRK if any logs are active */
    mcp_interrupt_log_event(MCP_INTERRUPT_TYPE_BRK, pc, 0xFFFE, handler);

    /* ... existing BRK handling ... */
}
```

### 5. Hook Points in VICE

The hooks need to be placed at interrupt dispatch points:

1. **interrupt.c** - Generic interrupt handling
2. **6502core.c** or **maincpu.c** - BRK instruction handling
3. **alarm.c** - Timer-based interrupt scheduling

Interrupt vector addresses:
- IRQ/BRK: `$FFFE/$FFFF`
- NMI: `$FFFA/$FFFB`
- RESET: `$FFFC/$FFFD` (not logged)

### 6. Thread Safety

If MCP runs in a separate thread:
- Use mutex protection for interrupt_log_configs array
- Use atomic operations for entry_count
- Consider lock-free ring buffer for high-frequency logging

```c
/* Thread-safe entry addition */
void mcp_interrupt_log_event_safe(uint8_t type, uint16_t pc,
                                   uint16_t vector_address,
                                   uint16_t handler_address) {
    pthread_mutex_lock(&interrupt_log_mutex);
    mcp_interrupt_log_event(type, pc, vector_address, handler_address);
    pthread_mutex_unlock(&interrupt_log_mutex);
}
```

## API Functions Needed

```c
/* Check if any interrupt log is active (fast check) */
int mcp_interrupt_log_any_active(void);

/* Log an interrupt event */
void mcp_interrupt_log_event(uint8_t type, uint16_t pc,
                             uint16_t vector_address,
                             uint16_t handler_address);

/* Check if a specific type is being logged */
int mcp_interrupt_log_type_active(uint8_t type);
```

## Entry Format Examples

### JSON response from log.stop/log.read:

```json
{
  "log_id": "intlog_1",
  "entries": [
    {
      "type": "irq",
      "cycle": 1234567,
      "pc": 49152,
      "vector_address": 65534,
      "handler_address": 59953
    },
    {
      "type": "nmi",
      "cycle": 1234789,
      "pc": 49200,
      "vector_address": 65530,
      "handler_address": 60000
    },
    {
      "type": "brk",
      "cycle": 1235000,
      "pc": 49300,
      "vector_address": 65534,
      "handler_address": 59953
    }
  ],
  "total_interrupts": 3,
  "stopped": true
}
```

## Use Cases

### 1. Debugging Raster IRQ Timing
```
Start: {types: ["irq"], max_entries: 1000}
Run game for one frame
Stop and analyze IRQ timing patterns
```

### 2. Understanding NMI Behavior
```
Start: {types: ["nmi"]}
Run game
Stop and see when NMIs occurred relative to game code
```

### 3. Finding BRK Usage (Software Interrupts)
```
Start: {types: ["brk"]}
Run program
Stop and see all BRK instructions hit (error handlers, etc.)
```

### 4. Full Interrupt Analysis
```
Start: {} (all types, default max_entries)
Run for specific period
Stop and analyze complete interrupt behavior
```

## Performance Impact

Interrupt logging has moderate overhead:
- Array bounds checking per interrupt
- Type filter check per interrupt
- Entry copying per logged interrupt

Mitigations:
- Type filtering to reduce logged events
- max_entries limit to bound memory use
- Fast "any active" check to skip entirely when not logging
- Incremental reads via since_index to avoid repeated full reads

## Implementation Priority

1. Basic IRQ hook integration (required)
2. NMI hook integration (required)
3. BRK hook integration (required)
4. Thread safety (required for separate MCP thread)
5. Performance optimization (future enhancement)

## Related Files

- `/vice/src/mcp/mcp_tools.c` - MCP interrupt log tool implementations
- `/vice/src/mcp/mcp_tools.h` - MCP interrupt log declarations
- `/vice/src/interrupt.c` - Interrupt handling
- `/vice/src/6502core.c` or `/vice/src/maincpu.c` - CPU execution including BRK
- `/vice/src/alarm.c` - Timing-based interrupt scheduling
