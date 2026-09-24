/*
 * mcp_tools_execution.c - MCP execution control and register tool handlers
 *
 * Written by:
 *  Barry Walker <barrywalker@gmail.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "mcp_tools_internal.h"

#include "maincpu.h"
#include "mos6510.h"
#include "monitor.h"  /* For mon_instructions_step/next, exit_mon, mcp_hold_paused */
#include "interrupt.h"  /* For interrupt_maincpu_trigger_trap */
#include "mainlock.h"   /* For mainlock_obtain/release */
#include "vsync.h"      /* For vsync_on_vsync_do */

#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "ui.h"       /* For ui_pause_enable/disable/active */
#include "archdep_tick.h"  /* For tick_now/tick_now_delta/tick_sleep/tick_per_second */

/* mcp_step_active state is defined in monitor.c to avoid a circular link
 * dependency between libmonitor.a and libmcp.a (GNU ld limitation).
 * Use mcp_set_step_active() / mcp_is_step_active() from monitor.h. */

/* =========================================================================
 * Execution Control Tools
 * ========================================================================= */

cJSON* mcp_tool_execution_run(cJSON *params)
{
    cJSON *response;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.execution.run");

    /* Handle both UI pause mode and monitor mode.
     * The emulator can be paused in two ways:
     * 1. UI pause - controlled by ui_pause_enable/disable
     * 2. Monitor mode - controlled by exit_mon variable
     * We need to handle both cases. */

    /* Disable UI pause if active */
    if (ui_pause_active()) {
        log_message(mcp_tools_log, "Disabling UI pause");
        ui_pause_disable();
    }

    /* Also signal monitor to exit if in monitor mode */
    exit_mon = exit_mon_continue;

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "Execution resumed");

    return response;
}

/* Trap handler: runs on the emulator thread at the next instruction
 * boundary and holds it there until a client calls vice.execution.run.
 * Only while the pause flag is still up: vice.execution.pause can land
 * while a step or a frame advance is stopping the machine, which then
 * holds before this trap runs, and a vice.execution.run drops the flag
 * before the trap gets its turn. mcp_hold_paused() would raise the flag
 * again and stop the machine that was just told to run. */
static void mcp_pause_trap(uint16_t addr, void *data)
{
    (void)addr;
    (void)data;
    if (ui_pause_active()) {
        mcp_hold_paused();
    }
}

cJSON* mcp_tool_execution_pause(cJSON *params)
{
    cJSON *response;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.execution.pause");

    /* Use UI pause to stop the emulator without entering monitor mode.
     * This keeps the emulator window visible (no monitor popup) while
     * still stopping CPU execution. The transport layer acquires the
     * mainlock when dispatching during UI pause for thread safety.
     *
     * ui_pause_enable() only raises the flag: the GTK3 UI honours it at the
     * next vsync and the headless UI never does. Queue a trap as well, so
     * the emulator thread stops at the next instruction boundary. A trap
     * scheduled from inside a trap (this tool usually runs in one) is run
     * on the next cycle by interrupt_do_trap(). */
    if (!ui_pause_active()) {
        log_message(mcp_tools_log, "Enabling UI pause");
        ui_pause_enable();
        interrupt_maincpu_trigger_trap(mcp_pause_trap, NULL);
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "Execution paused");

    return response;
}

/* A step on a held machine is waited for, polling every 0.2 ms for up to
 * 2 s by the clock: a count of polls would stretch with every oversleep of
 * tick_sleep(), to tens of seconds on Windows. The count is capped to stay
 * well inside that at any speed near normal (running further is what a
 * checkpoint and vice.execution.run are for). A step over a subroutine
 * counts as one instruction however long the subroutine runs; only the
 * wait bounds it. */
#define MCP_STEP_COUNT_MAX  10000
#define MCP_STEP_TIMEOUT_S  2

cJSON* mcp_tool_execution_step(cJSON *params)
{
    cJSON *response;
    cJSON *count_item, *step_over_item;
    int count = 1;
    bool step_over = false;
    int was_held;
    int completed = 0;
    int stopped_early = 0;
    int timed_out = 0;
    tick_t start;

    log_message(mcp_tools_log, "Handling vice.execution.step");

    /* Parse optional count parameter */
    if (params != NULL) {
        count_item = cJSON_GetObjectItem(params, "count");
        if (count_item != NULL && cJSON_IsNumber(count_item)) {
            count = count_item->valueint;
            if (count < 1) {
                count = 1;
            }
            if (count > MCP_STEP_COUNT_MAX) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS,
                                 "count must be at most 10000; to run further, "
                                 "stop at a checkpoint with vice.execution.run");
            }
        }

        step_over_item = cJSON_GetObjectItem(params, "stepOver");
        if (step_over_item != NULL && cJSON_IsBool(step_over_item)) {
            step_over = cJSON_IsTrue(step_over_item);
        }
    }

    /* Set MCP step mode flag - this tells monitor_check_icount() to use
     * mcp_hold_paused() instead of monitor_startup() when stepping completes,
     * preventing the monitor window from opening during MCP operations. */
    mcp_set_step_active(1);

    /* Use VICE's step functions:
     * - mon_instructions_step: Step into subroutines
     * - mon_instructions_next: Step over subroutines */
    if (step_over) {
        mon_instructions_next(count);
    } else {
        mon_instructions_step(count);
    }

    /* Those only arm the count. A machine stopped by a checkpoint hit, a
     * completed step, vice.execution.pause or vice.frame.advance is held
     * on the emulator thread in mcp_hold_paused(), and stays held until
     * the pause flag drops: without this, the tool reports ok and nothing
     * moves. Release the hold, give the mainlock up so the emulator thread
     * can take it, and wait for the hold to take again, so the reply comes
     * back after the step, with the registers exported and vice.registers.get
     * truthful. A running machine is left alone: the count stops it. */
    was_held = ui_pause_active();
    if (was_held) {
        ui_pause_disable();
        mainlock_release();
        start = tick_now();
        while (!ui_pause_active()
               && tick_now_delta(start) < MCP_STEP_TIMEOUT_S * tick_per_second()) {
            tick_sleep(tick_per_second() / 5000);   /* 0.2 ms; a step is microseconds */
        }
        mainlock_obtain();
        if (!ui_pause_active()) {
            /* Still running: a step over a subroutine that takes its time,
             * or a count the machine's speed does not get through. Do not
             * leave it running on a reply that says the step is over: drop
             * the rest of the step and stop the machine where it is, the
             * way vice.execution.pause does. */
            timed_out = 1;
            mcp_cancel_step();
            ui_pause_enable();
            interrupt_maincpu_trigger_trap(mcp_pause_trap, NULL);
        } else if (mcp_is_step_active()) {
            /* Held, but not by the step, which clears the flag before it
             * holds (monitor_check_icount()): a checkpoint got there first.
             * Drop the rest of the step, or it stops the machine again
             * after the next resume. */
            stopped_early = 1;
            mcp_cancel_step();
        } else {
            completed = 1;
        }
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "instructions", count);
    cJSON_AddBoolToObject(response, "step_over", step_over);
    if (was_held) {
        cJSON_AddBoolToObject(response, "completed", completed);
    }
    if (completed || stopped_early) {
        cJSON_AddNumberToObject(response, "PC", maincpu_get_pc());
    }
    if (stopped_early) {
        cJSON_AddBoolToObject(response, "stopped_early", true);
        cJSON_AddStringToObject(response, "message",
            "Something else stopped the machine before the step finished "
            "(a checkpoint?); the rest of the step was dropped and the "
            "machine is paused there");
    }
    if (timed_out) {
        /* No PC: the machine stops at the next instruction boundary, after
         * this reply has gone. */
        cJSON_AddBoolToObject(response, "timed_out", true);
        cJSON_AddStringToObject(response, "message",
            "The step did not finish within 2 s; the rest of it was dropped "
            "and the machine has been paused at the next instruction boundary");
    }

    return response;
}

/* =========================================================================
 * Frame Advance
 * ========================================================================= */

#define MCP_FRAME_ADVANCE_MAX 1000

/* How long the tool waits for a frame to end before it gives up (far
 * beyond a frame at any speed short of a stopped machine), and how often
 * it looks for a stop that did not come from its own trap. */
#define MCP_FRAME_ADVANCE_TIMEOUT_MS 2000
#define MCP_FRAME_ADVANCE_POLL_MS    2

/* How an advance ended */
typedef enum {
    MCP_FRAME_BOUNDARY,     /* stopped at the boundary after the last frame */
    MCP_FRAME_STOPPED,      /* something else stopped the machine first */
    MCP_FRAME_TIMED_OUT     /* a frame did not end within the timeout */
} mcp_frame_end_t;

/* Shared between the tool (HTTP thread) and the callbacks (emulator
 * thread). Tool dispatch is serialised, so there is one advance at a time,
 * but a callback queued for an earlier advance can still be pending:
 * nothing cancels the vsync callback of an advance that a checkpoint cut
 * short or that timed out. Each advance takes a new sequence number and
 * hands it to its callbacks, and the trap acts only for the current one. */
static struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned int seq;   /* the current advance */
    int active;         /* the tool is waiting for it */
    int frames;         /* frames it runs */
    int done;           /* frames that have ended */
    int stopped;        /* the emulator thread has taken hold after the last */
} frame_advance = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0, 0, 0 };

static void mcp_frame_advance_vsync(void *param);

/* Trap: runs on the emulator thread at the first instruction boundary
 * after a vsync, with the registers exported. Count the frame. After the
 * last one, raise the pause flag so the transport switches to mainlock
 * dispatch, wake the tool, and hold; before it, keep running. */
static void mcp_frame_advance_trap(uint16_t addr, void *data)
{
    int last;

    (void)addr;

    pthread_mutex_lock(&frame_advance.mutex);
    if (!frame_advance.active || frame_advance.seq != (unsigned int)(uintptr_t)data) {
        /* Queued for an advance that has ended. Stopping here would park
         * a machine nobody is waiting for, or end the current advance
         * before its frames have run. */
        pthread_mutex_unlock(&frame_advance.mutex);
        return;
    }
    frame_advance.done++;
    last = frame_advance.done >= frame_advance.frames;
    if (last) {
        ui_pause_enable();
        frame_advance.stopped = 1;
        pthread_cond_signal(&frame_advance.cond);
    }
    pthread_mutex_unlock(&frame_advance.mutex);

    if (last) {
        mcp_hold_paused();
    } else {
        /* Count the next frame. Queued here, after the vsync, the callback
         * waits for the next one; queued from the vsync callback it would
         * run again in the same vsync (execute_vsync_callbacks loops until
         * the queue is empty). */
        vsync_on_vsync_do(mcp_frame_advance_vsync, data);
    }
}

/* Vsync callback: a frame is over, count it at the next instruction. The
 * parameter is the sequence number of the advance that queued it. */
static void mcp_frame_advance_vsync(void *param)
{
    interrupt_maincpu_trigger_trap(mcp_frame_advance_trap, param);
}

/* t += ms, for the absolute deadlines pthread_cond_timedwait takes */
static void mcp_timespec_add_ms(struct timespec *t, long ms)
{
    t->tv_sec += ms / 1000;
    t->tv_nsec += (ms % 1000) * 1000000L;
    if (t->tv_nsec >= 1000000000L) {
        t->tv_sec++;
        t->tv_nsec -= 1000000000L;
    }
}

static int mcp_timespec_before(const struct timespec *a, const struct timespec *b)
{
    return a->tv_sec < b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec < b->tv_nsec);
}

/* Run `frames` frames and stop at the first instruction boundary after the
 * last. Called with the machine in UI pause and the mainlock held by this
 * thread; gives the lock up while the frames run and holds it again on
 * return, with the machine stopped, or on MCP_FRAME_TIMED_OUT asked to
 * stop at the next instruction boundary. *done is set to the number of
 * frames that ended. */
static mcp_frame_end_t mcp_frame_advance_run(int frames, int *done)
{
    struct timespec deadline, now, wake;
    mcp_frame_end_t result;
    unsigned int seq;
    int seen = 0;

    pthread_mutex_lock(&frame_advance.mutex);
    seq = ++frame_advance.seq;
    frame_advance.active = 1;
    frame_advance.frames = frames;
    frame_advance.done = 0;
    frame_advance.stopped = 0;
    pthread_mutex_unlock(&frame_advance.mutex);

    /* One release for all the frames: the trap counts them. Stopping and
     * releasing the machine between frames races the emulator thread
     * where the mainlock does nothing (builds without USE_VICE_THREAD,
     * such as the headless UI): the pause flag, dropped for the next frame
     * before the trap has reached mcp_hold_paused(), is raised again there
     * and the machine held, and the next frame ends before it starts. */
    vsync_on_vsync_do(mcp_frame_advance_vsync, (void *)(uintptr_t)seq);
    ui_pause_disable();
    mainlock_release();

    /* CLOCK_REALTIME: pthread_cond_timedwait takes an absolute deadline on
     * that clock by default, as in mcp_transport.c. */
    clock_gettime(CLOCK_REALTIME, &deadline);
    mcp_timespec_add_ms(&deadline, MCP_FRAME_ADVANCE_TIMEOUT_MS);

    pthread_mutex_lock(&frame_advance.mutex);
    for (;;) {
        if (frame_advance.stopped) {
            result = MCP_FRAME_BOUNDARY;
            break;
        }
        /* Anything else that stops the machine raises the pause flag too,
         * and knows nothing of this advance: a stopping checkpoint or
         * watchpoint, which the monitor holds in mcp_hold_paused(), or a
         * pause from the UI. Look for it between short waits, as the
         * transport does for queued traps, rather than sleep out the
         * timeout with every other MCP call queued behind this one. */
        if (ui_pause_active()) {
            result = MCP_FRAME_STOPPED;
            break;
        }
        clock_gettime(CLOCK_REALTIME, &now);
        if (frame_advance.done != seen) {
            /* The timeout is per frame */
            seen = frame_advance.done;
            deadline = now;
            mcp_timespec_add_ms(&deadline, MCP_FRAME_ADVANCE_TIMEOUT_MS);
        } else if (!mcp_timespec_before(&now, &deadline)) {
            result = MCP_FRAME_TIMED_OUT;
            break;
        }
        wake = now;
        mcp_timespec_add_ms(&wake, MCP_FRAME_ADVANCE_POLL_MS);
        if (mcp_timespec_before(&deadline, &wake)) {
            wake = deadline;
        }
        pthread_cond_timedwait(&frame_advance.cond, &frame_advance.mutex, &wake);
    }
    frame_advance.active = 0;
    *done = frame_advance.done;
    pthread_mutex_unlock(&frame_advance.mutex);

    mainlock_obtain();

    if (result == MCP_FRAME_TIMED_OUT) {
        if (ui_pause_active()) {
            /* Stopped in the instant after the deadline */
            result = MCP_FRAME_STOPPED;
        } else {
            /* Leave the machine stopped, as the call found it, the way
             * vice.execution.pause does. Clearing active above keeps the
             * pending callback from stopping it again later. */
            log_message(mcp_tools_log, "vice.frame.advance: no frame boundary within %d ms, pausing",
                        MCP_FRAME_ADVANCE_TIMEOUT_MS);
            ui_pause_enable();
            interrupt_maincpu_trigger_trap(mcp_pause_trap, NULL);
        }
    }

    return result;
}

cJSON* mcp_tool_frame_advance(cJSON *params)
{
    cJSON *response, *frames_item;
    mcp_frame_end_t result;
    int frames = 1;
    int done = 0;

    log_message(mcp_tools_log, "Handling vice.frame.advance");

    if (params != NULL) {
        frames_item = cJSON_GetObjectItem(params, "frames");
        if (frames_item != NULL) {
            /* valueint truncates: 1.9 would run one frame */
            if (!cJSON_IsNumber(frames_item)
                || frames_item->valuedouble < 1
                || frames_item->valuedouble > MCP_FRAME_ADVANCE_MAX
                || frames_item->valuedouble != (double)frames_item->valueint) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS,
                                 "frames must be a whole number from 1 to 1000");
            }
            frames = frames_item->valueint;
        }
    }

    if (!ui_pause_active()) {
        return mcp_error(MCP_ERROR_EMULATOR_RUNNING,
            "Emulator is not stopped. Stop it first with vice.execution.pause, "
            "a stopping checkpoint or vice.execution.step");
    }

    result = mcp_frame_advance_run(frames, &done);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "frames", done);
    if (result == MCP_FRAME_BOUNDARY) {
        cJSON_AddNumberToObject(response, "PC", maincpu_get_pc());
        cJSON_AddStringToObject(response, "message", "Stopped at the frame boundary");
    } else if (result == MCP_FRAME_STOPPED) {
        cJSON_AddNumberToObject(response, "PC", maincpu_get_pc());
        cJSON_AddBoolToObject(response, "stopped_early", true);
        cJSON_AddStringToObject(response, "message",
            "Something else stopped the machine before the frame boundary "
            "(a checkpoint?); it is paused there");
    } else {
        /* No PC: the machine stops at the next instruction boundary, after
         * this reply has gone. */
        cJSON_AddBoolToObject(response, "stopped_early", true);
        cJSON_AddBoolToObject(response, "timed_out", true);
        cJSON_AddStringToObject(response, "message",
            "A frame did not end within 2 s; the machine has been paused "
            "at the next instruction boundary");
    }

    return response;
}

/* =========================================================================
 * Register Access Tools
 * ========================================================================= */

cJSON* mcp_tool_registers_get(cJSON *params)
{
    cJSON *response;
    unsigned int pc;
    unsigned int a;
    unsigned int x;
    unsigned int y;
    unsigned int sp;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.registers.get");

    /* Read CPU registers from VICE */
    pc = maincpu_get_pc();
    a = maincpu_get_a();
    x = maincpu_get_x();
    y = maincpu_get_y();
    sp = maincpu_get_sp();

    /* Build JSON response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(response, "PC", pc);
    cJSON_AddNumberToObject(response, "A", a);
    cJSON_AddNumberToObject(response, "X", x);
    cJSON_AddNumberToObject(response, "Y", y);
    cJSON_AddNumberToObject(response, "SP", sp);

    /* Add CPU status flags */
    cJSON_AddBoolToObject(response, "N", MOS6510_REGS_GET_SIGN(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "V", MOS6510_REGS_GET_OVERFLOW(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "B", MOS6510_REGS_GET_BREAK(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "D", MOS6510_REGS_GET_DECIMAL(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "I", MOS6510_REGS_GET_INTERRUPT(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "Z", MOS6510_REGS_GET_ZERO(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "C", MOS6510_REGS_GET_CARRY(&maincpu_regs) != 0);

    return response;
}

cJSON* mcp_tool_registers_set(cJSON *params)
{
    cJSON *response;
    cJSON *register_item, *value_item;
    const char *register_name;
    int value;

    log_message(mcp_tools_log, "Handling vice.registers.set");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    register_item = cJSON_GetObjectItem(params, "register");
    value_item = cJSON_GetObjectItem(params, "value");

    if (!cJSON_IsString(register_item) || !cJSON_IsNumber(value_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Invalid parameter types");
    }

    register_name = register_item->valuestring;
    value = value_item->valueint;

    /* Set register using VICE register access macros */
    if (strcmp(register_name, "PC") == 0) {
        if (value < 0 || value > 0xFFFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "PC value out of range (must be 0-65535)");
        }
        MOS6510_REGS_SET_PC(&maincpu_regs, (uint16_t)value);
    } else if (strcmp(register_name, "A") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "A value out of range (must be 0-255)");
        }
        MOS6510_REGS_SET_A(&maincpu_regs, (uint8_t)value);
    } else if (strcmp(register_name, "X") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "X value out of range (must be 0-255)");
        }
        MOS6510_REGS_SET_X(&maincpu_regs, (uint8_t)value);
    } else if (strcmp(register_name, "Y") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Y value out of range (must be 0-255)");
        }
        MOS6510_REGS_SET_Y(&maincpu_regs, (uint8_t)value);
    } else if (strcmp(register_name, "SP") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "SP value out of range (must be 0-255)");
        }
        MOS6510_REGS_SET_SP(&maincpu_regs, (uint8_t)value);
    } else if (strcmp(register_name, "V") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "V flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_OVERFLOW(&maincpu_regs, value);
    } else if (strcmp(register_name, "B") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "B flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_BREAK(&maincpu_regs, value);
    } else if (strcmp(register_name, "D") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "D flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_DECIMAL(&maincpu_regs, value);
    } else if (strcmp(register_name, "I") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "I flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_INTERRUPT(&maincpu_regs, value);
    } else if (strcmp(register_name, "C") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "C flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_CARRY(&maincpu_regs, value);
    } else if (strcmp(register_name, "N") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "N flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_SIGN(&maincpu_regs, value);
    } else if (strcmp(register_name, "Z") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Z flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_ZERO(&maincpu_regs, value);
    } else {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Unknown register name (must be PC, A, X, Y, SP, N, V, B, D, I, Z, or C)");
    }

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "register", register_name);
    cJSON_AddNumberToObject(response, "value", value);

    return response;
}

/* =========================================================================
 * Notification Functions (SSE Events)
 * ========================================================================= */

void mcp_notify_breakpoint(uint16_t pc, uint32_t bp_id)
{
    log_message(mcp_tools_log, "Notifying breakpoint hit: PC=$%04X, BP=%u", pc, bp_id);

    /* TODO: Build JSON event */
    /* TODO: Send via SSE */
    /* mcp_transport_sse_send_event("breakpoint", json_data); */
}

void mcp_notify_execution_state_changed(const char *state)
{
    log_message(mcp_tools_log, "Notifying execution state: %s", state);

    /* TODO: Build JSON event */
    /* TODO: Send via SSE */
    /* mcp_transport_sse_send_event("execution_state", json_data); */
}
