/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "src/monitor/mon_parse.y"

/* -*- C -*-
 *
 * mon_parse.y - Parser for the VICE built-in monitor.
 *
 * Written by
 *  Daniel Sladic <sladic@eecg.toronto.edu>
 *  Andreas Boose <viceteam@t-online.de>
 *  Thomas Giesel <skoe@directbox.com>
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

#include "vice.h"

#ifdef __GNUC__
#undef alloca
#define        alloca(n)       __builtin_alloca (n)
#else /* not __GNUC__ */
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#else  /* Not HAVE_ALLOCA_H  */
char *alloca();
#endif /* HAVE_ALLOCA_H.  */
#endif /* __GNUC__ */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "asm.h"
#include "console.h"
#include "drive.h"
#include "interrupt.h"
#include "lib.h"
#include "machine.h"
#include "mon_breakpoint.h"
#include "mon_command.h"
#include "mon_disassemble.h"
#include "mon_drive.h"
#include "mon_file.h"
#include "mon_input.h"
#include "mon_memmap.h"
#include "mon_memory.h"
#include "mon_register.h"
#include "mon_util.h"
#include "montypes.h"
#include "tapeport.h"
#include "resources.h"
#include "types.h"
#include "uimon.h"
#include "vsync.h"
#include "mon_profile.h"

#define join_ints(x,y) (LO16_TO_HI16(x)|y)
#define separate_int1(x) (HI16_TO_LO16(x))
#define separate_int2(x) (LO16(x))

static int yyerror(char *s);
static int temp;
static int resolve_datatype(unsigned guess_type, const char *num);
static int resolve_range(enum t_memspace memspace, MON_ADDR range[2],
                         const char *num);

/* Defined in the lexer */
extern int new_cmd, opt_asm;
extern int cur_len, last_len;

void free_buffer(void);
void make_buffer(char *str);
int yylex(void);

void set_yydebug(int val);

#define ERR_ILLEGAL_INPUT 1     /* Generic error as returned by yacc.  */
#define ERR_RANGE_BAD_START 2
#define ERR_RANGE_BAD_END 3
#define ERR_BAD_CMD 4
#define ERR_EXPECT_CHECKNUM 5
#define ERR_EXPECT_END_CMD 6
#define ERR_MISSING_CLOSE_PAREN 7
#define ERR_INCOMPLETE_COND_OP 8
#define ERR_EXPECT_FILENAME 9
#define ERR_ADDR_TOO_BIG 10
#define ERR_IMM_TOO_BIG 11
#define ERR_EXPECT_STRING 12
#define ERR_UNDEFINED_LABEL 13
#define ERR_EXPECT_DEVICE_NUM 14
#define ERR_EXPECT_ADDRESS 15
#define ERR_INVALID_REGISTER 16

#define BAD_ADDR (new_addr(e_invalid_space, 0))
#define CHECK_ADDR(x) ((x) == addr_mask(x))

/* set to YYDEBUG 1 to get parser debugging via "yydebug" command, requires to
   set_yydebug(1) in monitor.c:monitor_init */
#ifdef DEBUG
#define YYDEBUG 1
#else
#define YYDEBUG 0
#endif


#line 194 "src/monitor/mon_parse.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "mon_parse.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_H_NUMBER = 3,                   /* H_NUMBER  */
  YYSYMBOL_D_NUMBER = 4,                   /* D_NUMBER  */
  YYSYMBOL_O_NUMBER = 5,                   /* O_NUMBER  */
  YYSYMBOL_B_NUMBER = 6,                   /* B_NUMBER  */
  YYSYMBOL_CONVERT_OP = 7,                 /* CONVERT_OP  */
  YYSYMBOL_B_DATA = 8,                     /* B_DATA  */
  YYSYMBOL_H_RANGE_GUESS = 9,              /* H_RANGE_GUESS  */
  YYSYMBOL_D_NUMBER_GUESS = 10,            /* D_NUMBER_GUESS  */
  YYSYMBOL_O_NUMBER_GUESS = 11,            /* O_NUMBER_GUESS  */
  YYSYMBOL_B_NUMBER_GUESS = 12,            /* B_NUMBER_GUESS  */
  YYSYMBOL_BAD_CMD = 13,                   /* BAD_CMD  */
  YYSYMBOL_MEM_OP = 14,                    /* MEM_OP  */
  YYSYMBOL_IF = 15,                        /* IF  */
  YYSYMBOL_MEM_COMP = 16,                  /* MEM_COMP  */
  YYSYMBOL_MEM_DISK8 = 17,                 /* MEM_DISK8  */
  YYSYMBOL_MEM_DISK9 = 18,                 /* MEM_DISK9  */
  YYSYMBOL_MEM_DISK10 = 19,                /* MEM_DISK10  */
  YYSYMBOL_MEM_DISK11 = 20,                /* MEM_DISK11  */
  YYSYMBOL_EQUALS = 21,                    /* EQUALS  */
  YYSYMBOL_TRAIL = 22,                     /* TRAIL  */
  YYSYMBOL_CMD_SEP = 23,                   /* CMD_SEP  */
  YYSYMBOL_LABEL_ASGN_COMMENT = 24,        /* LABEL_ASGN_COMMENT  */
  YYSYMBOL_CMD_LOG = 25,                   /* CMD_LOG  */
  YYSYMBOL_CMD_LOGNAME = 26,               /* CMD_LOGNAME  */
  YYSYMBOL_CMD_SIDEFX = 27,                /* CMD_SIDEFX  */
  YYSYMBOL_CMD_DUMMY = 28,                 /* CMD_DUMMY  */
  YYSYMBOL_CMD_RETURN = 29,                /* CMD_RETURN  */
  YYSYMBOL_CMD_BLOCK_READ = 30,            /* CMD_BLOCK_READ  */
  YYSYMBOL_CMD_BLOCK_WRITE = 31,           /* CMD_BLOCK_WRITE  */
  YYSYMBOL_CMD_UP = 32,                    /* CMD_UP  */
  YYSYMBOL_CMD_DOWN = 33,                  /* CMD_DOWN  */
  YYSYMBOL_CMD_LOAD = 34,                  /* CMD_LOAD  */
  YYSYMBOL_CMD_BASICLOAD = 35,             /* CMD_BASICLOAD  */
  YYSYMBOL_CMD_SAVE = 36,                  /* CMD_SAVE  */
  YYSYMBOL_CMD_VERIFY = 37,                /* CMD_VERIFY  */
  YYSYMBOL_CMD_BVERIFY = 38,               /* CMD_BVERIFY  */
  YYSYMBOL_CMD_IGNORE = 39,                /* CMD_IGNORE  */
  YYSYMBOL_CMD_HUNT = 40,                  /* CMD_HUNT  */
  YYSYMBOL_CMD_FILL = 41,                  /* CMD_FILL  */
  YYSYMBOL_CMD_MOVE = 42,                  /* CMD_MOVE  */
  YYSYMBOL_CMD_GOTO = 43,                  /* CMD_GOTO  */
  YYSYMBOL_CMD_REGISTERS = 44,             /* CMD_REGISTERS  */
  YYSYMBOL_CMD_READSPACE = 45,             /* CMD_READSPACE  */
  YYSYMBOL_CMD_WRITESPACE = 46,            /* CMD_WRITESPACE  */
  YYSYMBOL_CMD_RADIX = 47,                 /* CMD_RADIX  */
  YYSYMBOL_CMD_MEM_DISPLAY = 48,           /* CMD_MEM_DISPLAY  */
  YYSYMBOL_CMD_BREAK = 49,                 /* CMD_BREAK  */
  YYSYMBOL_CMD_TRACE = 50,                 /* CMD_TRACE  */
  YYSYMBOL_CMD_IO = 51,                    /* CMD_IO  */
  YYSYMBOL_CMD_BRMON = 52,                 /* CMD_BRMON  */
  YYSYMBOL_CMD_COMPARE = 53,               /* CMD_COMPARE  */
  YYSYMBOL_CMD_DUMP = 54,                  /* CMD_DUMP  */
  YYSYMBOL_CMD_UNDUMP = 55,                /* CMD_UNDUMP  */
  YYSYMBOL_CMD_EXIT = 56,                  /* CMD_EXIT  */
  YYSYMBOL_CMD_DELETE = 57,                /* CMD_DELETE  */
  YYSYMBOL_CMD_CONDITION = 58,             /* CMD_CONDITION  */
  YYSYMBOL_CMD_COMMAND = 59,               /* CMD_COMMAND  */
  YYSYMBOL_CMD_ASSEMBLE = 60,              /* CMD_ASSEMBLE  */
  YYSYMBOL_CMD_DISASSEMBLE = 61,           /* CMD_DISASSEMBLE  */
  YYSYMBOL_CMD_NEXT = 62,                  /* CMD_NEXT  */
  YYSYMBOL_CMD_STEP = 63,                  /* CMD_STEP  */
  YYSYMBOL_CMD_PRINT = 64,                 /* CMD_PRINT  */
  YYSYMBOL_CMD_DEVICE = 65,                /* CMD_DEVICE  */
  YYSYMBOL_CMD_HELP = 66,                  /* CMD_HELP  */
  YYSYMBOL_CMD_WATCH = 67,                 /* CMD_WATCH  */
  YYSYMBOL_CMD_DISK = 68,                  /* CMD_DISK  */
  YYSYMBOL_CMD_QUIT = 69,                  /* CMD_QUIT  */
  YYSYMBOL_CMD_CHDIR = 70,                 /* CMD_CHDIR  */
  YYSYMBOL_CMD_BANK = 71,                  /* CMD_BANK  */
  YYSYMBOL_CMD_LOAD_LABELS = 72,           /* CMD_LOAD_LABELS  */
  YYSYMBOL_CMD_SAVE_LABELS = 73,           /* CMD_SAVE_LABELS  */
  YYSYMBOL_CMD_ADD_LABEL = 74,             /* CMD_ADD_LABEL  */
  YYSYMBOL_CMD_DEL_LABEL = 75,             /* CMD_DEL_LABEL  */
  YYSYMBOL_CMD_SHOW_LABELS = 76,           /* CMD_SHOW_LABELS  */
  YYSYMBOL_CMD_CLEAR_LABELS = 77,          /* CMD_CLEAR_LABELS  */
  YYSYMBOL_CMD_RECORD = 78,                /* CMD_RECORD  */
  YYSYMBOL_CMD_MON_STOP = 79,              /* CMD_MON_STOP  */
  YYSYMBOL_CMD_PLAYBACK = 80,              /* CMD_PLAYBACK  */
  YYSYMBOL_CMD_CHAR_DISPLAY = 81,          /* CMD_CHAR_DISPLAY  */
  YYSYMBOL_CMD_SPRITE_DISPLAY = 82,        /* CMD_SPRITE_DISPLAY  */
  YYSYMBOL_CMD_TEXT_DISPLAY = 83,          /* CMD_TEXT_DISPLAY  */
  YYSYMBOL_CMD_SCREENCODE_DISPLAY = 84,    /* CMD_SCREENCODE_DISPLAY  */
  YYSYMBOL_CMD_ENTER_DATA = 85,            /* CMD_ENTER_DATA  */
  YYSYMBOL_CMD_ENTER_BIN_DATA = 86,        /* CMD_ENTER_BIN_DATA  */
  YYSYMBOL_CMD_KEYBUF = 87,                /* CMD_KEYBUF  */
  YYSYMBOL_CMD_BLOAD = 88,                 /* CMD_BLOAD  */
  YYSYMBOL_CMD_BSAVE = 89,                 /* CMD_BSAVE  */
  YYSYMBOL_CMD_SCREEN = 90,                /* CMD_SCREEN  */
  YYSYMBOL_CMD_UNTIL = 91,                 /* CMD_UNTIL  */
  YYSYMBOL_CMD_CPU = 92,                   /* CMD_CPU  */
  YYSYMBOL_CMD_YYDEBUG = 93,               /* CMD_YYDEBUG  */
  YYSYMBOL_CMD_BACKTRACE = 94,             /* CMD_BACKTRACE  */
  YYSYMBOL_CMD_SCREENSHOT = 95,            /* CMD_SCREENSHOT  */
  YYSYMBOL_CMD_PWD = 96,                   /* CMD_PWD  */
  YYSYMBOL_CMD_DIR = 97,                   /* CMD_DIR  */
  YYSYMBOL_CMD_MKDIR = 98,                 /* CMD_MKDIR  */
  YYSYMBOL_CMD_RMDIR = 99,                 /* CMD_RMDIR  */
  YYSYMBOL_CMD_RESOURCE_GET = 100,         /* CMD_RESOURCE_GET  */
  YYSYMBOL_CMD_RESOURCE_SET = 101,         /* CMD_RESOURCE_SET  */
  YYSYMBOL_CMD_LOAD_RESOURCES = 102,       /* CMD_LOAD_RESOURCES  */
  YYSYMBOL_CMD_SAVE_RESOURCES = 103,       /* CMD_SAVE_RESOURCES  */
  YYSYMBOL_CMD_ATTACH = 104,               /* CMD_ATTACH  */
  YYSYMBOL_CMD_DETACH = 105,               /* CMD_DETACH  */
  YYSYMBOL_CMD_MON_RESET = 106,            /* CMD_MON_RESET  */
  YYSYMBOL_CMD_TAPECTRL = 107,             /* CMD_TAPECTRL  */
  YYSYMBOL_CMD_TAPEOFFS = 108,             /* CMD_TAPEOFFS  */
  YYSYMBOL_CMD_CARTFREEZE = 109,           /* CMD_CARTFREEZE  */
  YYSYMBOL_CMD_UPDB = 110,                 /* CMD_UPDB  */
  YYSYMBOL_CMD_JPDB = 111,                 /* CMD_JPDB  */
  YYSYMBOL_CMD_CPUHISTORY = 112,           /* CMD_CPUHISTORY  */
  YYSYMBOL_CMD_MEMMAPZAP = 113,            /* CMD_MEMMAPZAP  */
  YYSYMBOL_CMD_MEMMAPSHOW = 114,           /* CMD_MEMMAPSHOW  */
  YYSYMBOL_CMD_MEMMAPSAVE = 115,           /* CMD_MEMMAPSAVE  */
  YYSYMBOL_CMD_COMMENT = 116,              /* CMD_COMMENT  */
  YYSYMBOL_CMD_LIST = 117,                 /* CMD_LIST  */
  YYSYMBOL_CMD_STOPWATCH = 118,            /* CMD_STOPWATCH  */
  YYSYMBOL_RESET = 119,                    /* RESET  */
  YYSYMBOL_CMD_KEYMATRIX = 120,            /* CMD_KEYMATRIX  */
  YYSYMBOL_CMD_JOYSTICK = 121,             /* CMD_JOYSTICK  */
  YYSYMBOL_KEY_RESTORE = 122,              /* KEY_RESTORE  */
  YYSYMBOL_CMD_EXPORT = 123,               /* CMD_EXPORT  */
  YYSYMBOL_CMD_AUTOSTART = 124,            /* CMD_AUTOSTART  */
  YYSYMBOL_CMD_AUTOLOAD = 125,             /* CMD_AUTOLOAD  */
  YYSYMBOL_CMD_MAINCPU_TRACE = 126,        /* CMD_MAINCPU_TRACE  */
  YYSYMBOL_CMD_WARP = 127,                 /* CMD_WARP  */
  YYSYMBOL_CMD_PROFILE = 128,              /* CMD_PROFILE  */
  YYSYMBOL_FLAT = 129,                     /* FLAT  */
  YYSYMBOL_GRAPH = 130,                    /* GRAPH  */
  YYSYMBOL_FUNC = 131,                     /* FUNC  */
  YYSYMBOL_DEPTH = 132,                    /* DEPTH  */
  YYSYMBOL_DISASS = 133,                   /* DISASS  */
  YYSYMBOL_PROFILE_CONTEXT = 134,          /* PROFILE_CONTEXT  */
  YYSYMBOL_CLEAR = 135,                    /* CLEAR  */
  YYSYMBOL_CMD_LABEL_ASGN = 136,           /* CMD_LABEL_ASGN  */
  YYSYMBOL_L_PAREN = 137,                  /* L_PAREN  */
  YYSYMBOL_R_PAREN = 138,                  /* R_PAREN  */
  YYSYMBOL_ARG_IMMEDIATE = 139,            /* ARG_IMMEDIATE  */
  YYSYMBOL_REG_A = 140,                    /* REG_A  */
  YYSYMBOL_REG_X = 141,                    /* REG_X  */
  YYSYMBOL_REG_Y = 142,                    /* REG_Y  */
  YYSYMBOL_COMMA = 143,                    /* COMMA  */
  YYSYMBOL_INST_SEP = 144,                 /* INST_SEP  */
  YYSYMBOL_L_BRACKET = 145,                /* L_BRACKET  */
  YYSYMBOL_R_BRACKET = 146,                /* R_BRACKET  */
  YYSYMBOL_LESS_THAN = 147,                /* LESS_THAN  */
  YYSYMBOL_REG_U = 148,                    /* REG_U  */
  YYSYMBOL_REG_S = 149,                    /* REG_S  */
  YYSYMBOL_REG_PC = 150,                   /* REG_PC  */
  YYSYMBOL_REG_PCR = 151,                  /* REG_PCR  */
  YYSYMBOL_REG_B = 152,                    /* REG_B  */
  YYSYMBOL_REG_C = 153,                    /* REG_C  */
  YYSYMBOL_REG_D = 154,                    /* REG_D  */
  YYSYMBOL_REG_E = 155,                    /* REG_E  */
  YYSYMBOL_REG_H = 156,                    /* REG_H  */
  YYSYMBOL_REG_L = 157,                    /* REG_L  */
  YYSYMBOL_REG_AF = 158,                   /* REG_AF  */
  YYSYMBOL_REG_BC = 159,                   /* REG_BC  */
  YYSYMBOL_REG_DE = 160,                   /* REG_DE  */
  YYSYMBOL_REG_HL = 161,                   /* REG_HL  */
  YYSYMBOL_REG_IX = 162,                   /* REG_IX  */
  YYSYMBOL_REG_IY = 163,                   /* REG_IY  */
  YYSYMBOL_REG_SP = 164,                   /* REG_SP  */
  YYSYMBOL_REG_IXH = 165,                  /* REG_IXH  */
  YYSYMBOL_REG_IXL = 166,                  /* REG_IXL  */
  YYSYMBOL_REG_IYH = 167,                  /* REG_IYH  */
  YYSYMBOL_REG_IYL = 168,                  /* REG_IYL  */
  YYSYMBOL_PLUS = 169,                     /* PLUS  */
  YYSYMBOL_MINUS = 170,                    /* MINUS  */
  YYSYMBOL_STRING = 171,                   /* STRING  */
  YYSYMBOL_FILENAME = 172,                 /* FILENAME  */
  YYSYMBOL_R_O_L = 173,                    /* R_O_L  */
  YYSYMBOL_R_O_L_Q = 174,                  /* R_O_L_Q  */
  YYSYMBOL_OPCODE = 175,                   /* OPCODE  */
  YYSYMBOL_LABEL = 176,                    /* LABEL  */
  YYSYMBOL_BANKNAME = 177,                 /* BANKNAME  */
  YYSYMBOL_CPUTYPE = 178,                  /* CPUTYPE  */
  YYSYMBOL_MON_REGISTER = 179,             /* MON_REGISTER  */
  YYSYMBOL_COND_OP = 180,                  /* COND_OP  */
  YYSYMBOL_RADIX_TYPE = 181,               /* RADIX_TYPE  */
  YYSYMBOL_INPUT_SPEC = 182,               /* INPUT_SPEC  */
  YYSYMBOL_CMD_CHECKPT_ON = 183,           /* CMD_CHECKPT_ON  */
  YYSYMBOL_CMD_CHECKPT_OFF = 184,          /* CMD_CHECKPT_OFF  */
  YYSYMBOL_TOGGLE = 185,                   /* TOGGLE  */
  YYSYMBOL_MASK = 186,                     /* MASK  */
  YYSYMBOL_187_ = 187,                     /* '+'  */
  YYSYMBOL_188_ = 188,                     /* '-'  */
  YYSYMBOL_189_ = 189,                     /* '*'  */
  YYSYMBOL_190_ = 190,                     /* '/'  */
  YYSYMBOL_191_ = 191,                     /* '('  */
  YYSYMBOL_192_ = 192,                     /* ')'  */
  YYSYMBOL_193_ = 193,                     /* '@'  */
  YYSYMBOL_194_ = 194,                     /* ':'  */
  YYSYMBOL_YYACCEPT = 195,                 /* $accept  */
  YYSYMBOL_top_level = 196,                /* top_level  */
  YYSYMBOL_command_list = 197,             /* command_list  */
  YYSYMBOL_end_cmd = 198,                  /* end_cmd  */
  YYSYMBOL_command = 199,                  /* command  */
  YYSYMBOL_machine_state_rules = 200,      /* machine_state_rules  */
  YYSYMBOL_register_mod = 201,             /* register_mod  */
  YYSYMBOL_symbol_table_rules = 202,       /* symbol_table_rules  */
  YYSYMBOL_asm_rules = 203,                /* asm_rules  */
  YYSYMBOL_204_1 = 204,                    /* $@1  */
  YYSYMBOL_memory_rules = 205,             /* memory_rules  */
  YYSYMBOL_checkpoint_rules = 206,         /* checkpoint_rules  */
  YYSYMBOL_checkpoint_control_rules = 207, /* checkpoint_control_rules  */
  YYSYMBOL_monitor_state_rules = 208,      /* monitor_state_rules  */
  YYSYMBOL_monitor_misc_rules = 209,       /* monitor_misc_rules  */
  YYSYMBOL_disk_rules = 210,               /* disk_rules  */
  YYSYMBOL_cmd_file_rules = 211,           /* cmd_file_rules  */
  YYSYMBOL_data_entry_rules = 212,         /* data_entry_rules  */
  YYSYMBOL_monitor_debug_rules = 213,      /* monitor_debug_rules  */
  YYSYMBOL_rest_of_line = 214,             /* rest_of_line  */
  YYSYMBOL_opt_rest_of_line = 215,         /* opt_rest_of_line  */
  YYSYMBOL_rest_of_line_or_quoted = 216,   /* rest_of_line_or_quoted  */
  YYSYMBOL_filename = 217,                 /* filename  */
  YYSYMBOL_device_num = 218,               /* device_num  */
  YYSYMBOL_mem_op = 219,                   /* mem_op  */
  YYSYMBOL_opt_mem_op = 220,               /* opt_mem_op  */
  YYSYMBOL_register = 221,                 /* register  */
  YYSYMBOL_reg_list = 222,                 /* reg_list  */
  YYSYMBOL_reg_asgn = 223,                 /* reg_asgn  */
  YYSYMBOL_checkpt_num = 224,              /* checkpt_num  */
  YYSYMBOL_opt_context_num = 225,          /* opt_context_num  */
  YYSYMBOL_address_opt_range = 226,        /* address_opt_range  */
  YYSYMBOL_address_range = 227,            /* address_range  */
  YYSYMBOL_opt_address = 228,              /* opt_address  */
  YYSYMBOL_address = 229,                  /* address  */
  YYSYMBOL_opt_sep = 230,                  /* opt_sep  */
  YYSYMBOL_memspace = 231,                 /* memspace  */
  YYSYMBOL_memloc = 232,                   /* memloc  */
  YYSYMBOL_memaddr = 233,                  /* memaddr  */
  YYSYMBOL_expression = 234,               /* expression  */
  YYSYMBOL_opt_if_cond_expr = 235,         /* opt_if_cond_expr  */
  YYSYMBOL_cond_expr = 236,                /* cond_expr  */
  YYSYMBOL_cond_operand = 237,             /* cond_operand  */
  YYSYMBOL_data_list = 238,                /* data_list  */
  YYSYMBOL_data_element = 239,             /* data_element  */
  YYSYMBOL_hunt_list = 240,                /* hunt_list  */
  YYSYMBOL_hunt_element = 241,             /* hunt_element  */
  YYSYMBOL_value = 242,                    /* value  */
  YYSYMBOL_d_number = 243,                 /* d_number  */
  YYSYMBOL_opt_d_number = 244,             /* opt_d_number  */
  YYSYMBOL_guess_default = 245,            /* guess_default  */
  YYSYMBOL_number = 246,                   /* number  */
  YYSYMBOL_assembly_instr_list = 247,      /* assembly_instr_list  */
  YYSYMBOL_assembly_instruction = 248,     /* assembly_instruction  */
  YYSYMBOL_post_assemble = 249,            /* post_assemble  */
  YYSYMBOL_asm_operand_mode = 250,         /* asm_operand_mode  */
  YYSYMBOL_index_reg = 251,                /* index_reg  */
  YYSYMBOL_index_ureg = 252                /* index_ureg  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  361
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   2049

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  195
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  58
/* YYNRULES -- Number of rules.  */
#define YYNRULES  372
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  764

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   441


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     191,   192,   189,   187,     2,   188,     2,   190,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,   194,     2,
       2,     2,     2,     2,   193,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   191,   191,   192,   193,   196,   197,   200,   201,   202,
     205,   206,   207,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   220,   222,   224,   226,   228,   230,   232,
     234,   236,   238,   240,   242,   244,   246,   248,   250,   252,
     254,   256,   258,   260,   262,   264,   266,   268,   270,   272,
     274,   276,   278,   280,   282,   284,   286,   288,   290,   295,
     299,   302,   304,   306,   309,   314,   319,   321,   323,   325,
     327,   329,   331,   333,   335,   337,   341,   348,   347,   350,
     352,   354,   358,   360,   362,   364,   366,   368,   370,   372,
     374,   376,   378,   380,   382,   384,   386,   388,   390,   392,
     394,   396,   400,   409,   412,   416,   419,   428,   431,   440,
     445,   447,   449,   451,   453,   455,   457,   459,   461,   463,
     465,   469,   471,   476,   483,   491,   498,   510,   514,   516,
     534,   536,   538,   540,   542,   544,   546,   550,   552,   554,
     556,   558,   560,   562,   564,   566,   568,   570,   572,   574,
     576,   578,   580,   582,   584,   586,   588,   590,   592,   594,
     596,   598,   600,   602,   604,   606,   608,   610,   612,   614,
     616,   618,   620,   622,   624,   626,   628,   630,   632,   634,
     636,   638,   640,   642,   644,   646,   648,   650,   652,   656,
     658,   660,   662,   664,   666,   668,   670,   672,   674,   676,
     678,   680,   682,   684,   686,   688,   690,   692,   694,   696,
     698,   702,   704,   706,   710,   712,   716,   724,   727,   728,
     731,   734,   735,   738,   739,   742,   743,   746,   747,   750,
     756,   764,   765,   768,   772,   773,   776,   777,   780,   781,
     784,   785,   787,   791,   792,   795,   800,   805,   815,   816,
     819,   820,   821,   822,   823,   826,   828,   853,   854,   855,
     856,   857,   858,   859,   862,   863,   865,   870,   872,   874,
     876,   880,   886,   893,   906,   920,   921,   924,   925,   928,
     929,   932,   933,   934,   937,   938,   941,   942,   943,   944,
     947,   948,   951,   952,   953,   956,   957,   958,   959,   960,
     963,   964,   965,   968,   978,   979,   982,   989,  1000,  1011,
    1019,  1038,  1044,  1052,  1060,  1062,  1064,  1065,  1066,  1067,
    1068,  1069,  1070,  1072,  1074,  1076,  1078,  1079,  1080,  1081,
    1082,  1083,  1084,  1085,  1086,  1087,  1088,  1089,  1090,  1091,
    1092,  1093,  1094,  1095,  1096,  1098,  1099,  1114,  1118,  1122,
    1126,  1130,  1134,  1138,  1142,  1146,  1158,  1173,  1177,  1181,
    1185,  1189,  1193,  1197,  1201,  1205,  1217,  1222,  1230,  1231,
    1232,  1233,  1237
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "H_NUMBER", "D_NUMBER",
  "O_NUMBER", "B_NUMBER", "CONVERT_OP", "B_DATA", "H_RANGE_GUESS",
  "D_NUMBER_GUESS", "O_NUMBER_GUESS", "B_NUMBER_GUESS", "BAD_CMD",
  "MEM_OP", "IF", "MEM_COMP", "MEM_DISK8", "MEM_DISK9", "MEM_DISK10",
  "MEM_DISK11", "EQUALS", "TRAIL", "CMD_SEP", "LABEL_ASGN_COMMENT",
  "CMD_LOG", "CMD_LOGNAME", "CMD_SIDEFX", "CMD_DUMMY", "CMD_RETURN",
  "CMD_BLOCK_READ", "CMD_BLOCK_WRITE", "CMD_UP", "CMD_DOWN", "CMD_LOAD",
  "CMD_BASICLOAD", "CMD_SAVE", "CMD_VERIFY", "CMD_BVERIFY", "CMD_IGNORE",
  "CMD_HUNT", "CMD_FILL", "CMD_MOVE", "CMD_GOTO", "CMD_REGISTERS",
  "CMD_READSPACE", "CMD_WRITESPACE", "CMD_RADIX", "CMD_MEM_DISPLAY",
  "CMD_BREAK", "CMD_TRACE", "CMD_IO", "CMD_BRMON", "CMD_COMPARE",
  "CMD_DUMP", "CMD_UNDUMP", "CMD_EXIT", "CMD_DELETE", "CMD_CONDITION",
  "CMD_COMMAND", "CMD_ASSEMBLE", "CMD_DISASSEMBLE", "CMD_NEXT", "CMD_STEP",
  "CMD_PRINT", "CMD_DEVICE", "CMD_HELP", "CMD_WATCH", "CMD_DISK",
  "CMD_QUIT", "CMD_CHDIR", "CMD_BANK", "CMD_LOAD_LABELS",
  "CMD_SAVE_LABELS", "CMD_ADD_LABEL", "CMD_DEL_LABEL", "CMD_SHOW_LABELS",
  "CMD_CLEAR_LABELS", "CMD_RECORD", "CMD_MON_STOP", "CMD_PLAYBACK",
  "CMD_CHAR_DISPLAY", "CMD_SPRITE_DISPLAY", "CMD_TEXT_DISPLAY",
  "CMD_SCREENCODE_DISPLAY", "CMD_ENTER_DATA", "CMD_ENTER_BIN_DATA",
  "CMD_KEYBUF", "CMD_BLOAD", "CMD_BSAVE", "CMD_SCREEN", "CMD_UNTIL",
  "CMD_CPU", "CMD_YYDEBUG", "CMD_BACKTRACE", "CMD_SCREENSHOT", "CMD_PWD",
  "CMD_DIR", "CMD_MKDIR", "CMD_RMDIR", "CMD_RESOURCE_GET",
  "CMD_RESOURCE_SET", "CMD_LOAD_RESOURCES", "CMD_SAVE_RESOURCES",
  "CMD_ATTACH", "CMD_DETACH", "CMD_MON_RESET", "CMD_TAPECTRL",
  "CMD_TAPEOFFS", "CMD_CARTFREEZE", "CMD_UPDB", "CMD_JPDB",
  "CMD_CPUHISTORY", "CMD_MEMMAPZAP", "CMD_MEMMAPSHOW", "CMD_MEMMAPSAVE",
  "CMD_COMMENT", "CMD_LIST", "CMD_STOPWATCH", "RESET", "CMD_KEYMATRIX",
  "CMD_JOYSTICK", "KEY_RESTORE", "CMD_EXPORT", "CMD_AUTOSTART",
  "CMD_AUTOLOAD", "CMD_MAINCPU_TRACE", "CMD_WARP", "CMD_PROFILE", "FLAT",
  "GRAPH", "FUNC", "DEPTH", "DISASS", "PROFILE_CONTEXT", "CLEAR",
  "CMD_LABEL_ASGN", "L_PAREN", "R_PAREN", "ARG_IMMEDIATE", "REG_A",
  "REG_X", "REG_Y", "COMMA", "INST_SEP", "L_BRACKET", "R_BRACKET",
  "LESS_THAN", "REG_U", "REG_S", "REG_PC", "REG_PCR", "REG_B", "REG_C",
  "REG_D", "REG_E", "REG_H", "REG_L", "REG_AF", "REG_BC", "REG_DE",
  "REG_HL", "REG_IX", "REG_IY", "REG_SP", "REG_IXH", "REG_IXL", "REG_IYH",
  "REG_IYL", "PLUS", "MINUS", "STRING", "FILENAME", "R_O_L", "R_O_L_Q",
  "OPCODE", "LABEL", "BANKNAME", "CPUTYPE", "MON_REGISTER", "COND_OP",
  "RADIX_TYPE", "INPUT_SPEC", "CMD_CHECKPT_ON", "CMD_CHECKPT_OFF",
  "TOGGLE", "MASK", "'+'", "'-'", "'*'", "'/'", "'('", "')'", "'@'", "':'",
  "$accept", "top_level", "command_list", "end_cmd", "command",
  "machine_state_rules", "register_mod", "symbol_table_rules", "asm_rules",
  "$@1", "memory_rules", "checkpoint_rules", "checkpoint_control_rules",
  "monitor_state_rules", "monitor_misc_rules", "disk_rules",
  "cmd_file_rules", "data_entry_rules", "monitor_debug_rules",
  "rest_of_line", "opt_rest_of_line", "rest_of_line_or_quoted", "filename",
  "device_num", "mem_op", "opt_mem_op", "register", "reg_list", "reg_asgn",
  "checkpt_num", "opt_context_num", "address_opt_range", "address_range",
  "opt_address", "address", "opt_sep", "memspace", "memloc", "memaddr",
  "expression", "opt_if_cond_expr", "cond_expr", "cond_operand",
  "data_list", "data_element", "hunt_list", "hunt_element", "value",
  "d_number", "opt_d_number", "guess_default", "number",
  "assembly_instr_list", "assembly_instruction", "post_assemble",
  "asm_operand_mode", "index_reg", "index_ureg", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-404)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-250)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    1417,  1121,  -404,  -404,    22,     8,    96,   168,   427,  1121,
    1121,   155,   155,     8,     8,     8,     8,     8,   757,  1819,
    1819,  1819,  1546,   811,     2,  1193,  1372,  1372,  1546,  1819,
       8,     8,   427,   999,   757,   757,  1855,  1409,   155,   155,
    1121,  1155,   385,  1372,  -161,   427,  -140,   490,   482,   482,
    1855,   572,  1155,  1155,     8,   427,     8,  1409,  1409,  1409,
    1409,  1855,   427,  -161,     8,     8,  1546,  1409,   362,   427,
     427,     8,   427,  -133,  -140,  -140,   -97,   -78,     8,     8,
       8,  1121,   155,   -11,   155,   427,  1249,  1342,  1319,   427,
     155,     8,  -133,  1339,   411,  1894,  1571,   427,     8,     8,
     262,   309,   286,   118,  1881,   999,   999,   149,  1577,  -404,
    -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,
    -404,  -404,  -404,   128,  -404,  -404,  -404,  -404,  -404,  -404,
    -404,  -404,  -404,  -404,  -404,  -404,  -404,  1121,  -404,   -28,
     105,  -404,  -404,  -404,  -404,  -404,  -404,   427,  -404,  -404,
    -404,   427,   427,  -404,   427,  -404,  -404,   614,   614,  -404,
    -404,  1121,  -404,  1121,   920,   920,   953,   920,   920,  -404,
    -404,  -404,  -404,  -404,   155,  -404,  -404,  -404,   -11,   -11,
     -11,  -404,  -404,  -404,   -11,   -11,  -404,   427,   -11,  -404,
     143,   523,  -404,     7,   427,  -404,   -11,  -404,   427,  -404,
     417,  -404,  -404,   162,  1819,  -404,  1819,  -404,   427,   -11,
     427,   427,  -404,   584,  -404,   427,   142,    54,   130,  -404,
     427,  -404,  1121,  -404,  1121,   105,  -404,   427,  -404,  -404,
     427,  -404,  1819,   427,  -404,  -404,   427,   427,  -404,   318,
     427,   -11,   427,   -11,   -11,   427,   -11,  -404,   427,  -404,
     427,   427,  -404,   427,  -404,   427,  -404,   427,  -404,   427,
    -404,   427,   678,  -404,   427,   920,   920,  -404,   427,  -404,
     427,   427,  -404,  -404,  -404,   155,  -404,  -404,   427,   427,
     427,   427,    11,   427,   427,  1121,   105,  -404,  1121,  1121,
    -404,  1121,  -404,  -404,   427,  1249,  -404,  1903,  -404,  -404,
    1121,   -11,   427,   673,  -404,   427,  -404,   427,  -404,   132,
     427,  -404,  1342,   427,  -404,  1342,  -404,  1220,  1220,   427,
    -404,   427,  -404,   377,   377,  1855,  1855,   377,  1855,   427,
    -404,  1855,   868,  1342,    41,   234,   934,  1342,    75,  -404,
      77,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,
    -404,  -404,  -404,  -404,  -404,    79,  -404,  -404,   427,  -404,
     427,  -404,  -404,  -404,     6,  -404,  1121,  1121,  1121,  1121,
    -404,  -404,  -404,  -404,  -404,   237,   893,   105,   105,  -404,
     582,   582,  1714,  1734,   582,  1781,  -404,  1121,   333,  1855,
    1921,   678,  1855,  -404,  1342,  1342,   461,  -404,  -404,  -404,
    1819,  -404,  -404,   208,   208,  -404,  1855,  -404,  -404,  -404,
    1043,   427,    55,  -404,    53,  -404,   105,   105,  -404,  -404,
    -404,   208,  -404,  -404,  -404,  -404,    58,  -404,     8,  -404,
       8,    56,  -404,    61,  -404,  -404,  -404,  -404,  -404,  -404,
    -404,  -404,  -404,   841,  -404,  -404,  -404,  1801,  1761,  -404,
    -404,  -404,  -404,  1121,  -404,  -404,  -404,  -404,   427,  -404,
    -404,   105,  -404,   105,   105,   105,  -404,  -404,   427,  1262,
    1262,  1100,  1121,  -404,  -404,  -404,   427,  -404,   427,  -404,
     391,  -404,  1249,  -404,  1342,  -404,  1342,  -404,  -404,  -404,
     427,   445,  -404,   427,   427,   427,   427,  -404,   742,   102,
     108,   114,   116,   133,   141,  -100,  -404,   408,  -404,  -404,
    -404,  -404,   281,    74,  -404,   113,   467,   125,   127,    65,
    -404,   408,   408,  1156,  -404,  -404,  -404,  -404,   -77,   -77,
    -404,  -404,   427,  1855,   427,  -404,  -404,   427,   427,  -404,
     427,   427,  -404,   427,   105,  -404,  -404,   356,  -404,  -404,
    -404,  -404,  -404,   841,   427,  -404,  -404,   427,  1043,   427,
     427,   427,  1043,   109,  -404,    49,  -404,  -404,  -404,   427,
     138,   146,   427,  -404,  -404,   427,   427,   427,   427,   427,
     427,  -404,   678,  -404,   427,  -404,   427,   105,  -404,  -404,
    -404,  -404,  -404,  -404,  -404,  1048,  -404,  1048,  -404,   427,
     105,  -404,  -404,   427,  -404,   427,  -404,   427,   427,   427,
    -404,   377,  -404,  -404,  -404,  -404,  -404,   427,  -404,  -404,
    -404,  -404,  -404,  -404,  -404,   148,    21,  -404,   408,  -404,
     131,   408,   480,   -71,   408,   408,   766,   159,  -404,  -404,
    -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,
    -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,
     126,  -404,  -404,  -404,     9,   120,   104,  -404,  -404,    53,
      53,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,
    -404,  -404,  1262,  1262,  -404,  -404,  -404,  -404,  -404,  -404,
    -404,   427,  -404,   579,   178,   180,  -404,  -404,   174,   408,
     181,  -404,   -29,   187,   189,   196,   203,   182,  -404,  -404,
    1837,  -404,  -404,  -404,  -404,  -404,  1048,  -404,  1048,  -404,
    -404,  -404,  -404,  -404,  -404,  -404,   207,  -404,   209,  -404,
    -404,   210,  -404,  -404,  -404,  -404,  -404,  1043,  -404,  1262,
    1262,   216,  -404,  -404,  -110,  -404,  1048,  -404,  1048,  -404,
    -404,  1262,  1262,  -404,  1048,  -404,  1048,   427,  1262,  -404,
    -404,  1048,   427,  -404
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       0,     0,    22,     4,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   219,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   249,     0,     0,     0,     0,     0,     0,
       0,     0,   219,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   326,     0,     0,     0,     2,     5,
      10,    60,    11,    13,    12,    14,    15,    16,    17,    18,
      19,    20,    21,     0,   295,   297,   298,   296,   293,   294,
     292,   250,   251,   252,   253,   254,   229,     0,   285,     0,
       0,   263,   299,   284,     9,     8,     7,     0,   126,   222,
     221,     0,     0,   122,     0,   124,    45,     0,     0,   248,
      52,     0,    54,     0,     0,     0,     0,     0,     0,   235,
     286,   288,   289,   287,     0,   234,   241,   247,   249,   249,
     249,   245,   255,   256,   249,   249,    28,     0,   249,    61,
       0,     0,   232,     0,     0,   129,   249,    88,     0,   238,
     249,   226,   103,   227,     0,   109,     0,    29,     0,   249,
       0,     0,   134,     9,   117,     0,     0,     0,     0,    81,
       0,    50,     0,    48,     0,     0,   130,     0,   217,   139,
       0,   107,     0,     0,   133,   220,     0,     0,    23,     0,
       0,   249,     0,   249,   249,     0,   249,    72,     0,    74,
       0,     0,   212,     0,    90,     0,    92,     0,    94,     0,
      96,     0,     0,   215,     0,     0,     0,    56,     0,   105,
       0,     0,    31,   216,   144,     0,   146,   218,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   155,     0,     0,
     158,     0,   160,   162,     0,     0,    33,     0,    97,    98,
       0,   249,     0,     9,   203,     0,   223,     0,   179,     0,
       0,   165,     0,     0,   173,     0,   132,     0,     0,     0,
     135,     0,    58,   291,   237,     0,     0,     0,     0,     0,
     181,     0,     0,     0,   327,     0,     0,     0,   328,   329,
     330,   331,   332,   335,   338,   339,   340,   341,   342,   343,
     344,   333,   336,   334,   337,   307,   303,   111,     0,   113,
       0,     1,     6,     3,     0,   230,     0,     0,     0,     0,
     141,   125,   127,   121,   123,   249,     0,     0,     0,   224,
     249,   249,   194,     0,   249,     0,   114,     0,     0,     0,
       0,     0,     0,    27,     0,     0,     0,    63,    62,   128,
       0,    87,   225,   265,   265,    30,     0,    46,    47,   116,
       0,     0,     0,    79,     0,    80,     0,     0,   138,   131,
     140,   265,   137,   142,    25,    24,     0,    65,     0,    67,
       0,     0,    69,     0,    71,    73,   211,   213,    89,    91,
      93,    95,   278,     0,   276,   277,   143,     0,     0,    57,
     104,    32,   149,     0,   145,   147,   148,   151,     0,   153,
     154,     0,   206,     0,     0,     0,   161,   164,     0,     0,
       0,     0,     0,   177,   204,   178,     0,   170,     0,   166,
       0,   174,     0,   207,     0,   209,     0,   136,    59,   290,
       0,     0,   236,     0,     0,     0,     0,   180,     0,     0,
       0,     0,     0,     0,     0,     0,   306,     0,   368,   369,
     372,   371,     0,   351,   370,     0,     0,     0,     0,     0,
     345,     0,     0,     0,   110,   112,   262,   261,   257,   258,
     259,   260,     0,     0,     0,    53,    55,     0,     0,   195,
       0,     0,   200,     0,     0,   283,   282,     0,   280,   281,
     240,   242,   246,     0,     0,   233,   231,     0,     0,     0,
       0,     0,     0,     0,   271,     0,   270,   272,   120,     0,
     305,   304,     0,    51,    49,     0,     0,     0,     0,     0,
       0,   214,     0,   192,     0,   197,     0,     0,   152,   205,
     156,   157,   159,   163,    34,     0,    39,     0,    99,     0,
       0,   172,   171,     0,   167,     0,   175,     0,     0,     0,
     182,     0,   183,   185,   186,   188,   187,     0,    75,   316,
     317,   318,   319,   320,   321,   312,     0,   353,     0,   349,
     347,     0,     0,     0,     0,     0,     0,   366,   352,   354,
     308,   309,   310,   355,   311,   346,   201,   243,   202,   189,
     190,   193,   198,   199,   115,    85,   279,    84,    82,    86,
     264,   102,   108,    83,     0,     0,     0,   118,   119,     0,
     302,    78,   106,    26,    64,    66,    68,    70,   275,   191,
     196,   150,     0,     0,   100,   101,   169,   168,   176,   208,
     210,     0,    76,     0,     0,     0,   350,   348,     0,     0,
       0,   361,     0,     0,     0,     0,     0,     0,   269,   268,
       0,   267,   266,   300,   301,    35,     0,    40,     0,   184,
     322,   315,   323,   324,   325,   313,     0,   363,     0,   359,
     357,     0,   362,   364,   365,   356,   367,     0,   274,     0,
       0,     0,   360,   358,     0,    36,     0,    41,     0,   314,
     273,     0,     0,    37,     0,    42,     0,     0,     0,    38,
      43,     0,     0,    44
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -404,  -404,  -404,    -6,   256,  -404,  -404,  -404,  -404,  -404,
    -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,  -404,    29,
     282,    51,   871,   571,  -404,   136,   -19,  -404,   -23,   -20,
    -404,    44,    48,  -181,   759,   575,   650,  -177,  -404,   360,
    -356,  -301,  -404,    -1,  -184,  -404,  -148,  -404,    24,  -404,
    -404,   704,  -404,  -403,  -404,  -404,  -306,  -117
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,   107,   108,   148,   109,   110,   111,   112,   113,   414,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   230,
     278,   236,   151,   305,   203,   204,   138,   191,   192,   174,
     491,   198,   199,   532,   200,   533,   139,   181,   182,   140,
     559,   565,   566,   443,   444,   547,   548,   141,   306,   490,
     142,   183,   570,   123,   572,   356,   513,   514
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     153,   155,   156,   144,   190,   160,   162,   526,   144,   149,
     708,   571,   228,   215,   216,   217,   186,   189,   195,   197,
     202,   205,   207,   144,   145,   146,   212,   214,   750,   145,
     146,   219,   221,   223,   235,   226,   229,   231,   625,   234,
     277,   238,   175,   626,   145,   146,   247,   249,   560,   252,
     144,   254,   256,   258,   260,   411,   263,   175,   175,   175,
     267,   269,   272,   273,   274,   575,   276,   178,   184,   185,
     666,   145,   146,   233,   281,   701,   287,   209,   290,   292,
     293,   220,   296,   298,   299,   358,   360,   304,   308,   311,
     314,   316,   264,   282,   320,   322,   330,   144,   702,   357,
     359,   255,   257,   259,   261,   711,   144,   124,   125,   126,
     127,   270,   368,   369,   128,   129,   130,   730,   145,   146,
     131,   132,   133,   134,   135,   279,   280,   145,   146,   175,
     175,   144,   159,   144,   370,   124,   125,   126,   127,   331,
     731,   371,   128,   129,   130,   372,   373,   709,   374,   361,
     363,   365,   145,   146,   145,   146,   144,   410,  -249,  -249,
    -249,  -249,   694,   206,   395,  -249,  -249,  -249,   386,   144,
     695,  -249,  -249,  -249,  -249,  -249,   402,   145,   146,   232,
     150,   393,   458,   194,   507,   397,   365,   398,   399,   666,
     145,   146,   401,   366,   367,   368,   369,   159,   527,   537,
     538,   627,   405,   541,   407,   408,   629,   147,   636,   409,
     633,   637,   413,   552,   415,   638,   639,   552,   521,   418,
     522,   419,   523,   558,   420,  -249,   569,   422,   104,   666,
     423,   424,   579,   425,   427,   576,   429,   580,  -244,   432,
     619,   562,   434,   630,   435,   436,   620,   437,   403,   438,
     404,   439,   621,   440,   622,   441,   631,   660,   446,  -244,
    -244,   664,   449,   144,   450,   451,   713,   714,   634,   452,
     635,   623,   454,   455,   456,   457,   421,   459,   460,   624,
     462,   152,   669,   136,   145,   146,   665,   144,   466,   467,
     670,   693,   366,   367,   368,   369,   473,   563,   159,   474,
     697,   475,   707,   477,   479,   -77,   666,   481,   145,   146,
     144,   483,   485,   487,   710,   488,   725,   476,   726,   144,
     727,   470,   696,   497,   736,   698,   700,   729,   703,   704,
     706,   145,   146,   732,  -249,   733,   124,   125,   126,   127,
     145,   146,   734,   128,   129,   130,  -249,   489,   492,   735,
     741,   495,   524,   154,   525,   742,   743,   144,   749,   124,
     125,   126,   127,   144,   362,   712,   128,   129,   130,   157,
     158,   535,   536,   556,   302,   508,   509,   190,   145,   146,
     159,   170,   510,   511,   145,   146,   144,   171,   172,   173,
     553,   564,   144,   728,   124,   125,   126,   127,   678,   656,
     225,   128,   129,   130,   512,   568,   645,   145,   146,     0,
     573,   574,   144,   145,   146,   323,   324,   325,  -239,   326,
     327,   328,   508,   509,   366,   367,   368,   369,   144,   510,
     511,   540,  -239,   145,   146,     0,   744,   581,     0,  -239,
    -239,   286,     0,     0,   557,     0,   144,   319,     0,   145,
     146,   628,   588,     0,     0,   589,     0,   590,   591,   592,
       0,   159,   593,   594,   596,   598,     0,   145,   146,     0,
     601,   329,   602,     0,   604,     0,   606,   131,   132,   133,
     134,   135,     0,   149,   610,   612,     0,   613,   614,   615,
     616,   144,   618,     0,   321,  -249,   586,   364,   131,   132,
     133,   134,   135,     0,   545,     0,   131,   132,   133,   134,
     135,     0,   145,   146,     0,   599,     0,   375,   376,   546,
       0,   377,     0,   378,   144,     0,   646,   545,   648,     0,
     307,   649,   650,     0,   651,   652,     0,   653,   654,   564,
     271,   655,   546,   564,     0,   145,   146,   657,   658,   508,
     509,   659,     0,   661,   662,   663,   510,   511,   228,   667,
     159,     0,     0,   668,     0,     0,   671,     0,     0,   672,
     673,   674,   675,   676,   677,     0,   603,   611,   679,     0,
     680,   681,   416,  -244,   417,  -235,   161,   163,   131,   132,
     133,   134,   135,   684,   685,     0,     0,   686,     0,   687,
       0,   688,   689,   690,  -244,  -244,  -235,  -235,   508,   509,
       0,   692,     0,   222,   224,   510,   511,   124,   125,   126,
     127,   508,   509,     0,   128,   129,   130,     0,   510,   511,
     131,   132,   133,   134,   135,   691,     0,   632,     0,     0,
     136,     0,     0,     0,     0,   461,     0,   564,   463,   464,
     699,   465,     0,     0,   150,     0,     0,   288,   289,   291,
     471,     0,     0,   297,     0,   300,   396,   237,     0,   180,
     180,   180,   188,   193,  -224,   180,   715,   717,   188,   180,
       0,   124,   125,   126,   127,   719,   188,   180,   128,   129,
     130,   227,     0,     0,     0,  -224,  -224,   239,   241,   243,
     188,   246,   248,   250,     0,   143,     0,   180,   180,   180,
     180,   188,     0,   143,   143,     0,   188,   180,   564,   720,
       0,   721,     0,     0,     0,   159,   528,   529,   530,   531,
       0,     0,     0,   745,   747,   380,   381,   383,   384,   385,
     722,   723,   724,   144,   143,   753,   755,   544,   245,   387,
       0,   759,   760,   388,   389,   390,   763,     0,   169,   391,
     392,   170,     0,   394,   145,   146,   617,   171,   172,   173,
       0,   400,     0,     0,     0,   389,     0,     0,   179,   179,
     179,   187,     0,     0,   406,   143,     0,   208,   179,     0,
     294,   295,   412,   136,     0,   218,     0,     0,     0,   312,
     315,   366,   367,   368,   369,   137,     0,     0,   355,   244,
       0,     0,   144,   587,   426,     0,   428,     0,   430,   431,
     262,   433,     0,     0,     0,   268,     0,   131,   132,   133,
     134,   135,   600,   145,   146,     0,   447,   448,     0,     0,
       0,   143,   144,     0,  -249,  -249,  -249,  -249,     0,   442,
     453,  -249,  -249,  -249,   180,     0,   180,     0,     0,     0,
       0,   143,   143,   145,   146,   143,     0,   143,     0,     0,
       0,   124,   125,   126,   127,     0,   472,     0,   128,   129,
     130,     0,   180,     0,   164,   165,   166,   167,   168,     0,
       0,     0,   484,   486,     0,     0,   124,   125,   126,   127,
       0,   210,   211,   128,   129,   130,     0,   508,   509,   131,
     132,   133,   134,   135,   510,   511,   705,     0,     0,   240,
     242,   379,     0,     0,   170,   251,   143,   253,   143,     0,
     171,   172,   173,     0,     0,   265,   266,   124,   125,   126,
     127,     0,   275,     0,   128,   129,   130,   469,     0,   283,
     284,   285,     0,     0,   382,     0,     0,   170,     0,     0,
       0,     0,   301,   171,   172,   173,   445,     0,     0,   317,
     318,     0,     0,     0,     0,   188,   188,     0,   188,     0,
       0,   188,     0,     0,   159,     0,     0,     0,     0,   143,
     136,     0,   143,   143,     0,   143,     0,     0,     0,   468,
     213,     0,     0,   170,   143,     0,     0,     0,     0,   171,
     172,   173,  -249,   478,     0,     0,   480,     0,   582,   482,
       0,   145,   146,     0,     0,     0,   188,   499,   500,   501,
     502,   503,   504,   180,     0,   188,   505,   506,     0,   188,
     519,   520,   188,     0,   595,   597,   124,   125,   126,   127,
     180,     0,     0,   128,   129,   130,   188,     0,     0,   131,
     132,   133,   134,   135,   131,   132,   133,   134,   135,   177,
     143,   143,   143,   143,   515,     0,     0,   516,     0,     0,
     366,   367,   368,   369,   493,   494,   517,   496,   518,     0,
     498,   143,   549,     0,     0,   445,     0,   188,   180,   555,
       0,   144,     0,   124,   125,   126,   127,     0,     0,   176,
     128,   129,   130,     0,   567,     0,   131,   132,   133,   134,
     135,   180,   145,   146,   124,   125,   126,   127,   582,     0,
       0,   128,   129,   130,     0,   534,     0,   131,   132,   133,
     134,   135,   179,     0,   543,     0,     0,     0,   550,     0,
       0,   554,     0,     0,     0,     0,   144,   143,     0,   124,
     125,   126,   127,     0,     0,   561,   128,   129,   130,     0,
       0,   131,   132,   133,   134,   135,   143,   145,   146,     0,
     562,     0,     0,   188,   605,     0,   607,     0,   608,     0,
     609,     0,     0,     0,   144,     0,   124,   125,   126,   127,
       0,     0,   176,   128,   129,   130,   584,   179,     0,   131,
     132,   133,   134,   135,     0,   145,   146,     0,     0,     0,
       0,   144,   136,  -249,  -249,  -249,  -249,   644,     0,     0,
    -249,  -249,  -249,     0,     0,     0,   563,     0,     0,     0,
       0,     0,   145,   146,     0,   682,     0,   683,     0,     0,
     144,   549,   124,   125,   126,   127,     0,   716,   718,   128,
     129,   130,   567,   144,     0,     0,   567,     0,     0,     0,
       0,   145,   146,     0,     0,     0,   177,     0,  -249,  -249,
    -249,  -249,  -249,     0,   145,   146,   445,   366,   367,   368,
     369,     0,   647,     0,     0,     0,     0,   640,   641,   577,
     136,   578,     0,     0,   510,   642,   643,     0,     0,     0,
       0,     0,   137,     0,   746,   748,     0,     0,     0,     0,
     144,     0,     0,  -249,     0,     0,   754,   756,     0,  -249,
    -249,  -249,     0,   761,     0,  -249,  -249,  -249,  -249,  -249,
     303,   145,   146,   170,     0,   124,   125,   126,   127,   171,
     172,   173,   128,   129,   130,     0,     0,     0,     0,     0,
     188,   145,   146,   159,     0,     0,   739,     0,   740,   177,
     567,     0,     0,   144,   196,  -228,  -228,  -228,  -228,     0,
       0,  -228,  -228,  -228,  -228,     0,   201,     0,  -228,  -228,
    -228,  -228,  -228,     0,   145,   146,   751,     0,   752,     0,
       0,     0,     0,     0,   757,   159,   758,     0,     0,     0,
     144,   762,   124,   125,   126,   127,     0,     0,   176,   128,
     129,   130,     0,     0,     1,   131,   132,   133,   134,   135,
       2,   145,   146,     0,     0,     0,     0,     0,     0,     3,
       0,   567,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,   159,     0,    24,    25,    26,    27,    28,   738,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,     0,    95,    96,     0,
      97,    98,    99,   100,   101,   102,     0,   144,  -228,   124,
     125,   126,   127,   103,     0,     0,   128,   129,   130,     0,
       0,     0,   131,   132,   133,   134,   135,     0,   145,   146,
       0,     0,   144,     0,   124,   125,   126,   127,     0,     0,
       0,   128,   129,   130,     1,   177,     0,     0,     0,     0,
       2,     0,   104,   145,   146,     0,     0,     0,     0,     0,
     105,   106,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,     0,     0,    24,    25,    26,    27,    28,     0,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,     0,    95,    96,     0,
      97,    98,    99,   100,   101,   102,   313,     0,     0,     0,
       0,     0,     0,   103,     0,  -224,     0,  -224,  -224,  -224,
    -224,     0,   177,  -224,  -224,  -224,  -224,     0,     0,     0,
    -224,  -224,  -224,  -224,  -224,   539,     0,   124,   125,   126,
     127,     0,     0,   176,   128,   129,   130,     0,     0,     0,
     131,   132,   133,   134,   135,     0,     0,     0,     0,     0,
     105,   106,   585,     0,   124,   125,   126,   127,     0,     0,
     176,   128,   129,   130,     0,     0,     0,   131,   132,   133,
     134,   135,   542,     0,   124,   125,   126,   127,     0,     0,
       0,   128,   129,   130,     0,     0,     0,   131,   132,   133,
     134,   135,   583,     0,   124,   125,   126,   127,     0,     0,
       0,   128,   129,   130,     0,     0,     0,   131,   132,   133,
     134,   135,   124,   125,   126,   127,     0,     0,   176,   128,
     129,   130,     0,     0,     0,   131,   132,   133,   134,   135,
     124,   125,   126,   127,     0,     0,     0,   128,   129,   130,
       0,     0,     0,   131,   132,   133,   134,   135,   124,   125,
     126,   127,     0,     0,     0,   128,   129,   130,     0,     0,
       0,   131,   132,   133,   134,   135,     0,     0,     0,     0,
       0,     0,     0,     0,   124,   125,   126,   127,     0,     0,
    -224,   128,   129,   130,     0,   144,     0,   124,   125,   126,
     127,     0,     0,     0,   128,   129,   130,   170,     0,     0,
     177,     0,     0,   171,   172,   173,   145,   146,     0,   131,
     132,   133,   134,   135,   124,   125,   126,   127,     0,     0,
     551,   128,   129,   130,     0,     0,     0,   177,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   177,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   737,     0,     0,   177,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   177,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   177,     0,     0,   309,     0,   332,     0,
     333,   334,     0,     0,   335,     0,   336,     0,   337,   310,
       0,   177,     0,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354
};

static const yytype_int16 yycheck[] =
{
       6,     7,     8,     1,    23,    11,    12,     1,     1,     1,
       1,   414,   173,    33,    34,    35,    22,    23,    24,    25,
      26,    27,    28,     1,    22,    23,    32,    33,   138,    22,
      23,    37,    38,    39,   174,    41,    42,    43,   138,    45,
     173,    47,    18,   143,    22,    23,    52,    53,   404,    55,
       1,    57,    58,    59,    60,     1,    62,    33,    34,    35,
      66,    67,    68,    69,    70,   421,    72,    19,    20,    21,
     180,    22,    23,    44,   171,   146,    82,    29,    84,    85,
      86,    37,    88,    89,    90,   105,   106,    93,    94,    95,
      96,    97,    63,   171,   100,   101,   102,     1,   169,   105,
     106,    57,    58,    59,    60,     1,     1,     3,     4,     5,
       6,    67,   189,   190,    10,    11,    12,   146,    22,    23,
      16,    17,    18,    19,    20,    74,    75,    22,    23,   105,
     106,     1,   143,     1,   140,     3,     4,     5,     6,    21,
     169,   147,    10,    11,    12,   151,   152,   138,   154,     0,
      22,   179,    22,    23,    22,    23,     1,    15,     3,     4,
       5,     6,   141,    27,    21,    10,    11,    12,   174,     1,
     149,    16,    17,    18,    19,    20,    14,    22,    23,    43,
     172,   187,   171,   181,   143,   191,   179,   193,   194,   180,
      22,    23,   198,   187,   188,   189,   190,   143,   192,   380,
     381,   507,   208,   384,   210,   211,   512,   185,   143,   215,
     516,   146,   218,   390,   220,   521,   522,   394,   143,   225,
     143,   227,   143,    15,   230,   171,   171,   233,   175,   180,
     236,   237,   176,   239,   240,   177,   242,   176,     1,   245,
     138,   137,   248,   169,   250,   251,   138,   253,   204,   255,
     206,   257,   138,   259,   138,   261,   143,   558,   264,    22,
      23,   562,   268,     1,   270,   271,   669,   670,   143,   275,
     143,   138,   278,   279,   280,   281,   232,   283,   284,   138,
     286,   185,   144,   179,    22,    23,   177,     1,   294,   295,
     144,   143,   187,   188,   189,   190,   302,   193,   143,   305,
     169,   307,   143,   309,   310,   175,   180,   313,    22,    23,
       1,   317,   318,   319,   194,   321,   138,   185,   138,     1,
     146,   297,   628,   329,   142,   631,   632,   146,   634,   635,
     636,    22,    23,   146,   179,   146,     3,     4,     5,     6,
      22,    23,   146,    10,    11,    12,   191,   323,   324,   146,
     143,   327,   358,   185,   360,   146,   146,     1,   142,     3,
       4,     5,     6,     1,   108,   666,    10,    11,    12,     9,
      10,   377,   378,   396,    92,   141,   142,   396,    22,    23,
     143,     4,   148,   149,    22,    23,     1,    10,    11,    12,
     391,   410,     1,   699,     3,     4,     5,     6,   582,   547,
      40,    10,    11,    12,   170,   411,   523,    22,    23,    -1,
     416,   417,     1,    22,    23,   129,   130,   131,     1,   133,
     134,   135,   141,   142,   187,   188,   189,   190,     1,   148,
     149,   383,    15,    22,    23,    -1,   737,   443,    -1,    22,
      23,    81,    -1,    -1,   400,    -1,     1,   185,    -1,    22,
      23,   170,   458,    -1,    -1,   461,    -1,   463,   464,   465,
      -1,   143,   468,   469,   470,   471,    -1,    22,    23,    -1,
     476,   185,   478,    -1,   480,    -1,   482,    16,    17,    18,
      19,    20,    -1,     1,   490,   491,    -1,   493,   494,   495,
     496,     1,   498,    -1,   185,   177,   448,   137,    16,    17,
      18,    19,    20,    -1,   171,    -1,    16,    17,    18,    19,
      20,    -1,    22,    23,    -1,   471,    -1,   157,   158,   186,
      -1,   161,    -1,   163,     1,    -1,   532,   171,   534,    -1,
     119,   537,   538,    -1,   540,   541,    -1,   543,   544,   558,
     178,   547,   186,   562,    -1,    22,    23,   553,   554,   141,
     142,   557,    -1,   559,   560,   561,   148,   149,   173,   565,
     143,    -1,    -1,   569,    -1,    -1,   572,    -1,    -1,   575,
     576,   577,   578,   579,   580,    -1,   185,   132,   584,    -1,
     586,   587,   222,     1,   224,     1,    11,    12,    16,    17,
      18,    19,    20,   599,   600,    -1,    -1,   603,    -1,   605,
      -1,   607,   608,   609,    22,    23,    22,    23,   141,   142,
      -1,   617,    -1,    38,    39,   148,   149,     3,     4,     5,
       6,   141,   142,    -1,    10,    11,    12,    -1,   148,   149,
      16,    17,    18,    19,    20,   611,    -1,   170,    -1,    -1,
     179,    -1,    -1,    -1,    -1,   285,    -1,   666,   288,   289,
     170,   291,    -1,    -1,   172,    -1,    -1,    82,    83,    84,
     300,    -1,    -1,    88,    -1,    90,   143,   177,    -1,    19,
      20,    21,    22,    23,     1,    25,   682,   683,    28,    29,
      -1,     3,     4,     5,     6,   691,    36,    37,    10,    11,
      12,    41,    -1,    -1,    -1,    22,    23,    47,    48,    49,
      50,    51,    52,    53,    -1,     1,    -1,    57,    58,    59,
      60,    61,    -1,     9,    10,    -1,    66,    67,   737,   140,
      -1,   142,    -1,    -1,    -1,   143,   366,   367,   368,   369,
      -1,    -1,    -1,   739,   740,   164,   165,   166,   167,   168,
     161,   162,   163,     1,    40,   751,   752,   387,   176,   174,
      -1,   757,   758,   178,   179,   180,   762,    -1,     1,   184,
     185,     4,    -1,   188,    22,    23,    24,    10,    11,    12,
      -1,   196,    -1,    -1,    -1,   200,    -1,    -1,    19,    20,
      21,    22,    -1,    -1,   209,    81,    -1,    28,    29,    -1,
      86,    87,   217,   179,    -1,    36,    -1,    -1,    -1,    95,
      96,   187,   188,   189,   190,   191,    -1,    -1,   104,    50,
      -1,    -1,     1,   453,   239,    -1,   241,    -1,   243,   244,
      61,   246,    -1,    -1,    -1,    66,    -1,    16,    17,    18,
      19,    20,   472,    22,    23,    -1,   265,   266,    -1,    -1,
      -1,   137,     1,    -1,     3,     4,     5,     6,    -1,   171,
     275,    10,    11,    12,   204,    -1,   206,    -1,    -1,    -1,
      -1,   157,   158,    22,    23,   161,    -1,   163,    -1,    -1,
      -1,     3,     4,     5,     6,    -1,   301,    -1,    10,    11,
      12,    -1,   232,    -1,    13,    14,    15,    16,    17,    -1,
      -1,    -1,   317,   318,    -1,    -1,     3,     4,     5,     6,
      -1,    30,    31,    10,    11,    12,    -1,   141,   142,    16,
      17,    18,    19,    20,   148,   149,   150,    -1,    -1,    48,
      49,     1,    -1,    -1,     4,    54,   222,    56,   224,    -1,
      10,    11,    12,    -1,    -1,    64,    65,     3,     4,     5,
       6,    -1,    71,    -1,    10,    11,    12,   297,    -1,    78,
      79,    80,    -1,    -1,     1,    -1,    -1,     4,    -1,    -1,
      -1,    -1,    91,    10,    11,    12,   262,    -1,    -1,    98,
      99,    -1,    -1,    -1,    -1,   325,   326,    -1,   328,    -1,
      -1,   331,    -1,    -1,   143,    -1,    -1,    -1,    -1,   285,
     179,    -1,   288,   289,    -1,   291,    -1,    -1,    -1,   295,
       1,    -1,    -1,     4,   300,    -1,    -1,    -1,    -1,    10,
      11,    12,   171,   309,    -1,    -1,   312,    -1,   443,   315,
      -1,    22,    23,    -1,    -1,    -1,   376,   159,   160,   161,
     162,   163,   164,   383,    -1,   385,   332,   333,    -1,   389,
     336,   337,   392,    -1,   469,   470,     3,     4,     5,     6,
     400,    -1,    -1,    10,    11,    12,   406,    -1,    -1,    16,
      17,    18,    19,    20,    16,    17,    18,    19,    20,   176,
     366,   367,   368,   369,   140,    -1,    -1,   143,    -1,    -1,
     187,   188,   189,   190,   325,   326,   152,   328,   154,    -1,
     331,   387,   388,    -1,    -1,   391,    -1,   447,   448,   395,
      -1,     1,    -1,     3,     4,     5,     6,    -1,    -1,     9,
      10,    11,    12,    -1,   410,    -1,    16,    17,    18,    19,
      20,   471,    22,    23,     3,     4,     5,     6,   553,    -1,
      -1,    10,    11,    12,    -1,   376,    -1,    16,    17,    18,
      19,    20,   383,    -1,   385,    -1,    -1,    -1,   389,    -1,
      -1,   392,    -1,    -1,    -1,    -1,     1,   453,    -1,     3,
       4,     5,     6,    -1,    -1,   406,    10,    11,    12,    -1,
      -1,    16,    17,    18,    19,    20,   472,    22,    23,    -1,
     137,    -1,    -1,   533,   480,    -1,   482,    -1,   484,    -1,
     486,    -1,    -1,    -1,     1,    -1,     3,     4,     5,     6,
      -1,    -1,     9,    10,    11,    12,   447,   448,    -1,    16,
      17,    18,    19,    20,    -1,    22,    23,    -1,    -1,    -1,
      -1,     1,   179,     3,     4,     5,     6,   523,    -1,    -1,
      10,    11,    12,    -1,    -1,    -1,   193,    -1,    -1,    -1,
      -1,    -1,    22,    23,    -1,   595,    -1,   597,    -1,    -1,
       1,   547,     3,     4,     5,     6,    -1,   682,   683,    10,
      11,    12,   558,     1,    -1,    -1,   562,    -1,    -1,    -1,
      -1,    22,    23,    -1,    -1,    -1,   176,    -1,    16,    17,
      18,    19,    20,    -1,    22,    23,   582,   187,   188,   189,
     190,    -1,   533,    -1,    -1,    -1,    -1,   141,   142,   428,
     179,   430,    -1,    -1,   148,   149,   150,    -1,    -1,    -1,
      -1,    -1,   191,    -1,   739,   740,    -1,    -1,    -1,    -1,
       1,    -1,    -1,     4,    -1,    -1,   751,   752,    -1,    10,
      11,    12,    -1,   758,    -1,    16,    17,    18,    19,    20,
       1,    22,    23,     4,    -1,     3,     4,     5,     6,    10,
      11,    12,    10,    11,    12,    -1,    -1,    -1,    -1,    -1,
     710,    22,    23,   143,    -1,    -1,   716,    -1,   718,   176,
     666,    -1,    -1,     1,   181,     3,     4,     5,     6,    -1,
      -1,     9,    10,    11,    12,    -1,    14,    -1,    16,    17,
      18,    19,    20,    -1,    22,    23,   746,    -1,   748,    -1,
      -1,    -1,    -1,    -1,   754,   143,   756,    -1,    -1,    -1,
       1,   761,     3,     4,     5,     6,    -1,    -1,     9,    10,
      11,    12,    -1,    -1,     7,    16,    17,    18,    19,    20,
      13,    22,    23,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      -1,   737,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,   143,    -1,    47,    48,    49,    50,    51,   710,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,    -1,   120,   121,    -1,
     123,   124,   125,   126,   127,   128,    -1,     1,   176,     3,
       4,     5,     6,   136,    -1,    -1,    10,    11,    12,    -1,
      -1,    -1,    16,    17,    18,    19,    20,    -1,    22,    23,
      -1,    -1,     1,    -1,     3,     4,     5,     6,    -1,    -1,
      -1,    10,    11,    12,     7,   176,    -1,    -1,    -1,    -1,
      13,    -1,   175,    22,    23,    -1,    -1,    -1,    -1,    -1,
     183,   184,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    -1,    -1,    47,    48,    49,    50,    51,    -1,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,    -1,   120,   121,    -1,
     123,   124,   125,   126,   127,   128,   135,    -1,    -1,    -1,
      -1,    -1,    -1,   136,    -1,     1,    -1,     3,     4,     5,
       6,    -1,   176,     9,    10,    11,    12,    -1,    -1,    -1,
      16,    17,    18,    19,    20,     1,    -1,     3,     4,     5,
       6,    -1,    -1,     9,    10,    11,    12,    -1,    -1,    -1,
      16,    17,    18,    19,    20,    -1,    -1,    -1,    -1,    -1,
     183,   184,     1,    -1,     3,     4,     5,     6,    -1,    -1,
       9,    10,    11,    12,    -1,    -1,    -1,    16,    17,    18,
      19,    20,     1,    -1,     3,     4,     5,     6,    -1,    -1,
      -1,    10,    11,    12,    -1,    -1,    -1,    16,    17,    18,
      19,    20,     1,    -1,     3,     4,     5,     6,    -1,    -1,
      -1,    10,    11,    12,    -1,    -1,    -1,    16,    17,    18,
      19,    20,     3,     4,     5,     6,    -1,    -1,     9,    10,
      11,    12,    -1,    -1,    -1,    16,    17,    18,    19,    20,
       3,     4,     5,     6,    -1,    -1,    -1,    10,    11,    12,
      -1,    -1,    -1,    16,    17,    18,    19,    20,     3,     4,
       5,     6,    -1,    -1,    -1,    10,    11,    12,    -1,    -1,
      -1,    16,    17,    18,    19,    20,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,     6,    -1,    -1,
     176,    10,    11,    12,    -1,     1,    -1,     3,     4,     5,
       6,    -1,    -1,    -1,    10,    11,    12,     4,    -1,    -1,
     176,    -1,    -1,    10,    11,    12,    22,    23,    -1,    16,
      17,    18,    19,    20,     3,     4,     5,     6,    -1,    -1,
       9,    10,    11,    12,    -1,    -1,    -1,   176,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   176,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   137,    -1,    -1,   176,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   176,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   176,    -1,    -1,   122,    -1,   137,    -1,
     139,   140,    -1,    -1,   143,    -1,   145,    -1,   147,   135,
      -1,   176,    -1,   152,   153,   154,   155,   156,   157,   158,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     7,    13,    22,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    47,    48,    49,    50,    51,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   120,   121,   123,   124,   125,
     126,   127,   128,   136,   175,   183,   184,   196,   197,   199,
     200,   201,   202,   203,   205,   206,   207,   208,   209,   210,
     211,   212,   213,   248,     3,     4,     5,     6,    10,    11,
      12,    16,    17,    18,    19,    20,   179,   191,   221,   231,
     234,   242,   245,   246,     1,    22,    23,   185,   198,     1,
     172,   217,   185,   198,   185,   198,   198,   234,   234,   143,
     198,   230,   198,   230,   217,   217,   217,   217,   217,     1,
       4,    10,    11,    12,   224,   243,     9,   176,   227,   229,
     231,   232,   233,   246,   227,   227,   198,   229,   231,   198,
     221,   222,   223,   231,   181,   198,   181,   198,   226,   227,
     229,    14,   198,   219,   220,   198,   220,   198,   229,   227,
     217,   217,   198,     1,   198,   224,   224,   224,   229,   198,
     226,   198,   230,   198,   230,   234,   198,   231,   173,   198,
     214,   198,   220,   214,   198,   174,   216,   177,   198,   231,
     217,   231,   217,   231,   229,   176,   231,   198,   231,   198,
     231,   217,   198,   217,   198,   226,   198,   226,   198,   226,
     198,   226,   229,   198,   214,   217,   217,   198,   229,   198,
     226,   178,   198,   198,   198,   217,   198,   173,   215,   216,
     216,   171,   171,   217,   217,   217,   234,   198,   230,   230,
     198,   230,   198,   198,   246,   246,   198,   230,   198,   198,
     230,   217,   215,     1,   198,   218,   243,   119,   198,   122,
     135,   198,   246,   135,   198,   246,   198,   217,   217,   185,
     198,   185,   198,   129,   130,   131,   133,   134,   135,   185,
     198,    21,   137,   139,   140,   143,   145,   147,   152,   153,
     154,   155,   156,   157,   158,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   246,   250,   198,   224,   198,
     224,     0,   199,    22,   234,   179,   187,   188,   189,   190,
     198,   198,   198,   198,   198,   234,   234,   234,   234,     1,
     218,   218,     1,   218,   218,   218,   198,   230,   230,   230,
     230,   230,   230,   198,   230,    21,   143,   198,   198,   198,
     230,   198,    14,   226,   226,   198,   230,   198,   198,   198,
      15,     1,   230,   198,   204,   198,   234,   234,   198,   198,
     198,   226,   198,   198,   198,   198,   230,   198,   230,   198,
     230,   230,   198,   230,   198,   198,   198,   198,   198,   198,
     198,   198,   171,   238,   239,   246,   198,   218,   218,   198,
     198,   198,   198,   230,   198,   198,   198,   198,   171,   198,
     198,   234,   198,   234,   234,   234,   198,   198,   246,   231,
     243,   234,   230,   198,   198,   198,   185,   198,   246,   198,
     246,   198,   246,   198,   230,   198,   230,   198,   198,   243,
     244,   225,   243,   229,   229,   243,   229,   198,   229,   159,
     160,   161,   162,   163,   164,   246,   246,   143,   141,   142,
     148,   149,   170,   251,   252,   140,   143,   152,   154,   246,
     246,   143,   143,   143,   198,   198,     1,   192,   234,   234,
     234,   234,   228,   230,   229,   198,   198,   228,   228,     1,
     227,   228,     1,   229,   234,   171,   186,   240,   241,   246,
     229,     9,   232,   238,   229,   246,   223,   226,    15,   235,
     235,   229,   137,   193,   221,   236,   237,   246,   198,   171,
     247,   248,   249,   198,   198,   235,   177,   217,   217,   176,
     176,   198,   230,     1,   229,     1,   227,   234,   198,   198,
     198,   198,   198,   198,   198,   230,   198,   230,   198,   226,
     234,   198,   198,   185,   198,   246,   198,   246,   246,   246,
     198,   132,   198,   198,   198,   198,   198,    24,   198,   138,
     138,   138,   138,   138,   138,   138,   143,   251,   170,   251,
     169,   143,   170,   251,   143,   143,   143,   146,   251,   251,
     141,   142,   149,   150,   246,   252,   198,   229,   198,   198,
     198,   198,   198,   198,   198,   198,   241,   198,   198,   198,
     236,   198,   198,   198,   236,   177,   180,   198,   198,   144,
     144,   198,   198,   198,   198,   198,   198,   198,   239,   198,
     198,   198,   231,   231,   198,   198,   198,   198,   198,   198,
     198,   243,   198,   143,   141,   149,   251,   169,   251,   170,
     251,   146,   169,   251,   251,   150,   251,   143,     1,   138,
     194,     1,   236,   248,   248,   198,   230,   198,   230,   198,
     140,   142,   161,   162,   163,   138,   138,   146,   251,   146,
     146,   169,   146,   146,   146,   146,   142,   137,   229,   231,
     231,   143,   146,   146,   236,   198,   230,   198,   230,   142,
     138,   231,   231,   198,   230,   198,   230,   231,   231,   198,
     198,   230,   231,   198
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,   195,   196,   196,   196,   197,   197,   198,   198,   198,
     199,   199,   199,   199,   199,   199,   199,   199,   199,   199,
     199,   199,   199,   200,   200,   200,   200,   200,   200,   200,
     200,   200,   200,   200,   200,   200,   200,   200,   200,   200,
     200,   200,   200,   200,   200,   200,   200,   200,   200,   200,
     200,   200,   200,   200,   200,   200,   200,   200,   200,   200,
     200,   201,   201,   201,   202,   202,   202,   202,   202,   202,
     202,   202,   202,   202,   202,   202,   202,   204,   203,   203,
     203,   203,   205,   205,   205,   205,   205,   205,   205,   205,
     205,   205,   205,   205,   205,   205,   205,   205,   205,   205,
     205,   205,   206,   206,   206,   206,   206,   206,   206,   206,
     207,   207,   207,   207,   207,   207,   207,   207,   207,   207,
     207,   208,   208,   208,   208,   208,   208,   208,   208,   208,
     208,   208,   208,   208,   208,   208,   208,   209,   209,   209,
     209,   209,   209,   209,   209,   209,   209,   209,   209,   209,
     209,   209,   209,   209,   209,   209,   209,   209,   209,   209,
     209,   209,   209,   209,   209,   209,   209,   209,   209,   209,
     209,   209,   209,   209,   209,   209,   209,   209,   209,   209,
     209,   209,   209,   209,   209,   209,   209,   209,   209,   210,
     210,   210,   210,   210,   210,   210,   210,   210,   210,   210,
     210,   210,   210,   210,   210,   210,   210,   210,   210,   210,
     210,   211,   211,   211,   212,   212,   213,   214,   215,   215,
     216,   217,   217,   218,   218,   219,   219,   220,   220,   221,
     221,   222,   222,   223,   224,   224,   225,   225,   226,   226,
     227,   227,   227,   228,   228,   229,   229,   229,   230,   230,
     231,   231,   231,   231,   231,   232,   233,   234,   234,   234,
     234,   234,   234,   234,   235,   235,   236,   236,   236,   236,
     236,   237,   237,   237,   237,   238,   238,   239,   239,   240,
     240,   241,   241,   241,   242,   242,   243,   243,   243,   243,
     244,   244,   245,   245,   245,   246,   246,   246,   246,   246,
     247,   247,   247,   248,   249,   249,   250,   250,   250,   250,
     250,   250,   250,   250,   250,   250,   250,   250,   250,   250,
     250,   250,   250,   250,   250,   250,   250,   250,   250,   250,
     250,   250,   250,   250,   250,   250,   250,   250,   250,   250,
     250,   250,   250,   250,   250,   250,   250,   250,   250,   250,
     250,   250,   250,   250,   250,   250,   250,   250,   250,   250,
     250,   250,   250,   250,   250,   250,   250,   250,   251,   251,
     251,   251,   252
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     3,     3,     5,     3,     2,     2,
       3,     2,     3,     2,     4,     6,     8,    10,    12,     4,
       6,     8,    10,    12,    14,     2,     3,     3,     2,     4,
       2,     4,     2,     4,     2,     4,     2,     3,     2,     3,
       1,     2,     3,     3,     5,     3,     5,     3,     5,     3,
       5,     3,     2,     3,     2,     4,     5,     0,     5,     3,
       3,     2,     5,     5,     5,     5,     5,     3,     2,     3,
       2,     3,     2,     3,     2,     3,     2,     2,     2,     4,
       5,     5,     5,     2,     3,     2,     5,     2,     5,     2,
       3,     2,     3,     2,     3,     5,     3,     2,     5,     5,
       4,     3,     2,     3,     2,     3,     2,     3,     3,     2,
       2,     3,     2,     2,     2,     2,     3,     3,     3,     2,
       3,     3,     3,     3,     2,     3,     2,     3,     3,     3,
       5,     3,     4,     3,     3,     2,     4,     4,     2,     4,
       2,     3,     2,     4,     3,     2,     3,     4,     5,     5,
       3,     4,     4,     2,     3,     4,     5,     3,     3,     2,
       3,     2,     4,     4,     6,     4,     4,     4,     4,     5,
       5,     5,     4,     5,     3,     4,     5,     4,     5,     5,
       4,     5,     5,     2,     3,     4,     3,     3,     5,     3,
       5,     3,     2,     3,     4,     2,     2,     1,     1,     0,
       1,     1,     1,     1,     1,     2,     1,     1,     0,     1,
       2,     3,     1,     3,     1,     1,     1,     0,     1,     1,
       3,     1,     3,     2,     0,     1,     3,     1,     1,     0,
       1,     1,     1,     1,     1,     1,     1,     3,     3,     3,
       3,     3,     3,     1,     2,     0,     3,     3,     3,     3,
       1,     1,     1,     6,     4,     3,     1,     1,     1,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     0,     1,     1,     1,     1,     1,     1,     1,     1,
       3,     3,     2,     2,     1,     1,     2,     1,     3,     3,
       3,     3,     3,     5,     7,     5,     3,     3,     3,     3,
       3,     3,     5,     5,     5,     5,     0,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     3,     3,     4,     3,
       4,     2,     3,     3,     3,     3,     5,     5,     6,     5,
       6,     4,     5,     5,     5,     5,     3,     5,     1,     1,
       1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* top_level: command_list  */
#line 191 "src/monitor/mon_parse.y"
                        { (yyval.i) = 0; }
#line 2246 "src/monitor/mon_parse.c"
    break;

  case 3: /* top_level: assembly_instruction TRAIL  */
#line 192 "src/monitor/mon_parse.y"
                                      { (yyval.i) = 0; }
#line 2252 "src/monitor/mon_parse.c"
    break;

  case 4: /* top_level: TRAIL  */
#line 193 "src/monitor/mon_parse.y"
                 { new_cmd = 1; asm_mode = 0;  (yyval.i) = 0; }
#line 2258 "src/monitor/mon_parse.c"
    break;

  case 9: /* end_cmd: error  */
#line 202 "src/monitor/mon_parse.y"
               { return ERR_EXPECT_END_CMD; }
#line 2264 "src/monitor/mon_parse.c"
    break;

  case 22: /* command: BAD_CMD  */
#line 217 "src/monitor/mon_parse.y"
                 { return ERR_BAD_CMD; }
#line 2270 "src/monitor/mon_parse.c"
    break;

  case 23: /* machine_state_rules: CMD_BANK end_cmd  */
#line 221 "src/monitor/mon_parse.y"
                     { mon_bank(e_default_space, NULL); }
#line 2276 "src/monitor/mon_parse.c"
    break;

  case 24: /* machine_state_rules: CMD_BANK memspace end_cmd  */
#line 223 "src/monitor/mon_parse.y"
                     { mon_bank((yyvsp[-1].i), NULL); }
#line 2282 "src/monitor/mon_parse.c"
    break;

  case 25: /* machine_state_rules: CMD_BANK BANKNAME end_cmd  */
#line 225 "src/monitor/mon_parse.y"
                     { mon_bank(e_default_space, (yyvsp[-1].str)); }
#line 2288 "src/monitor/mon_parse.c"
    break;

  case 26: /* machine_state_rules: CMD_BANK memspace opt_sep BANKNAME end_cmd  */
#line 227 "src/monitor/mon_parse.y"
                     { mon_bank((yyvsp[-3].i), (yyvsp[-1].str)); }
#line 2294 "src/monitor/mon_parse.c"
    break;

  case 27: /* machine_state_rules: CMD_GOTO address end_cmd  */
#line 229 "src/monitor/mon_parse.y"
                     { mon_jump((yyvsp[-1].a)); }
#line 2300 "src/monitor/mon_parse.c"
    break;

  case 28: /* machine_state_rules: CMD_GOTO end_cmd  */
#line 231 "src/monitor/mon_parse.y"
                     { mon_go(); }
#line 2306 "src/monitor/mon_parse.c"
    break;

  case 29: /* machine_state_rules: CMD_IO end_cmd  */
#line 233 "src/monitor/mon_parse.y"
                     { mon_display_io_regs(0); }
#line 2312 "src/monitor/mon_parse.c"
    break;

  case 30: /* machine_state_rules: CMD_IO address end_cmd  */
#line 235 "src/monitor/mon_parse.y"
                     { mon_display_io_regs((yyvsp[-1].a)); }
#line 2318 "src/monitor/mon_parse.c"
    break;

  case 31: /* machine_state_rules: CMD_CPU end_cmd  */
#line 237 "src/monitor/mon_parse.y"
                     { monitor_cpu_type_set(""); }
#line 2324 "src/monitor/mon_parse.c"
    break;

  case 32: /* machine_state_rules: CMD_CPU CPUTYPE end_cmd  */
#line 239 "src/monitor/mon_parse.y"
                     { monitor_cpu_type_set((yyvsp[-1].str)); }
#line 2330 "src/monitor/mon_parse.c"
    break;

  case 33: /* machine_state_rules: CMD_CPUHISTORY end_cmd  */
#line 241 "src/monitor/mon_parse.y"
                     { mon_cpuhistory(-1, e_invalid_space,  e_invalid_space, e_invalid_space, e_invalid_space, e_invalid_space); }
#line 2336 "src/monitor/mon_parse.c"
    break;

  case 34: /* machine_state_rules: CMD_CPUHISTORY opt_sep memspace end_cmd  */
#line 243 "src/monitor/mon_parse.y"
                     { mon_cpuhistory(-1, (yyvsp[-1].i), e_invalid_space, e_invalid_space, e_invalid_space, e_invalid_space); }
#line 2342 "src/monitor/mon_parse.c"
    break;

  case 35: /* machine_state_rules: CMD_CPUHISTORY opt_sep memspace opt_sep memspace end_cmd  */
#line 245 "src/monitor/mon_parse.y"
                     { mon_cpuhistory(-1, (yyvsp[-3].i), (yyvsp[-1].i), e_invalid_space, e_invalid_space, e_invalid_space); }
#line 2348 "src/monitor/mon_parse.c"
    break;

  case 36: /* machine_state_rules: CMD_CPUHISTORY opt_sep memspace opt_sep memspace opt_sep memspace end_cmd  */
#line 247 "src/monitor/mon_parse.y"
                     { mon_cpuhistory(-1, (yyvsp[-5].i), (yyvsp[-3].i), (yyvsp[-1].i), e_invalid_space, e_invalid_space); }
#line 2354 "src/monitor/mon_parse.c"
    break;

  case 37: /* machine_state_rules: CMD_CPUHISTORY opt_sep memspace opt_sep memspace opt_sep memspace opt_sep memspace end_cmd  */
#line 249 "src/monitor/mon_parse.y"
                     { mon_cpuhistory(-1, (yyvsp[-7].i), (yyvsp[-5].i), (yyvsp[-3].i),  (yyvsp[-1].i), e_invalid_space); }
#line 2360 "src/monitor/mon_parse.c"
    break;

  case 38: /* machine_state_rules: CMD_CPUHISTORY opt_sep memspace opt_sep memspace opt_sep memspace opt_sep memspace opt_sep memspace end_cmd  */
#line 251 "src/monitor/mon_parse.y"
                     { mon_cpuhistory(-1, (yyvsp[-9].i), (yyvsp[-7].i), (yyvsp[-5].i),  (yyvsp[-3].i), (yyvsp[-1].i)); }
#line 2366 "src/monitor/mon_parse.c"
    break;

  case 39: /* machine_state_rules: CMD_CPUHISTORY opt_sep d_number end_cmd  */
#line 253 "src/monitor/mon_parse.y"
                     { mon_cpuhistory((yyvsp[-1].i), e_invalid_space, e_invalid_space, e_invalid_space, e_invalid_space, e_invalid_space); }
#line 2372 "src/monitor/mon_parse.c"
    break;

  case 40: /* machine_state_rules: CMD_CPUHISTORY opt_sep d_number opt_sep memspace end_cmd  */
#line 255 "src/monitor/mon_parse.y"
                     { mon_cpuhistory((yyvsp[-3].i), (yyvsp[-1].i), e_invalid_space, e_invalid_space, e_invalid_space, e_invalid_space); }
#line 2378 "src/monitor/mon_parse.c"
    break;

  case 41: /* machine_state_rules: CMD_CPUHISTORY opt_sep d_number opt_sep memspace opt_sep memspace end_cmd  */
#line 257 "src/monitor/mon_parse.y"
                     { mon_cpuhistory((yyvsp[-5].i), (yyvsp[-3].i), (yyvsp[-1].i), e_invalid_space, e_invalid_space, e_invalid_space); }
#line 2384 "src/monitor/mon_parse.c"
    break;

  case 42: /* machine_state_rules: CMD_CPUHISTORY opt_sep d_number opt_sep memspace opt_sep memspace opt_sep memspace end_cmd  */
#line 259 "src/monitor/mon_parse.y"
                     { mon_cpuhistory((yyvsp[-7].i), (yyvsp[-5].i), (yyvsp[-3].i), (yyvsp[-1].i), e_invalid_space, e_invalid_space); }
#line 2390 "src/monitor/mon_parse.c"
    break;

  case 43: /* machine_state_rules: CMD_CPUHISTORY opt_sep d_number opt_sep memspace opt_sep memspace opt_sep memspace opt_sep memspace end_cmd  */
#line 261 "src/monitor/mon_parse.y"
                     { mon_cpuhistory((yyvsp[-9].i), (yyvsp[-7].i), (yyvsp[-5].i), (yyvsp[-3].i), (yyvsp[-1].i), e_invalid_space); }
#line 2396 "src/monitor/mon_parse.c"
    break;

  case 44: /* machine_state_rules: CMD_CPUHISTORY opt_sep d_number opt_sep memspace opt_sep memspace opt_sep memspace opt_sep memspace opt_sep memspace end_cmd  */
#line 263 "src/monitor/mon_parse.y"
                     { mon_cpuhistory((yyvsp[-11].i), (yyvsp[-9].i), (yyvsp[-7].i), (yyvsp[-5].i), (yyvsp[-3].i), (yyvsp[-1].i)); }
#line 2402 "src/monitor/mon_parse.c"
    break;

  case 45: /* machine_state_rules: CMD_RETURN end_cmd  */
#line 265 "src/monitor/mon_parse.y"
                     { mon_instruction_return(); }
#line 2408 "src/monitor/mon_parse.c"
    break;

  case 46: /* machine_state_rules: CMD_DUMP filename end_cmd  */
#line 267 "src/monitor/mon_parse.y"
                     { mon_write_snapshot((yyvsp[-1].str),0,0,0); /* FIXME */ }
#line 2414 "src/monitor/mon_parse.c"
    break;

  case 47: /* machine_state_rules: CMD_UNDUMP filename end_cmd  */
#line 269 "src/monitor/mon_parse.y"
                     { mon_read_snapshot((yyvsp[-1].str), 0); }
#line 2420 "src/monitor/mon_parse.c"
    break;

  case 48: /* machine_state_rules: CMD_STEP end_cmd  */
#line 271 "src/monitor/mon_parse.y"
                     { mon_instructions_step(-1); }
#line 2426 "src/monitor/mon_parse.c"
    break;

  case 49: /* machine_state_rules: CMD_STEP opt_sep expression end_cmd  */
#line 273 "src/monitor/mon_parse.y"
                     { mon_instructions_step((yyvsp[-1].i)); }
#line 2432 "src/monitor/mon_parse.c"
    break;

  case 50: /* machine_state_rules: CMD_NEXT end_cmd  */
#line 275 "src/monitor/mon_parse.y"
                     { mon_instructions_next(-1); }
#line 2438 "src/monitor/mon_parse.c"
    break;

  case 51: /* machine_state_rules: CMD_NEXT opt_sep expression end_cmd  */
#line 277 "src/monitor/mon_parse.y"
                     { mon_instructions_next((yyvsp[-1].i)); }
#line 2444 "src/monitor/mon_parse.c"
    break;

  case 52: /* machine_state_rules: CMD_UP end_cmd  */
#line 279 "src/monitor/mon_parse.y"
                     { mon_stack_up(-1); }
#line 2450 "src/monitor/mon_parse.c"
    break;

  case 53: /* machine_state_rules: CMD_UP opt_sep expression end_cmd  */
#line 281 "src/monitor/mon_parse.y"
                     { mon_stack_up((yyvsp[-1].i)); }
#line 2456 "src/monitor/mon_parse.c"
    break;

  case 54: /* machine_state_rules: CMD_DOWN end_cmd  */
#line 283 "src/monitor/mon_parse.y"
                     { mon_stack_down(-1); }
#line 2462 "src/monitor/mon_parse.c"
    break;

  case 55: /* machine_state_rules: CMD_DOWN opt_sep expression end_cmd  */
#line 285 "src/monitor/mon_parse.y"
                     { mon_stack_down((yyvsp[-1].i)); }
#line 2468 "src/monitor/mon_parse.c"
    break;

  case 56: /* machine_state_rules: CMD_SCREEN end_cmd  */
#line 287 "src/monitor/mon_parse.y"
                     { mon_display_screen(-1); }
#line 2474 "src/monitor/mon_parse.c"
    break;

  case 57: /* machine_state_rules: CMD_SCREEN address end_cmd  */
#line 289 "src/monitor/mon_parse.y"
                     { mon_display_screen((yyvsp[-1].a)); }
#line 2480 "src/monitor/mon_parse.c"
    break;

  case 58: /* machine_state_rules: CMD_WARP end_cmd  */
#line 291 "src/monitor/mon_parse.y"
                     {
                        mon_out("Warp mode is %s.\n",
                                vsync_get_warp_mode() ? "on" : "off");
                     }
#line 2489 "src/monitor/mon_parse.c"
    break;

  case 59: /* machine_state_rules: CMD_WARP TOGGLE end_cmd  */
#line 296 "src/monitor/mon_parse.y"
                     {
                        vsync_set_warp_mode((((yyvsp[-1].action) == e_TOGGLE) ? (vsync_get_warp_mode() ^ 1) : (yyvsp[-1].action)));
                     }
#line 2497 "src/monitor/mon_parse.c"
    break;

  case 61: /* register_mod: CMD_REGISTERS end_cmd  */
#line 303 "src/monitor/mon_parse.y"
              { (monitor_cpu_for_memspace[default_memspace]->mon_register_print)(default_memspace); }
#line 2503 "src/monitor/mon_parse.c"
    break;

  case 62: /* register_mod: CMD_REGISTERS memspace end_cmd  */
#line 305 "src/monitor/mon_parse.y"
              { (monitor_cpu_for_memspace[(yyvsp[-1].i)]->mon_register_print)((yyvsp[-1].i)); }
#line 2509 "src/monitor/mon_parse.c"
    break;

  case 64: /* symbol_table_rules: CMD_LOAD_LABELS memspace opt_sep filename end_cmd  */
#line 310 "src/monitor/mon_parse.y"
                    {
                        /* What about the memspace? */
                        mon_playback_commands((yyvsp[-1].str),true);
                    }
#line 2518 "src/monitor/mon_parse.c"
    break;

  case 65: /* symbol_table_rules: CMD_LOAD_LABELS filename end_cmd  */
#line 315 "src/monitor/mon_parse.y"
                    {
                        /* What about the memspace? */
                        mon_playback_commands((yyvsp[-1].str),true);
                    }
#line 2527 "src/monitor/mon_parse.c"
    break;

  case 66: /* symbol_table_rules: CMD_SAVE_LABELS memspace opt_sep filename end_cmd  */
#line 320 "src/monitor/mon_parse.y"
                    { mon_save_symbols((yyvsp[-3].i), (yyvsp[-1].str)); }
#line 2533 "src/monitor/mon_parse.c"
    break;

  case 67: /* symbol_table_rules: CMD_SAVE_LABELS filename end_cmd  */
#line 322 "src/monitor/mon_parse.y"
                    { mon_save_symbols(e_default_space, (yyvsp[-1].str)); }
#line 2539 "src/monitor/mon_parse.c"
    break;

  case 68: /* symbol_table_rules: CMD_ADD_LABEL address opt_sep LABEL end_cmd  */
#line 324 "src/monitor/mon_parse.y"
                    { mon_add_name_to_symbol_table((yyvsp[-3].a), (yyvsp[-1].str)); }
#line 2545 "src/monitor/mon_parse.c"
    break;

  case 69: /* symbol_table_rules: CMD_DEL_LABEL LABEL end_cmd  */
#line 326 "src/monitor/mon_parse.y"
                    { mon_remove_name_from_symbol_table(e_default_space, (yyvsp[-1].str)); }
#line 2551 "src/monitor/mon_parse.c"
    break;

  case 70: /* symbol_table_rules: CMD_DEL_LABEL memspace opt_sep LABEL end_cmd  */
#line 328 "src/monitor/mon_parse.y"
                    { mon_remove_name_from_symbol_table((yyvsp[-3].i), (yyvsp[-1].str)); }
#line 2557 "src/monitor/mon_parse.c"
    break;

  case 71: /* symbol_table_rules: CMD_SHOW_LABELS memspace end_cmd  */
#line 330 "src/monitor/mon_parse.y"
                    { mon_print_symbol_table((yyvsp[-1].i)); }
#line 2563 "src/monitor/mon_parse.c"
    break;

  case 72: /* symbol_table_rules: CMD_SHOW_LABELS end_cmd  */
#line 332 "src/monitor/mon_parse.y"
                    { mon_print_symbol_table(e_default_space); }
#line 2569 "src/monitor/mon_parse.c"
    break;

  case 73: /* symbol_table_rules: CMD_CLEAR_LABELS memspace end_cmd  */
#line 334 "src/monitor/mon_parse.y"
                    { mon_clear_symbol_table((yyvsp[-1].i)); }
#line 2575 "src/monitor/mon_parse.c"
    break;

  case 74: /* symbol_table_rules: CMD_CLEAR_LABELS end_cmd  */
#line 336 "src/monitor/mon_parse.y"
                    { mon_clear_symbol_table(e_default_space); }
#line 2581 "src/monitor/mon_parse.c"
    break;

  case 75: /* symbol_table_rules: CMD_LABEL_ASGN EQUALS address end_cmd  */
#line 338 "src/monitor/mon_parse.y"
                    {
                        mon_add_name_to_symbol_table((yyvsp[-1].a), mon_prepend_dot_to_name((yyvsp[-3].str)));
                    }
#line 2589 "src/monitor/mon_parse.c"
    break;

  case 76: /* symbol_table_rules: CMD_LABEL_ASGN EQUALS address LABEL_ASGN_COMMENT end_cmd  */
#line 342 "src/monitor/mon_parse.y"
                    {
                        mon_add_name_to_symbol_table((yyvsp[-2].a), mon_prepend_dot_to_name((yyvsp[-4].str)));
                    }
#line 2597 "src/monitor/mon_parse.c"
    break;

  case 77: /* $@1: %empty  */
#line 348 "src/monitor/mon_parse.y"
           { mon_start_assemble_mode((yyvsp[0].a), NULL); }
#line 2603 "src/monitor/mon_parse.c"
    break;

  case 78: /* asm_rules: CMD_ASSEMBLE address $@1 post_assemble end_cmd  */
#line 349 "src/monitor/mon_parse.y"
           { }
#line 2609 "src/monitor/mon_parse.c"
    break;

  case 79: /* asm_rules: CMD_ASSEMBLE address end_cmd  */
#line 351 "src/monitor/mon_parse.y"
           { mon_start_assemble_mode((yyvsp[-1].a), NULL); }
#line 2615 "src/monitor/mon_parse.c"
    break;

  case 80: /* asm_rules: CMD_DISASSEMBLE address_opt_range end_cmd  */
#line 353 "src/monitor/mon_parse.y"
           { mon_disassemble_lines((yyvsp[-1].range)[0], (yyvsp[-1].range)[1]); }
#line 2621 "src/monitor/mon_parse.c"
    break;

  case 81: /* asm_rules: CMD_DISASSEMBLE end_cmd  */
#line 355 "src/monitor/mon_parse.y"
           { mon_disassemble_lines(BAD_ADDR, BAD_ADDR); }
#line 2627 "src/monitor/mon_parse.c"
    break;

  case 82: /* memory_rules: CMD_MOVE address_range opt_sep address end_cmd  */
#line 359 "src/monitor/mon_parse.y"
              { mon_memory_move((yyvsp[-3].range)[0], (yyvsp[-3].range)[1], (yyvsp[-1].a)); }
#line 2633 "src/monitor/mon_parse.c"
    break;

  case 83: /* memory_rules: CMD_COMPARE address_range opt_sep address end_cmd  */
#line 361 "src/monitor/mon_parse.y"
              { mon_memory_compare((yyvsp[-3].range)[0], (yyvsp[-3].range)[1], (yyvsp[-1].a)); }
#line 2639 "src/monitor/mon_parse.c"
    break;

  case 84: /* memory_rules: CMD_FILL address_range opt_sep data_list end_cmd  */
#line 363 "src/monitor/mon_parse.y"
              { mon_memory_fill((yyvsp[-3].range)[0], (yyvsp[-3].range)[1],(unsigned char *)(yyvsp[-1].str)); }
#line 2645 "src/monitor/mon_parse.c"
    break;

  case 85: /* memory_rules: CMD_HUNT address_range opt_sep hunt_list end_cmd  */
#line 365 "src/monitor/mon_parse.y"
              { mon_memory_hunt((yyvsp[-3].range)[0], (yyvsp[-3].range)[1],(unsigned char *)(yyvsp[-1].str)); }
#line 2651 "src/monitor/mon_parse.c"
    break;

  case 86: /* memory_rules: CMD_MEM_DISPLAY RADIX_TYPE opt_sep address_opt_range end_cmd  */
#line 367 "src/monitor/mon_parse.y"
              { mon_memory_display((yyvsp[-3].rt), (yyvsp[-1].range)[0], (yyvsp[-1].range)[1], DF_PETSCII); }
#line 2657 "src/monitor/mon_parse.c"
    break;

  case 87: /* memory_rules: CMD_MEM_DISPLAY address_opt_range end_cmd  */
#line 369 "src/monitor/mon_parse.y"
              { mon_memory_display(default_radix, (yyvsp[-1].range)[0], (yyvsp[-1].range)[1], DF_PETSCII); }
#line 2663 "src/monitor/mon_parse.c"
    break;

  case 88: /* memory_rules: CMD_MEM_DISPLAY end_cmd  */
#line 371 "src/monitor/mon_parse.y"
              { mon_memory_display(default_radix, BAD_ADDR, BAD_ADDR, DF_PETSCII); }
#line 2669 "src/monitor/mon_parse.c"
    break;

  case 89: /* memory_rules: CMD_CHAR_DISPLAY address_opt_range end_cmd  */
#line 373 "src/monitor/mon_parse.y"
              { mon_memory_display_data((yyvsp[-1].range)[0], (yyvsp[-1].range)[1], 8, 8); }
#line 2675 "src/monitor/mon_parse.c"
    break;

  case 90: /* memory_rules: CMD_CHAR_DISPLAY end_cmd  */
#line 375 "src/monitor/mon_parse.y"
              { mon_memory_display_data(BAD_ADDR, BAD_ADDR, 8, 8); }
#line 2681 "src/monitor/mon_parse.c"
    break;

  case 91: /* memory_rules: CMD_SPRITE_DISPLAY address_opt_range end_cmd  */
#line 377 "src/monitor/mon_parse.y"
              { mon_memory_display_data((yyvsp[-1].range)[0], (yyvsp[-1].range)[1], 24, 21); }
#line 2687 "src/monitor/mon_parse.c"
    break;

  case 92: /* memory_rules: CMD_SPRITE_DISPLAY end_cmd  */
#line 379 "src/monitor/mon_parse.y"
              { mon_memory_display_data(BAD_ADDR, BAD_ADDR, 24, 21); }
#line 2693 "src/monitor/mon_parse.c"
    break;

  case 93: /* memory_rules: CMD_TEXT_DISPLAY address_opt_range end_cmd  */
#line 381 "src/monitor/mon_parse.y"
              { mon_memory_display(e_text, (yyvsp[-1].range)[0], (yyvsp[-1].range)[1], DF_PETSCII); }
#line 2699 "src/monitor/mon_parse.c"
    break;

  case 94: /* memory_rules: CMD_TEXT_DISPLAY end_cmd  */
#line 383 "src/monitor/mon_parse.y"
              { mon_memory_display(e_text, BAD_ADDR, BAD_ADDR, DF_PETSCII); }
#line 2705 "src/monitor/mon_parse.c"
    break;

  case 95: /* memory_rules: CMD_SCREENCODE_DISPLAY address_opt_range end_cmd  */
#line 385 "src/monitor/mon_parse.y"
              { mon_memory_display(e_text, (yyvsp[-1].range)[0], (yyvsp[-1].range)[1], DF_SCREEN_CODE); }
#line 2711 "src/monitor/mon_parse.c"
    break;

  case 96: /* memory_rules: CMD_SCREENCODE_DISPLAY end_cmd  */
#line 387 "src/monitor/mon_parse.y"
              { mon_memory_display(e_text, BAD_ADDR, BAD_ADDR, DF_SCREEN_CODE); }
#line 2717 "src/monitor/mon_parse.c"
    break;

  case 97: /* memory_rules: CMD_MEMMAPZAP end_cmd  */
#line 389 "src/monitor/mon_parse.y"
              { mon_memmap_zap(); }
#line 2723 "src/monitor/mon_parse.c"
    break;

  case 98: /* memory_rules: CMD_MEMMAPSHOW end_cmd  */
#line 391 "src/monitor/mon_parse.y"
              { mon_memmap_show(-1,BAD_ADDR,BAD_ADDR); }
#line 2729 "src/monitor/mon_parse.c"
    break;

  case 99: /* memory_rules: CMD_MEMMAPSHOW opt_sep expression end_cmd  */
#line 393 "src/monitor/mon_parse.y"
              { mon_memmap_show((yyvsp[-1].i),BAD_ADDR,BAD_ADDR); }
#line 2735 "src/monitor/mon_parse.c"
    break;

  case 100: /* memory_rules: CMD_MEMMAPSHOW opt_sep expression address_opt_range end_cmd  */
#line 395 "src/monitor/mon_parse.y"
              { mon_memmap_show((yyvsp[-2].i),(yyvsp[-1].range)[0],(yyvsp[-1].range)[1]); }
#line 2741 "src/monitor/mon_parse.c"
    break;

  case 101: /* memory_rules: CMD_MEMMAPSAVE filename opt_sep expression end_cmd  */
#line 397 "src/monitor/mon_parse.y"
              { mon_memmap_save((yyvsp[-3].str),(yyvsp[-1].i)); }
#line 2747 "src/monitor/mon_parse.c"
    break;

  case 102: /* checkpoint_rules: CMD_BREAK opt_mem_op address_opt_range opt_if_cond_expr end_cmd  */
#line 401 "src/monitor/mon_parse.y"
                  {
                      if ((yyvsp[-3].i)) {
                          temp = mon_breakpoint_add_checkpoint((yyvsp[-2].range)[0], (yyvsp[-2].range)[1], TRUE, (yyvsp[-3].i), FALSE, TRUE);
                      } else {
                          temp = mon_breakpoint_add_checkpoint((yyvsp[-2].range)[0], (yyvsp[-2].range)[1], TRUE, e_exec, FALSE, TRUE);
                      }
                      mon_breakpoint_set_checkpoint_condition(temp, (yyvsp[-1].cond_node));
                  }
#line 2760 "src/monitor/mon_parse.c"
    break;

  case 103: /* checkpoint_rules: CMD_BREAK end_cmd  */
#line 410 "src/monitor/mon_parse.y"
                  { mon_breakpoint_print_checkpoints(); }
#line 2766 "src/monitor/mon_parse.c"
    break;

  case 104: /* checkpoint_rules: CMD_UNTIL address_opt_range end_cmd  */
#line 413 "src/monitor/mon_parse.y"
                  {
                      mon_breakpoint_add_checkpoint((yyvsp[-1].range)[0], (yyvsp[-1].range)[1], TRUE, e_exec, TRUE, TRUE);
                  }
#line 2774 "src/monitor/mon_parse.c"
    break;

  case 105: /* checkpoint_rules: CMD_UNTIL end_cmd  */
#line 417 "src/monitor/mon_parse.y"
                  { mon_breakpoint_print_checkpoints(); }
#line 2780 "src/monitor/mon_parse.c"
    break;

  case 106: /* checkpoint_rules: CMD_WATCH opt_mem_op address_opt_range opt_if_cond_expr end_cmd  */
#line 420 "src/monitor/mon_parse.y"
                  {
                      if ((yyvsp[-3].i)) {
                          temp = mon_breakpoint_add_checkpoint((yyvsp[-2].range)[0], (yyvsp[-2].range)[1], TRUE, (yyvsp[-3].i), FALSE, TRUE);
                      } else {
                          temp = mon_breakpoint_add_checkpoint((yyvsp[-2].range)[0], (yyvsp[-2].range)[1], TRUE, e_load | e_store, FALSE, TRUE);
                      }
                      mon_breakpoint_set_checkpoint_condition(temp, (yyvsp[-1].cond_node));
                  }
#line 2793 "src/monitor/mon_parse.c"
    break;

  case 107: /* checkpoint_rules: CMD_WATCH end_cmd  */
#line 429 "src/monitor/mon_parse.y"
                  { mon_breakpoint_print_checkpoints(); }
#line 2799 "src/monitor/mon_parse.c"
    break;

  case 108: /* checkpoint_rules: CMD_TRACE opt_mem_op address_opt_range opt_if_cond_expr end_cmd  */
#line 432 "src/monitor/mon_parse.y"
                  {
                      if ((yyvsp[-3].i)) {
                          temp = mon_breakpoint_add_checkpoint((yyvsp[-2].range)[0], (yyvsp[-2].range)[1], FALSE, (yyvsp[-3].i), FALSE, TRUE);
                      } else {
                          temp = mon_breakpoint_add_checkpoint((yyvsp[-2].range)[0], (yyvsp[-2].range)[1], FALSE, e_exec | e_load | e_store, FALSE, TRUE);
                      }
                      mon_breakpoint_set_checkpoint_condition(temp, (yyvsp[-1].cond_node));
                  }
#line 2812 "src/monitor/mon_parse.c"
    break;

  case 109: /* checkpoint_rules: CMD_TRACE end_cmd  */
#line 441 "src/monitor/mon_parse.y"
                  { mon_breakpoint_print_checkpoints(); }
#line 2818 "src/monitor/mon_parse.c"
    break;

  case 110: /* checkpoint_control_rules: CMD_CHECKPT_ON checkpt_num end_cmd  */
#line 446 "src/monitor/mon_parse.y"
                          { mon_breakpoint_switch_checkpoint(e_ON, (yyvsp[-1].i)); }
#line 2824 "src/monitor/mon_parse.c"
    break;

  case 111: /* checkpoint_control_rules: CMD_CHECKPT_ON end_cmd  */
#line 448 "src/monitor/mon_parse.y"
                          { mon_breakpoint_switch_checkpoint(e_ON, -1); }
#line 2830 "src/monitor/mon_parse.c"
    break;

  case 112: /* checkpoint_control_rules: CMD_CHECKPT_OFF checkpt_num end_cmd  */
#line 450 "src/monitor/mon_parse.y"
                          { mon_breakpoint_switch_checkpoint(e_OFF, (yyvsp[-1].i)); }
#line 2836 "src/monitor/mon_parse.c"
    break;

  case 113: /* checkpoint_control_rules: CMD_CHECKPT_OFF end_cmd  */
#line 452 "src/monitor/mon_parse.y"
                          { mon_breakpoint_switch_checkpoint(e_OFF, -1); }
#line 2842 "src/monitor/mon_parse.c"
    break;

  case 114: /* checkpoint_control_rules: CMD_IGNORE checkpt_num end_cmd  */
#line 454 "src/monitor/mon_parse.y"
                          { mon_breakpoint_set_ignore_count((yyvsp[-1].i), -1); }
#line 2848 "src/monitor/mon_parse.c"
    break;

  case 115: /* checkpoint_control_rules: CMD_IGNORE checkpt_num opt_sep expression end_cmd  */
#line 456 "src/monitor/mon_parse.y"
                          { mon_breakpoint_set_ignore_count((yyvsp[-3].i), (yyvsp[-1].i)); }
#line 2854 "src/monitor/mon_parse.c"
    break;

  case 116: /* checkpoint_control_rules: CMD_DELETE checkpt_num end_cmd  */
#line 458 "src/monitor/mon_parse.y"
                          { mon_breakpoint_delete_checkpoint((yyvsp[-1].i)); }
#line 2860 "src/monitor/mon_parse.c"
    break;

  case 117: /* checkpoint_control_rules: CMD_DELETE end_cmd  */
#line 460 "src/monitor/mon_parse.y"
                          { mon_breakpoint_delete_checkpoint(-1); }
#line 2866 "src/monitor/mon_parse.c"
    break;

  case 118: /* checkpoint_control_rules: CMD_CONDITION checkpt_num IF cond_expr end_cmd  */
#line 462 "src/monitor/mon_parse.y"
                          { mon_breakpoint_set_checkpoint_condition((yyvsp[-3].i), (yyvsp[-1].cond_node)); }
#line 2872 "src/monitor/mon_parse.c"
    break;

  case 119: /* checkpoint_control_rules: CMD_COMMAND checkpt_num opt_sep STRING end_cmd  */
#line 464 "src/monitor/mon_parse.y"
                          { mon_breakpoint_set_checkpoint_command((yyvsp[-3].i), (yyvsp[-1].str)); }
#line 2878 "src/monitor/mon_parse.c"
    break;

  case 120: /* checkpoint_control_rules: CMD_COMMAND checkpt_num error end_cmd  */
#line 466 "src/monitor/mon_parse.y"
                          { return ERR_EXPECT_STRING; }
#line 2884 "src/monitor/mon_parse.c"
    break;

  case 121: /* monitor_state_rules: CMD_SIDEFX TOGGLE end_cmd  */
#line 470 "src/monitor/mon_parse.y"
                     { sidefx = (((yyvsp[-1].action) == e_TOGGLE) ? (sidefx ^ 1) : (yyvsp[-1].action)); }
#line 2890 "src/monitor/mon_parse.c"
    break;

  case 122: /* monitor_state_rules: CMD_SIDEFX end_cmd  */
#line 472 "src/monitor/mon_parse.y"
                     {
                         mon_out("I/O side effects are %s\n",
                                   sidefx ? "enabled" : "disabled");
                     }
#line 2899 "src/monitor/mon_parse.c"
    break;

  case 123: /* monitor_state_rules: CMD_DUMMY TOGGLE end_cmd  */
#line 477 "src/monitor/mon_parse.y"
                     { 
                         break_on_dummy_access = (((yyvsp[-1].action) == e_TOGGLE) ? (break_on_dummy_access ^ 1) : (yyvsp[-1].action)); 
                         /* FIXME: some day we might want to toggle the break-on-dummy-access 
                                   per MEMSPACE, for now its a global option */                         
                         mon_breakpoint_set_dummy_state(e_default_space, break_on_dummy_access);
                     }
#line 2910 "src/monitor/mon_parse.c"
    break;

  case 124: /* monitor_state_rules: CMD_DUMMY end_cmd  */
#line 484 "src/monitor/mon_parse.y"
                     {
                         mon_out("Checkpoints will %strigger on dummy accesses.\n",
                                   break_on_dummy_access ? "" : "not ");
                         /* FIXME: some day we might want to toggle the break-on-dummy-access 
                                   per MEMSPACE, for now its a global option */                         
                         mon_breakpoint_set_dummy_state(e_default_space, break_on_dummy_access);
                     }
#line 2922 "src/monitor/mon_parse.c"
    break;

  case 125: /* monitor_state_rules: CMD_LOG TOGGLE end_cmd  */
#line 492 "src/monitor/mon_parse.y"
                     { 
                        int logenabled;
                        resources_get_int("MonitorLogEnabled", &logenabled);
                        logenabled = (((yyvsp[-1].action) == e_TOGGLE) ? (logenabled ^ 1) : (yyvsp[-1].action));
                        resources_set_int("MonitorLogEnabled", logenabled);
                     }
#line 2933 "src/monitor/mon_parse.c"
    break;

  case 126: /* monitor_state_rules: CMD_LOG end_cmd  */
#line 499 "src/monitor/mon_parse.y"
                     {
                         int logenabled;
                         const char *logfilename;
                         resources_get_int("MonitorLogEnabled", &logenabled);
                         resources_get_string("MonitorLogFileName", &logfilename);
                         if (logenabled) {
                            mon_out("Logging to '%s' is enabled.\n", logfilename);
                         } else {
                            mon_out("Logging is disabled.\n");
                         }
                     }
#line 2949 "src/monitor/mon_parse.c"
    break;

  case 127: /* monitor_state_rules: CMD_LOGNAME filename end_cmd  */
#line 511 "src/monitor/mon_parse.y"
                     { 
                        resources_set_string("MonitorLogFileName", (yyvsp[-1].str));
                     }
#line 2957 "src/monitor/mon_parse.c"
    break;

  case 128: /* monitor_state_rules: CMD_RADIX RADIX_TYPE end_cmd  */
#line 515 "src/monitor/mon_parse.y"
                     { default_radix = (yyvsp[-1].rt); }
#line 2963 "src/monitor/mon_parse.c"
    break;

  case 129: /* monitor_state_rules: CMD_RADIX end_cmd  */
#line 517 "src/monitor/mon_parse.y"
                     {
                         const char *p;

                         if (default_radix == e_hexadecimal)
                             p = "Hexadecimal";
                         else if (default_radix == e_decimal)
                             p = "Decimal";
                         else if (default_radix == e_octal)
                             p = "Octal";
                         else if (default_radix == e_binary)
                             p = "Binary";
                         else
                             p = "Unknown";

                         mon_out("Default radix is %s\n", p);
                     }
#line 2984 "src/monitor/mon_parse.c"
    break;

  case 130: /* monitor_state_rules: CMD_DEVICE end_cmd  */
#line 535 "src/monitor/mon_parse.y"
                     { monitor_change_device(e_default_space); }
#line 2990 "src/monitor/mon_parse.c"
    break;

  case 131: /* monitor_state_rules: CMD_DEVICE memspace end_cmd  */
#line 537 "src/monitor/mon_parse.y"
                     { monitor_change_device((yyvsp[-1].i)); }
#line 2996 "src/monitor/mon_parse.c"
    break;

  case 132: /* monitor_state_rules: CMD_EXPORT end_cmd  */
#line 539 "src/monitor/mon_parse.y"
                     { mon_export(); }
#line 3002 "src/monitor/mon_parse.c"
    break;

  case 133: /* monitor_state_rules: CMD_QUIT end_cmd  */
#line 541 "src/monitor/mon_parse.y"
                     { mon_quit(); YYACCEPT; }
#line 3008 "src/monitor/mon_parse.c"
    break;

  case 134: /* monitor_state_rules: CMD_EXIT end_cmd  */
#line 543 "src/monitor/mon_parse.y"
                     { mon_exit(); YYACCEPT; }
#line 3014 "src/monitor/mon_parse.c"
    break;

  case 135: /* monitor_state_rules: CMD_MAINCPU_TRACE end_cmd  */
#line 545 "src/monitor/mon_parse.y"
                     { mon_maincpu_trace(); }
#line 3020 "src/monitor/mon_parse.c"
    break;

  case 136: /* monitor_state_rules: CMD_MAINCPU_TRACE TOGGLE end_cmd  */
#line 547 "src/monitor/mon_parse.y"
                     { mon_maincpu_toggle_trace((yyvsp[-1].action)); }
#line 3026 "src/monitor/mon_parse.c"
    break;

  case 137: /* monitor_misc_rules: CMD_DISK rest_of_line end_cmd  */
#line 551 "src/monitor/mon_parse.y"
                    { mon_drive_execute_disk_cmd((yyvsp[-1].str)); }
#line 3032 "src/monitor/mon_parse.c"
    break;

  case 138: /* monitor_misc_rules: CMD_PRINT expression end_cmd  */
#line 553 "src/monitor/mon_parse.y"
                    { mon_out("\t%d\n",(yyvsp[-1].i)); }
#line 3038 "src/monitor/mon_parse.c"
    break;

  case 139: /* monitor_misc_rules: CMD_HELP end_cmd  */
#line 555 "src/monitor/mon_parse.y"
                    { mon_command_print_help(NULL); }
#line 3044 "src/monitor/mon_parse.c"
    break;

  case 140: /* monitor_misc_rules: CMD_HELP rest_of_line end_cmd  */
#line 557 "src/monitor/mon_parse.y"
                    { mon_command_print_help((yyvsp[-1].str)); }
#line 3050 "src/monitor/mon_parse.c"
    break;

  case 141: /* monitor_misc_rules: CONVERT_OP expression end_cmd  */
#line 559 "src/monitor/mon_parse.y"
                    { mon_print_convert((yyvsp[-1].i)); }
#line 3056 "src/monitor/mon_parse.c"
    break;

  case 142: /* monitor_misc_rules: CMD_CHDIR rest_of_line_or_quoted end_cmd  */
#line 561 "src/monitor/mon_parse.y"
                    { mon_change_dir((yyvsp[-1].str)); }
#line 3062 "src/monitor/mon_parse.c"
    break;

  case 143: /* monitor_misc_rules: CMD_KEYBUF rest_of_line end_cmd  */
#line 563 "src/monitor/mon_parse.y"
                    { mon_keyboard_feed((yyvsp[-1].str)); }
#line 3068 "src/monitor/mon_parse.c"
    break;

  case 144: /* monitor_misc_rules: CMD_BACKTRACE end_cmd  */
#line 565 "src/monitor/mon_parse.y"
                    { mon_backtrace(); }
#line 3074 "src/monitor/mon_parse.c"
    break;

  case 145: /* monitor_misc_rules: CMD_DIR opt_rest_of_line end_cmd  */
#line 567 "src/monitor/mon_parse.y"
                     { mon_show_dir((yyvsp[-1].str)); }
#line 3080 "src/monitor/mon_parse.c"
    break;

  case 146: /* monitor_misc_rules: CMD_PWD end_cmd  */
#line 569 "src/monitor/mon_parse.y"
                     { mon_show_pwd(); }
#line 3086 "src/monitor/mon_parse.c"
    break;

  case 147: /* monitor_misc_rules: CMD_MKDIR rest_of_line_or_quoted end_cmd  */
#line 571 "src/monitor/mon_parse.y"
                    { mon_make_dir((yyvsp[-1].str)); }
#line 3092 "src/monitor/mon_parse.c"
    break;

  case 148: /* monitor_misc_rules: CMD_RMDIR rest_of_line_or_quoted end_cmd  */
#line 573 "src/monitor/mon_parse.y"
                    { mon_remove_dir((yyvsp[-1].str)); }
#line 3098 "src/monitor/mon_parse.c"
    break;

  case 149: /* monitor_misc_rules: CMD_SCREENSHOT filename end_cmd  */
#line 575 "src/monitor/mon_parse.y"
                    { mon_screenshot_save((yyvsp[-1].str),-1); }
#line 3104 "src/monitor/mon_parse.c"
    break;

  case 150: /* monitor_misc_rules: CMD_SCREENSHOT filename opt_sep expression end_cmd  */
#line 577 "src/monitor/mon_parse.y"
                    { mon_screenshot_save((yyvsp[-3].str),(yyvsp[-1].i)); }
#line 3110 "src/monitor/mon_parse.c"
    break;

  case 151: /* monitor_misc_rules: CMD_RESOURCE_GET STRING end_cmd  */
#line 579 "src/monitor/mon_parse.y"
                    { mon_resource_get((yyvsp[-1].str)); }
#line 3116 "src/monitor/mon_parse.c"
    break;

  case 152: /* monitor_misc_rules: CMD_RESOURCE_SET STRING STRING end_cmd  */
#line 581 "src/monitor/mon_parse.y"
                    { mon_resource_set((yyvsp[-2].str),(yyvsp[-1].str)); }
#line 3122 "src/monitor/mon_parse.c"
    break;

  case 153: /* monitor_misc_rules: CMD_LOAD_RESOURCES filename end_cmd  */
#line 583 "src/monitor/mon_parse.y"
                    { resources_load((yyvsp[-1].str)); }
#line 3128 "src/monitor/mon_parse.c"
    break;

  case 154: /* monitor_misc_rules: CMD_SAVE_RESOURCES filename end_cmd  */
#line 585 "src/monitor/mon_parse.y"
                    { resources_save((yyvsp[-1].str)); }
#line 3134 "src/monitor/mon_parse.c"
    break;

  case 155: /* monitor_misc_rules: CMD_MON_RESET end_cmd  */
#line 587 "src/monitor/mon_parse.y"
                    { mon_reset_machine(-1); }
#line 3140 "src/monitor/mon_parse.c"
    break;

  case 156: /* monitor_misc_rules: CMD_MON_RESET opt_sep expression end_cmd  */
#line 589 "src/monitor/mon_parse.y"
                    { mon_reset_machine((yyvsp[-1].i)); }
#line 3146 "src/monitor/mon_parse.c"
    break;

  case 157: /* monitor_misc_rules: CMD_TAPECTRL opt_sep expression end_cmd  */
#line 591 "src/monitor/mon_parse.y"
                    { mon_tape_ctrl(TAPEPORT_PORT_1, (yyvsp[-1].i)); }
#line 3152 "src/monitor/mon_parse.c"
    break;

  case 158: /* monitor_misc_rules: CMD_TAPEOFFS end_cmd  */
#line 593 "src/monitor/mon_parse.y"
                    { mon_tape_offs(TAPEPORT_PORT_1, -1); }
#line 3158 "src/monitor/mon_parse.c"
    break;

  case 159: /* monitor_misc_rules: CMD_TAPEOFFS opt_sep expression end_cmd  */
#line 595 "src/monitor/mon_parse.y"
                    { mon_tape_offs(TAPEPORT_PORT_1, (yyvsp[-1].i)); }
#line 3164 "src/monitor/mon_parse.c"
    break;

  case 160: /* monitor_misc_rules: CMD_CARTFREEZE end_cmd  */
#line 597 "src/monitor/mon_parse.y"
                    { mon_cart_freeze(); }
#line 3170 "src/monitor/mon_parse.c"
    break;

  case 161: /* monitor_misc_rules: CMD_UPDB number end_cmd  */
#line 599 "src/monitor/mon_parse.y"
                    { mon_userport_set_output((yyvsp[-1].i)); }
#line 3176 "src/monitor/mon_parse.c"
    break;

  case 162: /* monitor_misc_rules: CMD_UPDB end_cmd  */
#line 601 "src/monitor/mon_parse.y"
                    { mon_userport_get_output(); }
#line 3182 "src/monitor/mon_parse.c"
    break;

  case 163: /* monitor_misc_rules: CMD_JPDB number number end_cmd  */
#line 603 "src/monitor/mon_parse.y"
                    { mon_joyport_set_output((yyvsp[-2].i), (yyvsp[-1].i)); }
#line 3188 "src/monitor/mon_parse.c"
    break;

  case 164: /* monitor_misc_rules: CMD_JPDB number end_cmd  */
#line 605 "src/monitor/mon_parse.y"
                    { mon_joyport_get_output((yyvsp[-1].i)); }
#line 3194 "src/monitor/mon_parse.c"
    break;

  case 165: /* monitor_misc_rules: CMD_KEYMATRIX end_cmd  */
#line 607 "src/monitor/mon_parse.y"
                    { mon_keymatrix_show(); }
#line 3200 "src/monitor/mon_parse.c"
    break;

  case 166: /* monitor_misc_rules: CMD_KEYMATRIX CLEAR end_cmd  */
#line 609 "src/monitor/mon_parse.y"
                    { mon_keymatrix_clear(); }
#line 3206 "src/monitor/mon_parse.c"
    break;

  case 167: /* monitor_misc_rules: CMD_KEYMATRIX number number end_cmd  */
#line 611 "src/monitor/mon_parse.y"
                    { mon_keymatrix_set((yyvsp[-2].i), (yyvsp[-1].i), e_ON, 0); }
#line 3212 "src/monitor/mon_parse.c"
    break;

  case 168: /* monitor_misc_rules: CMD_KEYMATRIX number number number end_cmd  */
#line 613 "src/monitor/mon_parse.y"
                    { mon_keymatrix_set((yyvsp[-3].i), (yyvsp[-2].i), e_ON, (yyvsp[-1].i)); }
#line 3218 "src/monitor/mon_parse.c"
    break;

  case 169: /* monitor_misc_rules: CMD_KEYMATRIX number number TOGGLE end_cmd  */
#line 615 "src/monitor/mon_parse.y"
                    { mon_keymatrix_set((yyvsp[-3].i), (yyvsp[-2].i), (yyvsp[-1].action), 0); }
#line 3224 "src/monitor/mon_parse.c"
    break;

  case 170: /* monitor_misc_rules: CMD_KEYMATRIX KEY_RESTORE end_cmd  */
#line 617 "src/monitor/mon_parse.y"
                    { mon_keymatrix_restore(e_ON, 0); }
#line 3230 "src/monitor/mon_parse.c"
    break;

  case 171: /* monitor_misc_rules: CMD_KEYMATRIX KEY_RESTORE number end_cmd  */
#line 619 "src/monitor/mon_parse.y"
                    { mon_keymatrix_restore(e_ON, (yyvsp[-1].i)); }
#line 3236 "src/monitor/mon_parse.c"
    break;

  case 172: /* monitor_misc_rules: CMD_KEYMATRIX KEY_RESTORE TOGGLE end_cmd  */
#line 621 "src/monitor/mon_parse.y"
                    { mon_keymatrix_restore((yyvsp[-1].action), 0); }
#line 3242 "src/monitor/mon_parse.c"
    break;

  case 173: /* monitor_misc_rules: CMD_JOYSTICK end_cmd  */
#line 623 "src/monitor/mon_parse.y"
                    { mon_joystick_show(); }
#line 3248 "src/monitor/mon_parse.c"
    break;

  case 174: /* monitor_misc_rules: CMD_JOYSTICK CLEAR end_cmd  */
#line 625 "src/monitor/mon_parse.y"
                    { mon_joystick_clear(); }
#line 3254 "src/monitor/mon_parse.c"
    break;

  case 175: /* monitor_misc_rules: CMD_JOYSTICK number number end_cmd  */
#line 627 "src/monitor/mon_parse.y"
                    { mon_joystick_set((yyvsp[-2].i), (yyvsp[-1].i), 0); }
#line 3260 "src/monitor/mon_parse.c"
    break;

  case 176: /* monitor_misc_rules: CMD_JOYSTICK number number number end_cmd  */
#line 629 "src/monitor/mon_parse.y"
                    { mon_joystick_set((yyvsp[-3].i), (yyvsp[-2].i), (yyvsp[-1].i)); }
#line 3266 "src/monitor/mon_parse.c"
    break;

  case 177: /* monitor_misc_rules: CMD_COMMENT opt_rest_of_line end_cmd  */
#line 631 "src/monitor/mon_parse.y"
                     { }
#line 3272 "src/monitor/mon_parse.c"
    break;

  case 178: /* monitor_misc_rules: CMD_STOPWATCH RESET end_cmd  */
#line 633 "src/monitor/mon_parse.y"
                     { mon_stopwatch_reset(); }
#line 3278 "src/monitor/mon_parse.c"
    break;

  case 179: /* monitor_misc_rules: CMD_STOPWATCH end_cmd  */
#line 635 "src/monitor/mon_parse.y"
                     { mon_stopwatch_show(default_memspace, "Stopwatch: ", "\n"); }
#line 3284 "src/monitor/mon_parse.c"
    break;

  case 180: /* monitor_misc_rules: CMD_PROFILE TOGGLE end_cmd  */
#line 637 "src/monitor/mon_parse.y"
                     { mon_profile_action((yyvsp[-1].action)); }
#line 3290 "src/monitor/mon_parse.c"
    break;

  case 181: /* monitor_misc_rules: CMD_PROFILE end_cmd  */
#line 639 "src/monitor/mon_parse.y"
                     { mon_profile(); }
#line 3296 "src/monitor/mon_parse.c"
    break;

  case 182: /* monitor_misc_rules: CMD_PROFILE FLAT opt_d_number end_cmd  */
#line 641 "src/monitor/mon_parse.y"
                     { mon_profile_flat((yyvsp[-1].i)); }
#line 3302 "src/monitor/mon_parse.c"
    break;

  case 183: /* monitor_misc_rules: CMD_PROFILE GRAPH opt_context_num end_cmd  */
#line 643 "src/monitor/mon_parse.y"
                     { mon_profile_graph((yyvsp[-1].i), -1); }
#line 3308 "src/monitor/mon_parse.c"
    break;

  case 184: /* monitor_misc_rules: CMD_PROFILE GRAPH opt_context_num DEPTH d_number end_cmd  */
#line 645 "src/monitor/mon_parse.y"
                     { mon_profile_graph((yyvsp[-3].i), (yyvsp[-1].i)); }
#line 3314 "src/monitor/mon_parse.c"
    break;

  case 185: /* monitor_misc_rules: CMD_PROFILE FUNC address end_cmd  */
#line 647 "src/monitor/mon_parse.y"
                     { mon_profile_func((yyvsp[-1].a)); }
#line 3320 "src/monitor/mon_parse.c"
    break;

  case 186: /* monitor_misc_rules: CMD_PROFILE DISASS address end_cmd  */
#line 649 "src/monitor/mon_parse.y"
                     { mon_profile_disass((yyvsp[-1].a)); }
#line 3326 "src/monitor/mon_parse.c"
    break;

  case 187: /* monitor_misc_rules: CMD_PROFILE CLEAR address end_cmd  */
#line 651 "src/monitor/mon_parse.y"
                     { mon_profile_clear((yyvsp[-1].a)); }
#line 3332 "src/monitor/mon_parse.c"
    break;

  case 188: /* monitor_misc_rules: CMD_PROFILE PROFILE_CONTEXT d_number end_cmd  */
#line 653 "src/monitor/mon_parse.y"
                     { mon_profile_disass_context((yyvsp[-1].i)); }
#line 3338 "src/monitor/mon_parse.c"
    break;

  case 189: /* disk_rules: CMD_LOAD filename device_num opt_address end_cmd  */
#line 657 "src/monitor/mon_parse.y"
            { mon_file_load((yyvsp[-3].str), (yyvsp[-2].i), (yyvsp[-1].a), FALSE, FALSE); }
#line 3344 "src/monitor/mon_parse.c"
    break;

  case 190: /* disk_rules: CMD_BASICLOAD filename device_num opt_address end_cmd  */
#line 659 "src/monitor/mon_parse.y"
            { mon_file_load((yyvsp[-3].str), (yyvsp[-2].i), (yyvsp[-1].a), FALSE, TRUE); }
#line 3350 "src/monitor/mon_parse.c"
    break;

  case 191: /* disk_rules: CMD_BLOAD filename device_num address end_cmd  */
#line 661 "src/monitor/mon_parse.y"
            { mon_file_load((yyvsp[-3].str), (yyvsp[-2].i), (yyvsp[-1].a), TRUE, FALSE); }
#line 3356 "src/monitor/mon_parse.c"
    break;

  case 192: /* disk_rules: CMD_BLOAD filename device_num error  */
#line 663 "src/monitor/mon_parse.y"
            { return ERR_EXPECT_ADDRESS; }
#line 3362 "src/monitor/mon_parse.c"
    break;

  case 193: /* disk_rules: CMD_SAVE filename device_num address_range end_cmd  */
#line 665 "src/monitor/mon_parse.y"
            { mon_file_save((yyvsp[-3].str), (yyvsp[-2].i), (yyvsp[-1].range)[0], (yyvsp[-1].range)[1], FALSE); }
#line 3368 "src/monitor/mon_parse.c"
    break;

  case 194: /* disk_rules: CMD_SAVE filename error  */
#line 667 "src/monitor/mon_parse.y"
            { return ERR_EXPECT_DEVICE_NUM; }
#line 3374 "src/monitor/mon_parse.c"
    break;

  case 195: /* disk_rules: CMD_SAVE filename device_num error  */
#line 669 "src/monitor/mon_parse.y"
            { return ERR_EXPECT_ADDRESS; }
#line 3380 "src/monitor/mon_parse.c"
    break;

  case 196: /* disk_rules: CMD_BSAVE filename device_num address_range end_cmd  */
#line 671 "src/monitor/mon_parse.y"
            { mon_file_save((yyvsp[-3].str), (yyvsp[-2].i), (yyvsp[-1].range)[0], (yyvsp[-1].range)[1], TRUE); }
#line 3386 "src/monitor/mon_parse.c"
    break;

  case 197: /* disk_rules: CMD_BSAVE filename device_num error  */
#line 673 "src/monitor/mon_parse.y"
            { return ERR_EXPECT_ADDRESS; }
#line 3392 "src/monitor/mon_parse.c"
    break;

  case 198: /* disk_rules: CMD_VERIFY filename device_num opt_address end_cmd  */
#line 675 "src/monitor/mon_parse.y"
            { mon_file_verify((yyvsp[-3].str),(yyvsp[-2].i),(yyvsp[-1].a),FALSE); }
#line 3398 "src/monitor/mon_parse.c"
    break;

  case 199: /* disk_rules: CMD_BVERIFY filename device_num address end_cmd  */
#line 677 "src/monitor/mon_parse.y"
            { mon_file_verify((yyvsp[-3].str),(yyvsp[-2].i),(yyvsp[-1].a),TRUE); }
#line 3404 "src/monitor/mon_parse.c"
    break;

  case 200: /* disk_rules: CMD_BVERIFY filename device_num error  */
#line 679 "src/monitor/mon_parse.y"
            { return ERR_EXPECT_ADDRESS; }
#line 3410 "src/monitor/mon_parse.c"
    break;

  case 201: /* disk_rules: CMD_BLOCK_READ expression expression opt_address end_cmd  */
#line 681 "src/monitor/mon_parse.y"
            { mon_drive_block_cmd(0,(yyvsp[-3].i),(yyvsp[-2].i),(yyvsp[-1].a)); }
#line 3416 "src/monitor/mon_parse.c"
    break;

  case 202: /* disk_rules: CMD_BLOCK_WRITE expression expression address end_cmd  */
#line 683 "src/monitor/mon_parse.y"
            { mon_drive_block_cmd(1,(yyvsp[-3].i),(yyvsp[-2].i),(yyvsp[-1].a)); }
#line 3422 "src/monitor/mon_parse.c"
    break;

  case 203: /* disk_rules: CMD_LIST end_cmd  */
#line 685 "src/monitor/mon_parse.y"
            { mon_drive_list(-1); }
#line 3428 "src/monitor/mon_parse.c"
    break;

  case 204: /* disk_rules: CMD_LIST device_num end_cmd  */
#line 687 "src/monitor/mon_parse.y"
            { mon_drive_list((yyvsp[-1].i)); }
#line 3434 "src/monitor/mon_parse.c"
    break;

  case 205: /* disk_rules: CMD_ATTACH filename expression end_cmd  */
#line 689 "src/monitor/mon_parse.y"
            { mon_attach((yyvsp[-2].str),(yyvsp[-1].i)); }
#line 3440 "src/monitor/mon_parse.c"
    break;

  case 206: /* disk_rules: CMD_DETACH expression end_cmd  */
#line 691 "src/monitor/mon_parse.y"
            { mon_detach((yyvsp[-1].i)); }
#line 3446 "src/monitor/mon_parse.c"
    break;

  case 207: /* disk_rules: CMD_AUTOSTART filename end_cmd  */
#line 693 "src/monitor/mon_parse.y"
            { mon_autostart((yyvsp[-1].str),0,1); }
#line 3452 "src/monitor/mon_parse.c"
    break;

  case 208: /* disk_rules: CMD_AUTOSTART filename opt_sep number end_cmd  */
#line 695 "src/monitor/mon_parse.y"
            { mon_autostart((yyvsp[-3].str),(yyvsp[-1].i),1); }
#line 3458 "src/monitor/mon_parse.c"
    break;

  case 209: /* disk_rules: CMD_AUTOLOAD filename end_cmd  */
#line 697 "src/monitor/mon_parse.y"
            { mon_autostart((yyvsp[-1].str),0,0); }
#line 3464 "src/monitor/mon_parse.c"
    break;

  case 210: /* disk_rules: CMD_AUTOLOAD filename opt_sep number end_cmd  */
#line 699 "src/monitor/mon_parse.y"
            { mon_autostart((yyvsp[-3].str),(yyvsp[-1].i),0); }
#line 3470 "src/monitor/mon_parse.c"
    break;

  case 211: /* cmd_file_rules: CMD_RECORD filename end_cmd  */
#line 703 "src/monitor/mon_parse.y"
                { mon_record_commands((yyvsp[-1].str)); }
#line 3476 "src/monitor/mon_parse.c"
    break;

  case 212: /* cmd_file_rules: CMD_MON_STOP end_cmd  */
#line 705 "src/monitor/mon_parse.y"
                { mon_end_recording(); }
#line 3482 "src/monitor/mon_parse.c"
    break;

  case 213: /* cmd_file_rules: CMD_PLAYBACK filename end_cmd  */
#line 707 "src/monitor/mon_parse.y"
                { mon_playback_commands((yyvsp[-1].str),true); }
#line 3488 "src/monitor/mon_parse.c"
    break;

  case 214: /* data_entry_rules: CMD_ENTER_DATA address data_list end_cmd  */
#line 711 "src/monitor/mon_parse.y"
                  { mon_memory_fill((yyvsp[-2].a), BAD_ADDR, (unsigned char *)(yyvsp[-1].str)); }
#line 3494 "src/monitor/mon_parse.c"
    break;

  case 215: /* data_entry_rules: CMD_ENTER_BIN_DATA end_cmd  */
#line 713 "src/monitor/mon_parse.y"
                  { printf("Not yet.\n"); }
#line 3500 "src/monitor/mon_parse.c"
    break;

  case 216: /* monitor_debug_rules: CMD_YYDEBUG end_cmd  */
#line 717 "src/monitor/mon_parse.y"
                     {
#if YYDEBUG
                     yydebug = 1;
#endif
                     }
#line 3510 "src/monitor/mon_parse.c"
    break;

  case 217: /* rest_of_line: R_O_L  */
#line 724 "src/monitor/mon_parse.y"
                    { (yyval.str) = (yyvsp[0].str); }
#line 3516 "src/monitor/mon_parse.c"
    break;

  case 218: /* opt_rest_of_line: R_O_L  */
#line 727 "src/monitor/mon_parse.y"
                        { (yyval.str) = (yyvsp[0].str); }
#line 3522 "src/monitor/mon_parse.c"
    break;

  case 219: /* opt_rest_of_line: %empty  */
#line 728 "src/monitor/mon_parse.y"
                    { (yyval.str) = NULL; }
#line 3528 "src/monitor/mon_parse.c"
    break;

  case 220: /* rest_of_line_or_quoted: R_O_L_Q  */
#line 731 "src/monitor/mon_parse.y"
                                { (yyval.str) = (yyvsp[0].str); }
#line 3534 "src/monitor/mon_parse.c"
    break;

  case 222: /* filename: error  */
#line 735 "src/monitor/mon_parse.y"
                { return ERR_EXPECT_FILENAME; }
#line 3540 "src/monitor/mon_parse.c"
    break;

  case 224: /* device_num: error  */
#line 739 "src/monitor/mon_parse.y"
              { return ERR_EXPECT_DEVICE_NUM; }
#line 3546 "src/monitor/mon_parse.c"
    break;

  case 225: /* mem_op: mem_op MEM_OP  */
#line 742 "src/monitor/mon_parse.y"
                      { (yyval.i) = (yyvsp[-1].i) | (yyvsp[0].i); }
#line 3552 "src/monitor/mon_parse.c"
    break;

  case 226: /* mem_op: MEM_OP  */
#line 743 "src/monitor/mon_parse.y"
               { (yyval.i) = (yyvsp[0].i); }
#line 3558 "src/monitor/mon_parse.c"
    break;

  case 227: /* opt_mem_op: mem_op  */
#line 746 "src/monitor/mon_parse.y"
                   { (yyval.i) = (yyvsp[0].i); }
#line 3564 "src/monitor/mon_parse.c"
    break;

  case 228: /* opt_mem_op: %empty  */
#line 747 "src/monitor/mon_parse.y"
            { (yyval.i) = 0; }
#line 3570 "src/monitor/mon_parse.c"
    break;

  case 229: /* register: MON_REGISTER  */
#line 750 "src/monitor/mon_parse.y"
                                {
                                    if (!mon_register_valid(default_memspace, (yyvsp[0].reg))) {
                                        return ERR_INVALID_REGISTER;
                                    }
                                    (yyval.i) = new_reg(default_memspace, (yyvsp[0].reg));
                                }
#line 3581 "src/monitor/mon_parse.c"
    break;

  case 230: /* register: memspace MON_REGISTER  */
#line 756 "src/monitor/mon_parse.y"
                                {
                                    if (!mon_register_valid((yyvsp[-1].i), (yyvsp[0].reg))) {
                                        return ERR_INVALID_REGISTER;
                                    }
                                    (yyval.i) = new_reg((yyvsp[-1].i), (yyvsp[0].reg));
                                }
#line 3592 "src/monitor/mon_parse.c"
    break;

  case 233: /* reg_asgn: register EQUALS number  */
#line 769 "src/monitor/mon_parse.y"
          { (monitor_cpu_for_memspace[reg_memspace((yyvsp[-2].i))]->mon_register_set_val)(reg_memspace((yyvsp[-2].i)), reg_regid((yyvsp[-2].i)), (uint16_t) (yyvsp[0].i)); }
#line 3598 "src/monitor/mon_parse.c"
    break;

  case 234: /* checkpt_num: d_number  */
#line 772 "src/monitor/mon_parse.y"
                      { (yyval.i) = (yyvsp[0].i); }
#line 3604 "src/monitor/mon_parse.c"
    break;

  case 235: /* checkpt_num: error  */
#line 773 "src/monitor/mon_parse.y"
                   { return ERR_EXPECT_CHECKNUM; }
#line 3610 "src/monitor/mon_parse.c"
    break;

  case 236: /* opt_context_num: d_number  */
#line 776 "src/monitor/mon_parse.y"
                          { (yyval.i) = (yyvsp[0].i); }
#line 3616 "src/monitor/mon_parse.c"
    break;

  case 237: /* opt_context_num: %empty  */
#line 777 "src/monitor/mon_parse.y"
                 { (yyval.i) = -1; }
#line 3622 "src/monitor/mon_parse.c"
    break;

  case 239: /* address_opt_range: address  */
#line 781 "src/monitor/mon_parse.y"
                           { (yyval.range)[0] = (yyvsp[0].a); (yyval.range)[1] = BAD_ADDR; }
#line 3628 "src/monitor/mon_parse.c"
    break;

  case 240: /* address_range: address opt_sep address  */
#line 784 "src/monitor/mon_parse.y"
                                       { (yyval.range)[0] = (yyvsp[-2].a); (yyval.range)[1] = (yyvsp[0].a); }
#line 3634 "src/monitor/mon_parse.c"
    break;

  case 241: /* address_range: H_RANGE_GUESS  */
#line 786 "src/monitor/mon_parse.y"
               { if (resolve_range(e_default_space, (yyval.range), (yyvsp[0].str))) return ERR_ADDR_TOO_BIG; }
#line 3640 "src/monitor/mon_parse.c"
    break;

  case 242: /* address_range: memspace opt_sep H_RANGE_GUESS  */
#line 788 "src/monitor/mon_parse.y"
               { if (resolve_range((yyvsp[-2].i), (yyval.range), (yyvsp[0].str))) return ERR_ADDR_TOO_BIG; }
#line 3646 "src/monitor/mon_parse.c"
    break;

  case 243: /* opt_address: opt_sep address  */
#line 791 "src/monitor/mon_parse.y"
                             { (yyval.a) = (yyvsp[0].a); }
#line 3652 "src/monitor/mon_parse.c"
    break;

  case 244: /* opt_address: %empty  */
#line 792 "src/monitor/mon_parse.y"
             { (yyval.a) = BAD_ADDR; }
#line 3658 "src/monitor/mon_parse.c"
    break;

  case 245: /* address: memloc  */
#line 796 "src/monitor/mon_parse.y"
         {
             (yyval.a) = new_addr(e_default_space,(yyvsp[0].i));
             if (opt_asm) new_cmd = asm_mode = 1;
         }
#line 3667 "src/monitor/mon_parse.c"
    break;

  case 246: /* address: memspace opt_sep memloc  */
#line 801 "src/monitor/mon_parse.y"
         {
             (yyval.a) = new_addr((yyvsp[-2].i), (yyvsp[0].i));
             if (opt_asm) new_cmd = asm_mode = 1;
         }
#line 3676 "src/monitor/mon_parse.c"
    break;

  case 247: /* address: LABEL  */
#line 806 "src/monitor/mon_parse.y"
         {
             temp = mon_symbol_table_lookup_addr(e_default_space, (yyvsp[0].str));
             if (temp >= 0)
                 (yyval.a) = new_addr(e_default_space, temp);
             else
                 return ERR_UNDEFINED_LABEL;
         }
#line 3688 "src/monitor/mon_parse.c"
    break;

  case 250: /* memspace: MEM_COMP  */
#line 819 "src/monitor/mon_parse.y"
                   { (yyval.i) = e_comp_space; }
#line 3694 "src/monitor/mon_parse.c"
    break;

  case 251: /* memspace: MEM_DISK8  */
#line 820 "src/monitor/mon_parse.y"
                    { (yyval.i) = e_disk8_space; }
#line 3700 "src/monitor/mon_parse.c"
    break;

  case 252: /* memspace: MEM_DISK9  */
#line 821 "src/monitor/mon_parse.y"
                    { (yyval.i) = e_disk9_space; }
#line 3706 "src/monitor/mon_parse.c"
    break;

  case 253: /* memspace: MEM_DISK10  */
#line 822 "src/monitor/mon_parse.y"
                     { (yyval.i) = e_disk10_space; }
#line 3712 "src/monitor/mon_parse.c"
    break;

  case 254: /* memspace: MEM_DISK11  */
#line 823 "src/monitor/mon_parse.y"
                     { (yyval.i) = e_disk11_space; }
#line 3718 "src/monitor/mon_parse.c"
    break;

  case 255: /* memloc: memaddr  */
#line 826 "src/monitor/mon_parse.y"
                { (yyval.i) = (yyvsp[0].i); if (!CHECK_ADDR((yyvsp[0].i))) return ERR_ADDR_TOO_BIG; }
#line 3724 "src/monitor/mon_parse.c"
    break;

  case 256: /* memaddr: number  */
#line 828 "src/monitor/mon_parse.y"
                { (yyval.i) = (yyvsp[0].i); }
#line 3730 "src/monitor/mon_parse.c"
    break;

  case 257: /* expression: expression '+' expression  */
#line 853 "src/monitor/mon_parse.y"
                                      { (yyval.i) = (yyvsp[-2].i) + (yyvsp[0].i); }
#line 3736 "src/monitor/mon_parse.c"
    break;

  case 258: /* expression: expression '-' expression  */
#line 854 "src/monitor/mon_parse.y"
                                      { (yyval.i) = (yyvsp[-2].i) - (yyvsp[0].i); }
#line 3742 "src/monitor/mon_parse.c"
    break;

  case 259: /* expression: expression '*' expression  */
#line 855 "src/monitor/mon_parse.y"
                                      { (yyval.i) = (yyvsp[-2].i) * (yyvsp[0].i); }
#line 3748 "src/monitor/mon_parse.c"
    break;

  case 260: /* expression: expression '/' expression  */
#line 856 "src/monitor/mon_parse.y"
                                      { (yyval.i) = ((yyvsp[0].i)) ? ((yyvsp[-2].i) / (yyvsp[0].i)) : 1; }
#line 3754 "src/monitor/mon_parse.c"
    break;

  case 261: /* expression: '(' expression ')'  */
#line 857 "src/monitor/mon_parse.y"
                               { (yyval.i) = (yyvsp[-1].i); }
#line 3760 "src/monitor/mon_parse.c"
    break;

  case 262: /* expression: '(' expression error  */
#line 858 "src/monitor/mon_parse.y"
                                 { return ERR_MISSING_CLOSE_PAREN; }
#line 3766 "src/monitor/mon_parse.c"
    break;

  case 263: /* expression: value  */
#line 859 "src/monitor/mon_parse.y"
                  { (yyval.i) = (yyvsp[0].i); }
#line 3772 "src/monitor/mon_parse.c"
    break;

  case 264: /* opt_if_cond_expr: IF cond_expr  */
#line 862 "src/monitor/mon_parse.y"
                               { (yyval.cond_node) = (yyvsp[0].cond_node); }
#line 3778 "src/monitor/mon_parse.c"
    break;

  case 265: /* opt_if_cond_expr: %empty  */
#line 863 "src/monitor/mon_parse.y"
                  { (yyval.cond_node) = 0; }
#line 3784 "src/monitor/mon_parse.c"
    break;

  case 266: /* cond_expr: cond_expr COND_OP cond_expr  */
#line 866 "src/monitor/mon_parse.y"
           {
               (yyval.cond_node) = new_cond; (yyval.cond_node)->is_parenthized = FALSE;
               (yyval.cond_node)->child1 = (yyvsp[-2].cond_node); (yyval.cond_node)->child2 = (yyvsp[0].cond_node); (yyval.cond_node)->operation = (yyvsp[-1].cond_op);
           }
#line 3793 "src/monitor/mon_parse.c"
    break;

  case 267: /* cond_expr: cond_expr COND_OP error  */
#line 871 "src/monitor/mon_parse.y"
           { return ERR_INCOMPLETE_COND_OP; }
#line 3799 "src/monitor/mon_parse.c"
    break;

  case 268: /* cond_expr: L_PAREN cond_expr R_PAREN  */
#line 873 "src/monitor/mon_parse.y"
           { (yyval.cond_node) = (yyvsp[-1].cond_node); (yyval.cond_node)->is_parenthized = TRUE; }
#line 3805 "src/monitor/mon_parse.c"
    break;

  case 269: /* cond_expr: L_PAREN cond_expr error  */
#line 875 "src/monitor/mon_parse.y"
           { return ERR_MISSING_CLOSE_PAREN; }
#line 3811 "src/monitor/mon_parse.c"
    break;

  case 270: /* cond_expr: cond_operand  */
#line 877 "src/monitor/mon_parse.y"
           { (yyval.cond_node) = (yyvsp[0].cond_node); }
#line 3817 "src/monitor/mon_parse.c"
    break;

  case 271: /* cond_operand: register  */
#line 880 "src/monitor/mon_parse.y"
                          { (yyval.cond_node) = new_cond;
                            (yyval.cond_node)->operation = e_INV;
                            (yyval.cond_node)->is_parenthized = FALSE;
                            (yyval.cond_node)->reg_num = (yyvsp[0].i); (yyval.cond_node)->is_reg = TRUE; (yyval.cond_node)->banknum=-1;
                            (yyval.cond_node)->child1 = NULL; (yyval.cond_node)->child2 = NULL;
                          }
#line 3828 "src/monitor/mon_parse.c"
    break;

  case 272: /* cond_operand: number  */
#line 886 "src/monitor/mon_parse.y"
                          { (yyval.cond_node) = new_cond;
                            (yyval.cond_node)->operation = e_INV;
                            (yyval.cond_node)->is_parenthized = FALSE;
                            (yyval.cond_node)->value = (yyvsp[0].i); (yyval.cond_node)->is_reg = FALSE; (yyval.cond_node)->banknum=-1;
                            (yyval.cond_node)->child1 = NULL; (yyval.cond_node)->child2 = NULL;
                          }
#line 3839 "src/monitor/mon_parse.c"
    break;

  case 273: /* cond_operand: '@' BANKNAME ':' L_PAREN cond_expr R_PAREN  */
#line 893 "src/monitor/mon_parse.y"
                                                             {
                            (yyval.cond_node) = new_cond;
                            (yyval.cond_node)->operation = e_INV;
                            (yyval.cond_node)->is_parenthized = FALSE;
                            (yyval.cond_node)->banknum = mon_banknum_from_bank(e_default_space, (yyvsp[-4].str));
                            if ((yyval.cond_node)->banknum < 0) {
                                return ERR_ILLEGAL_INPUT;
                            }
                            (yyval.cond_node)->value = 0;
                            (yyval.cond_node)->is_reg = FALSE;
                            (yyval.cond_node)->child1 = (yyvsp[-1].cond_node);
                            (yyval.cond_node)->child2 = NULL;
                        }
#line 3857 "src/monitor/mon_parse.c"
    break;

  case 274: /* cond_operand: '@' BANKNAME ':' address  */
#line 906 "src/monitor/mon_parse.y"
                                           {
                            (yyval.cond_node) = new_cond;
                            (yyval.cond_node)->operation = e_INV;
                            (yyval.cond_node)->is_parenthized = FALSE;
                            (yyval.cond_node)->banknum = mon_banknum_from_bank(e_default_space, (yyvsp[-2].str));
                            if ((yyval.cond_node)->banknum < 0) {
                                return ERR_ILLEGAL_INPUT;
                            }
                            (yyval.cond_node)->value = (yyvsp[0].a);
                            (yyval.cond_node)->is_reg = FALSE;
                            (yyval.cond_node)->child1 = NULL; (yyval.cond_node)->child2 = NULL;  
                        }
#line 3874 "src/monitor/mon_parse.c"
    break;

  case 277: /* data_element: number  */
#line 924 "src/monitor/mon_parse.y"
                     { mon_add_number_to_buffer((yyvsp[0].i)); }
#line 3880 "src/monitor/mon_parse.c"
    break;

  case 278: /* data_element: STRING  */
#line 925 "src/monitor/mon_parse.y"
                     { mon_add_string_to_buffer((yyvsp[0].str)); }
#line 3886 "src/monitor/mon_parse.c"
    break;

  case 281: /* hunt_element: number  */
#line 932 "src/monitor/mon_parse.y"
                     { mon_add_number_to_buffer((yyvsp[0].i)); }
#line 3892 "src/monitor/mon_parse.c"
    break;

  case 282: /* hunt_element: MASK  */
#line 933 "src/monitor/mon_parse.y"
                     { mon_add_number_masked_to_buffer((yyvsp[0].i), 0x00); }
#line 3898 "src/monitor/mon_parse.c"
    break;

  case 283: /* hunt_element: STRING  */
#line 934 "src/monitor/mon_parse.y"
                     { mon_add_string_to_buffer((yyvsp[0].str)); }
#line 3904 "src/monitor/mon_parse.c"
    break;

  case 284: /* value: number  */
#line 937 "src/monitor/mon_parse.y"
              { (yyval.i) = (yyvsp[0].i); }
#line 3910 "src/monitor/mon_parse.c"
    break;

  case 285: /* value: register  */
#line 938 "src/monitor/mon_parse.y"
                { (yyval.i) = (monitor_cpu_for_memspace[reg_memspace((yyvsp[0].i))]->mon_register_get_val)(reg_memspace((yyvsp[0].i)), reg_regid((yyvsp[0].i))); }
#line 3916 "src/monitor/mon_parse.c"
    break;

  case 286: /* d_number: D_NUMBER  */
#line 941 "src/monitor/mon_parse.y"
                   { (yyval.i) = (yyvsp[0].i); }
#line 3922 "src/monitor/mon_parse.c"
    break;

  case 287: /* d_number: B_NUMBER_GUESS  */
#line 942 "src/monitor/mon_parse.y"
                         { (yyval.i) = (int)strtol((yyvsp[0].str), NULL, 10); }
#line 3928 "src/monitor/mon_parse.c"
    break;

  case 288: /* d_number: D_NUMBER_GUESS  */
#line 943 "src/monitor/mon_parse.y"
                         { (yyval.i) = (int)strtol((yyvsp[0].str), NULL, 10); }
#line 3934 "src/monitor/mon_parse.c"
    break;

  case 289: /* d_number: O_NUMBER_GUESS  */
#line 944 "src/monitor/mon_parse.y"
                         { (yyval.i) = (int)strtol((yyvsp[0].str), NULL, 10); }
#line 3940 "src/monitor/mon_parse.c"
    break;

  case 290: /* opt_d_number: d_number  */
#line 947 "src/monitor/mon_parse.y"
                       { (yyval.i) = (yyvsp[0].i); }
#line 3946 "src/monitor/mon_parse.c"
    break;

  case 291: /* opt_d_number: %empty  */
#line 948 "src/monitor/mon_parse.y"
              { (yyval.i) = -1; }
#line 3952 "src/monitor/mon_parse.c"
    break;

  case 292: /* guess_default: B_NUMBER_GUESS  */
#line 951 "src/monitor/mon_parse.y"
                              { (yyval.i) = resolve_datatype(B_NUMBER,(yyvsp[0].str)); }
#line 3958 "src/monitor/mon_parse.c"
    break;

  case 293: /* guess_default: D_NUMBER_GUESS  */
#line 952 "src/monitor/mon_parse.y"
                              { (yyval.i) = resolve_datatype(D_NUMBER,(yyvsp[0].str)); }
#line 3964 "src/monitor/mon_parse.c"
    break;

  case 294: /* guess_default: O_NUMBER_GUESS  */
#line 953 "src/monitor/mon_parse.y"
                              { (yyval.i) = resolve_datatype(O_NUMBER,(yyvsp[0].str)); }
#line 3970 "src/monitor/mon_parse.c"
    break;

  case 295: /* number: H_NUMBER  */
#line 956 "src/monitor/mon_parse.y"
                 { (yyval.i) = (yyvsp[0].i); }
#line 3976 "src/monitor/mon_parse.c"
    break;

  case 296: /* number: B_NUMBER  */
#line 957 "src/monitor/mon_parse.y"
                 { (yyval.i) = (yyvsp[0].i); }
#line 3982 "src/monitor/mon_parse.c"
    break;

  case 297: /* number: D_NUMBER  */
#line 958 "src/monitor/mon_parse.y"
                 { (yyval.i) = (yyvsp[0].i); }
#line 3988 "src/monitor/mon_parse.c"
    break;

  case 298: /* number: O_NUMBER  */
#line 959 "src/monitor/mon_parse.y"
                 { (yyval.i) = (yyvsp[0].i); }
#line 3994 "src/monitor/mon_parse.c"
    break;

  case 299: /* number: guess_default  */
#line 960 "src/monitor/mon_parse.y"
                      { (yyval.i) = (yyvsp[0].i); }
#line 4000 "src/monitor/mon_parse.c"
    break;

  case 303: /* assembly_instruction: OPCODE asm_operand_mode  */
#line 968 "src/monitor/mon_parse.y"
                                              { (yyval.i) = 0;
                                                if ((yyvsp[-1].str)) {
                                                    (monitor_cpu_for_memspace[default_memspace]->mon_assemble_instr)((yyvsp[-1].str), (yyvsp[0].mode));
                                                } else {
                                                    new_cmd = 1;
                                                    asm_mode = 0;
                                                }
                                                opt_asm = 0;
                                              }
#line 4014 "src/monitor/mon_parse.c"
    break;

  case 305: /* post_assemble: assembly_instr_list  */
#line 979 "src/monitor/mon_parse.y"
                                   { asm_mode = 0; }
#line 4020 "src/monitor/mon_parse.c"
    break;

  case 306: /* asm_operand_mode: ARG_IMMEDIATE number  */
#line 982 "src/monitor/mon_parse.y"
                                       { if ((yyvsp[0].i) > 0xff) {
                          (yyval.mode).addr_mode = ASM_ADDR_MODE_IMMEDIATE_16;
                          (yyval.mode).param = (yyvsp[0].i);
                        } else {
                          (yyval.mode).addr_mode = ASM_ADDR_MODE_IMMEDIATE;
                          (yyval.mode).param = (yyvsp[0].i);
                        } }
#line 4032 "src/monitor/mon_parse.c"
    break;

  case 307: /* asm_operand_mode: number  */
#line 989 "src/monitor/mon_parse.y"
           { if ((yyvsp[0].i) >= 0x10000) {
               (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE_LONG;
               (yyval.mode).param = (yyvsp[0].i);
             } else if ((yyvsp[0].i) < 0x100) {
               (yyval.mode).addr_mode = ASM_ADDR_MODE_ZERO_PAGE;
               (yyval.mode).param = (yyvsp[0].i);
             } else {
               (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE;
               (yyval.mode).param = (yyvsp[0].i);
             }
           }
#line 4048 "src/monitor/mon_parse.c"
    break;

  case 308: /* asm_operand_mode: number COMMA REG_X  */
#line 1000 "src/monitor/mon_parse.y"
                        { if ((yyvsp[-2].i) >= 0x10000) {
                            (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE_LONG_X;
                            (yyval.mode).param = (yyvsp[-2].i);
                          } else if ((yyvsp[-2].i) < 0x100) { 
                            (yyval.mode).addr_mode = ASM_ADDR_MODE_ZERO_PAGE_X;
                            (yyval.mode).param = (yyvsp[-2].i);
                          } else {
                            (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE_X;
                            (yyval.mode).param = (yyvsp[-2].i);
                          }
                        }
#line 4064 "src/monitor/mon_parse.c"
    break;

  case 309: /* asm_operand_mode: number COMMA REG_Y  */
#line 1011 "src/monitor/mon_parse.y"
                        { if ((yyvsp[-2].i) < 0x100) {
                            (yyval.mode).addr_mode = ASM_ADDR_MODE_ZERO_PAGE_Y;
                            (yyval.mode).param = (yyvsp[-2].i);
                          } else {
                            (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE_Y;
                            (yyval.mode).param = (yyvsp[-2].i);
                          }
                        }
#line 4077 "src/monitor/mon_parse.c"
    break;

  case 310: /* asm_operand_mode: number COMMA REG_S  */
#line 1019 "src/monitor/mon_parse.y"
                        { if ((yyvsp[-2].i) < 0x100) {
                            (yyval.mode).addr_mode = ASM_ADDR_MODE_STACK_RELATIVE;
                            (yyval.mode).param = (yyvsp[-2].i);
                          } else { /* 6809 */
                            (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
                            if ((yyvsp[-2].i) >= -16 && (yyvsp[-2].i) < 16) {
                                (yyval.mode).addr_submode = (yyvsp[0].i) | ((yyvsp[-2].i) & 0x1F);
                            } else if ((yyvsp[-2].i) >= -128 && (yyvsp[-2].i) < 128) {
                                (yyval.mode).addr_submode = 0x80 | (yyvsp[0].i) | ASM_ADDR_MODE_INDEXED_OFF8;
                                (yyval.mode).param = (yyvsp[-2].i);
                            } else if ((yyvsp[-2].i) >= -32768 && (yyvsp[-2].i) < 32768) {
                                (yyval.mode).addr_submode = 0x80 | (yyvsp[0].i) | ASM_ADDR_MODE_INDEXED_OFF16;
                                (yyval.mode).param = (yyvsp[-2].i);
                            } else {
                                (yyval.mode).addr_mode = ASM_ADDR_MODE_ILLEGAL;
                                mon_out("offset too large even for 16 bits (signed)\n");
                            }
                          }
                        }
#line 4101 "src/monitor/mon_parse.c"
    break;

  case 311: /* asm_operand_mode: number COMMA number  */
#line 1038 "src/monitor/mon_parse.y"
                        { if ((yyvsp[-2].i) < 0x100) {
                            (yyval.mode).addr_mode = ASM_ADDR_MODE_DOUBLE;
                            (yyval.mode).param = (yyvsp[0].i);
                            (yyval.mode).addr_submode = (yyvsp[-2].i);
                          }
                        }
#line 4112 "src/monitor/mon_parse.c"
    break;

  case 312: /* asm_operand_mode: L_PAREN number R_PAREN  */
#line 1044 "src/monitor/mon_parse.y"
                           { if ((yyvsp[-1].i) < 0x100) {
                               (yyval.mode).addr_mode = ASM_ADDR_MODE_INDIRECT;
                               (yyval.mode).param = (yyvsp[-1].i);
                             } else {
                               (yyval.mode).addr_mode = ASM_ADDR_MODE_ABS_INDIRECT;
                               (yyval.mode).param = (yyvsp[-1].i);
                             }
                           }
#line 4125 "src/monitor/mon_parse.c"
    break;

  case 313: /* asm_operand_mode: L_PAREN number COMMA REG_X R_PAREN  */
#line 1052 "src/monitor/mon_parse.y"
                                       { if ((yyvsp[-3].i) < 0x100) {
                                           (yyval.mode).addr_mode = ASM_ADDR_MODE_INDIRECT_X;
                                           (yyval.mode).param = (yyvsp[-3].i);
                                         } else {
                                           (yyval.mode).addr_mode = ASM_ADDR_MODE_ABS_INDIRECT_X;
                                           (yyval.mode).param = (yyvsp[-3].i);
                                         }
                                       }
#line 4138 "src/monitor/mon_parse.c"
    break;

  case 314: /* asm_operand_mode: L_PAREN number COMMA REG_S R_PAREN COMMA REG_Y  */
#line 1061 "src/monitor/mon_parse.y"
    { (yyval.mode).addr_mode = ASM_ADDR_MODE_STACK_RELATIVE_Y; (yyval.mode).param = (yyvsp[-5].i); }
#line 4144 "src/monitor/mon_parse.c"
    break;

  case 315: /* asm_operand_mode: L_PAREN number R_PAREN COMMA REG_Y  */
#line 1063 "src/monitor/mon_parse.y"
    { (yyval.mode).addr_mode = ASM_ADDR_MODE_INDIRECT_Y; (yyval.mode).param = (yyvsp[-3].i); }
#line 4150 "src/monitor/mon_parse.c"
    break;

  case 316: /* asm_operand_mode: L_PAREN REG_BC R_PAREN  */
#line 1064 "src/monitor/mon_parse.y"
                           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IND_BC; }
#line 4156 "src/monitor/mon_parse.c"
    break;

  case 317: /* asm_operand_mode: L_PAREN REG_DE R_PAREN  */
#line 1065 "src/monitor/mon_parse.y"
                           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IND_DE; }
#line 4162 "src/monitor/mon_parse.c"
    break;

  case 318: /* asm_operand_mode: L_PAREN REG_HL R_PAREN  */
#line 1066 "src/monitor/mon_parse.y"
                           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IND_HL; }
#line 4168 "src/monitor/mon_parse.c"
    break;

  case 319: /* asm_operand_mode: L_PAREN REG_IX R_PAREN  */
#line 1067 "src/monitor/mon_parse.y"
                           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IND_IX; }
#line 4174 "src/monitor/mon_parse.c"
    break;

  case 320: /* asm_operand_mode: L_PAREN REG_IY R_PAREN  */
#line 1068 "src/monitor/mon_parse.y"
                           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IND_IY; }
#line 4180 "src/monitor/mon_parse.c"
    break;

  case 321: /* asm_operand_mode: L_PAREN REG_SP R_PAREN  */
#line 1069 "src/monitor/mon_parse.y"
                           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IND_SP; }
#line 4186 "src/monitor/mon_parse.c"
    break;

  case 322: /* asm_operand_mode: L_PAREN number R_PAREN COMMA REG_A  */
#line 1071 "src/monitor/mon_parse.y"
    { (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE_A; (yyval.mode).param = (yyvsp[-3].i); }
#line 4192 "src/monitor/mon_parse.c"
    break;

  case 323: /* asm_operand_mode: L_PAREN number R_PAREN COMMA REG_HL  */
#line 1073 "src/monitor/mon_parse.y"
    { (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE_HL; (yyval.mode).param = (yyvsp[-3].i); }
#line 4198 "src/monitor/mon_parse.c"
    break;

  case 324: /* asm_operand_mode: L_PAREN number R_PAREN COMMA REG_IX  */
#line 1075 "src/monitor/mon_parse.y"
    { (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE_IX; (yyval.mode).param = (yyvsp[-3].i); }
#line 4204 "src/monitor/mon_parse.c"
    break;

  case 325: /* asm_operand_mode: L_PAREN number R_PAREN COMMA REG_IY  */
#line 1077 "src/monitor/mon_parse.y"
    { (yyval.mode).addr_mode = ASM_ADDR_MODE_ABSOLUTE_IY; (yyval.mode).param = (yyvsp[-3].i); }
#line 4210 "src/monitor/mon_parse.c"
    break;

  case 326: /* asm_operand_mode: %empty  */
#line 1078 "src/monitor/mon_parse.y"
    { (yyval.mode).addr_mode = ASM_ADDR_MODE_IMPLIED; }
#line 4216 "src/monitor/mon_parse.c"
    break;

  case 327: /* asm_operand_mode: REG_A  */
#line 1079 "src/monitor/mon_parse.y"
          { (yyval.mode).addr_mode = ASM_ADDR_MODE_ACCUMULATOR; }
#line 4222 "src/monitor/mon_parse.c"
    break;

  case 328: /* asm_operand_mode: REG_B  */
#line 1080 "src/monitor/mon_parse.y"
          { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_B; }
#line 4228 "src/monitor/mon_parse.c"
    break;

  case 329: /* asm_operand_mode: REG_C  */
#line 1081 "src/monitor/mon_parse.y"
          { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_C; }
#line 4234 "src/monitor/mon_parse.c"
    break;

  case 330: /* asm_operand_mode: REG_D  */
#line 1082 "src/monitor/mon_parse.y"
          { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_D; }
#line 4240 "src/monitor/mon_parse.c"
    break;

  case 331: /* asm_operand_mode: REG_E  */
#line 1083 "src/monitor/mon_parse.y"
          { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_E; }
#line 4246 "src/monitor/mon_parse.c"
    break;

  case 332: /* asm_operand_mode: REG_H  */
#line 1084 "src/monitor/mon_parse.y"
          { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_H; }
#line 4252 "src/monitor/mon_parse.c"
    break;

  case 333: /* asm_operand_mode: REG_IXH  */
#line 1085 "src/monitor/mon_parse.y"
            { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IXH; }
#line 4258 "src/monitor/mon_parse.c"
    break;

  case 334: /* asm_operand_mode: REG_IYH  */
#line 1086 "src/monitor/mon_parse.y"
            { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IYH; }
#line 4264 "src/monitor/mon_parse.c"
    break;

  case 335: /* asm_operand_mode: REG_L  */
#line 1087 "src/monitor/mon_parse.y"
          { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_L; }
#line 4270 "src/monitor/mon_parse.c"
    break;

  case 336: /* asm_operand_mode: REG_IXL  */
#line 1088 "src/monitor/mon_parse.y"
            { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IXL; }
#line 4276 "src/monitor/mon_parse.c"
    break;

  case 337: /* asm_operand_mode: REG_IYL  */
#line 1089 "src/monitor/mon_parse.y"
            { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IYL; }
#line 4282 "src/monitor/mon_parse.c"
    break;

  case 338: /* asm_operand_mode: REG_AF  */
#line 1090 "src/monitor/mon_parse.y"
           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_AF; }
#line 4288 "src/monitor/mon_parse.c"
    break;

  case 339: /* asm_operand_mode: REG_BC  */
#line 1091 "src/monitor/mon_parse.y"
           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_BC; }
#line 4294 "src/monitor/mon_parse.c"
    break;

  case 340: /* asm_operand_mode: REG_DE  */
#line 1092 "src/monitor/mon_parse.y"
           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_DE; }
#line 4300 "src/monitor/mon_parse.c"
    break;

  case 341: /* asm_operand_mode: REG_HL  */
#line 1093 "src/monitor/mon_parse.y"
           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_HL; }
#line 4306 "src/monitor/mon_parse.c"
    break;

  case 342: /* asm_operand_mode: REG_IX  */
#line 1094 "src/monitor/mon_parse.y"
           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IX; }
#line 4312 "src/monitor/mon_parse.c"
    break;

  case 343: /* asm_operand_mode: REG_IY  */
#line 1095 "src/monitor/mon_parse.y"
           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_IY; }
#line 4318 "src/monitor/mon_parse.c"
    break;

  case 344: /* asm_operand_mode: REG_SP  */
#line 1096 "src/monitor/mon_parse.y"
           { (yyval.mode).addr_mode = ASM_ADDR_MODE_REG_SP; }
#line 4324 "src/monitor/mon_parse.c"
    break;

  case 345: /* asm_operand_mode: LESS_THAN number  */
#line 1098 "src/monitor/mon_parse.y"
                     { (yyval.mode).addr_mode = ASM_ADDR_MODE_DIRECT; (yyval.mode).param = (yyvsp[0].i); }
#line 4330 "src/monitor/mon_parse.c"
    break;

  case 346: /* asm_operand_mode: number COMMA index_ureg  */
#line 1099 "src/monitor/mon_parse.y"
                            {    /* Clash with addr,x addr,y addr,s modes! */
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        if ((yyvsp[-2].i) >= -16 && (yyvsp[-2].i) < 16) {
            (yyval.mode).addr_submode = (3 << 5) | ((yyvsp[-2].i) & 0x1F);
        } else if ((yyvsp[-2].i) >= -128 && (yyvsp[-2].i) < 128) {
            (yyval.mode).addr_submode = 0x80 | (3 << 5) | ASM_ADDR_MODE_INDEXED_OFF8;
            (yyval.mode).param = (yyvsp[-2].i);
        } else if ((yyvsp[-2].i) >= -32768 && (yyvsp[-2].i) < 32768) {
            (yyval.mode).addr_submode = 0x80 | (3 << 5) | ASM_ADDR_MODE_INDEXED_OFF16;
            (yyval.mode).param = (yyvsp[-2].i);
        } else {
            (yyval.mode).addr_mode = ASM_ADDR_MODE_ILLEGAL;
            mon_out("offset too large even for 16 bits (signed)\n");
        }
    }
#line 4350 "src/monitor/mon_parse.c"
    break;

  case 347: /* asm_operand_mode: COMMA index_reg PLUS  */
#line 1114 "src/monitor/mon_parse.y"
                         {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-1].i) | ASM_ADDR_MODE_INDEXED_INC1;
        }
#line 4359 "src/monitor/mon_parse.c"
    break;

  case 348: /* asm_operand_mode: COMMA index_reg PLUS PLUS  */
#line 1118 "src/monitor/mon_parse.y"
                              {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-2].i) | ASM_ADDR_MODE_INDEXED_INC2;
        }
#line 4368 "src/monitor/mon_parse.c"
    break;

  case 349: /* asm_operand_mode: COMMA MINUS index_reg  */
#line 1122 "src/monitor/mon_parse.y"
                          {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[0].i) | ASM_ADDR_MODE_INDEXED_DEC1;
        }
#line 4377 "src/monitor/mon_parse.c"
    break;

  case 350: /* asm_operand_mode: COMMA MINUS MINUS index_reg  */
#line 1126 "src/monitor/mon_parse.y"
                                {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[0].i) | ASM_ADDR_MODE_INDEXED_DEC2;
        }
#line 4386 "src/monitor/mon_parse.c"
    break;

  case 351: /* asm_operand_mode: COMMA index_reg  */
#line 1130 "src/monitor/mon_parse.y"
                    {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[0].i) | ASM_ADDR_MODE_INDEXED_OFF0;
        }
#line 4395 "src/monitor/mon_parse.c"
    break;

  case 352: /* asm_operand_mode: REG_B COMMA index_reg  */
#line 1134 "src/monitor/mon_parse.y"
                          {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-1].i) | ASM_ADDR_MODE_INDEXED_OFFB;
        }
#line 4404 "src/monitor/mon_parse.c"
    break;

  case 353: /* asm_operand_mode: REG_A COMMA index_reg  */
#line 1138 "src/monitor/mon_parse.y"
                          {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-1].i) | ASM_ADDR_MODE_INDEXED_OFFA;
        }
#line 4413 "src/monitor/mon_parse.c"
    break;

  case 354: /* asm_operand_mode: REG_D COMMA index_reg  */
#line 1142 "src/monitor/mon_parse.y"
                          {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-1].i) | ASM_ADDR_MODE_INDEXED_OFFD;
        }
#line 4422 "src/monitor/mon_parse.c"
    break;

  case 355: /* asm_operand_mode: number COMMA REG_PC  */
#line 1146 "src/monitor/mon_parse.y"
                        {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).param = (yyvsp[-2].i);
        if ((yyvsp[-2].i) >= -128 && (yyvsp[-2].i) < 128) {
            (yyval.mode).addr_submode = ASM_ADDR_MODE_INDEXED_OFFPC8;
        } else if ((yyvsp[-2].i) >= -32768 && (yyvsp[-2].i) < 32768) {
            (yyval.mode).addr_submode = ASM_ADDR_MODE_INDEXED_OFFPC16;
        } else {
            (yyval.mode).addr_mode = ASM_ADDR_MODE_ILLEGAL;
            mon_out("offset too large even for 16 bits (signed)\n");
        }
    }
#line 4439 "src/monitor/mon_parse.c"
    break;

  case 356: /* asm_operand_mode: L_BRACKET number COMMA index_reg R_BRACKET  */
#line 1158 "src/monitor/mon_parse.y"
                                               {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        if ((yyvsp[-3].i) >= -16 && (yyvsp[-3].i) < 16) {
            (yyval.mode).addr_submode = (yyvsp[-3].i) & 0x1F;
        } else if ((yyvsp[-4].i) >= -128 && (yyvsp[-4].i) < 128) {
            (yyval.mode).addr_submode = ASM_ADDR_MODE_INDEXED_OFF8;
            (yyval.mode).param = (yyvsp[-3].i);
        } else if ((yyvsp[-3].i) >= -32768 && (yyvsp[-3].i) < 32768) {
            (yyval.mode).addr_submode = ASM_ADDR_MODE_INDEXED_OFF16;
            (yyval.mode).param = (yyvsp[-3].i);
        } else {
            (yyval.mode).addr_mode = ASM_ADDR_MODE_ILLEGAL;
            mon_out("offset too large even for 16 bits (signed)\n");
        }
    }
#line 4459 "src/monitor/mon_parse.c"
    break;

  case 357: /* asm_operand_mode: L_BRACKET COMMA index_reg PLUS R_BRACKET  */
#line 1173 "src/monitor/mon_parse.y"
                                             {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-2].i) | ASM_ADDR_MODE_INDEXED_INC1;
        }
#line 4468 "src/monitor/mon_parse.c"
    break;

  case 358: /* asm_operand_mode: L_BRACKET COMMA index_reg PLUS PLUS R_BRACKET  */
#line 1177 "src/monitor/mon_parse.y"
                                                  {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-3].i) | ASM_ADDR_MODE_INDEXED_INC2;
        }
#line 4477 "src/monitor/mon_parse.c"
    break;

  case 359: /* asm_operand_mode: L_BRACKET COMMA MINUS index_reg R_BRACKET  */
#line 1181 "src/monitor/mon_parse.y"
                                              {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-1].i) | ASM_ADDR_MODE_INDEXED_DEC1;
        }
#line 4486 "src/monitor/mon_parse.c"
    break;

  case 360: /* asm_operand_mode: L_BRACKET COMMA MINUS MINUS index_reg R_BRACKET  */
#line 1185 "src/monitor/mon_parse.y"
                                                    {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-1].i) | ASM_ADDR_MODE_INDEXED_DEC2;
        }
#line 4495 "src/monitor/mon_parse.c"
    break;

  case 361: /* asm_operand_mode: L_BRACKET COMMA index_reg R_BRACKET  */
#line 1189 "src/monitor/mon_parse.y"
                                        {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-1].i) | ASM_ADDR_MODE_INDEXED_OFF0;
        }
#line 4504 "src/monitor/mon_parse.c"
    break;

  case 362: /* asm_operand_mode: L_BRACKET REG_B COMMA index_reg R_BRACKET  */
#line 1193 "src/monitor/mon_parse.y"
                                              {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-2].i) | ASM_ADDR_MODE_INDEXED_OFFB;
        }
#line 4513 "src/monitor/mon_parse.c"
    break;

  case 363: /* asm_operand_mode: L_BRACKET REG_A COMMA index_reg R_BRACKET  */
#line 1197 "src/monitor/mon_parse.y"
                                              {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-2].i) | ASM_ADDR_MODE_INDEXED_OFFA;
        }
#line 4522 "src/monitor/mon_parse.c"
    break;

  case 364: /* asm_operand_mode: L_BRACKET REG_D COMMA index_reg R_BRACKET  */
#line 1201 "src/monitor/mon_parse.y"
                                              {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | (yyvsp[-2].i) | ASM_ADDR_MODE_INDEXED_OFFD;
        }
#line 4531 "src/monitor/mon_parse.c"
    break;

  case 365: /* asm_operand_mode: L_BRACKET number COMMA REG_PC R_BRACKET  */
#line 1205 "src/monitor/mon_parse.y"
                                            {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).param = (yyvsp[-3].i);
        if ((yyvsp[-3].i) >= -128 && (yyvsp[-3].i) < 128) {
            (yyval.mode).addr_submode = ASM_ADDR_MODE_INDEXED_OFFPC8_IND;
        } else if ((yyvsp[-3].i) >= -32768 && (yyvsp[-3].i) < 32768) {
            (yyval.mode).addr_submode = ASM_ADDR_MODE_INDEXED_OFFPC16_IND;
        } else {
            (yyval.mode).addr_mode = ASM_ADDR_MODE_ILLEGAL;
            mon_out("offset too large even for 16 bits (signed)\n");
        }
    }
#line 4548 "src/monitor/mon_parse.c"
    break;

  case 366: /* asm_operand_mode: L_BRACKET number R_BRACKET  */
#line 1217 "src/monitor/mon_parse.y"
                               {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDEXED;
        (yyval.mode).addr_submode = 0x80 | ASM_ADDR_MODE_EXTENDED_INDIRECT;
        (yyval.mode).param = (yyvsp[-1].i);
        }
#line 4558 "src/monitor/mon_parse.c"
    break;

  case 367: /* asm_operand_mode: L_BRACKET number R_BRACKET COMMA REG_Y  */
#line 1222 "src/monitor/mon_parse.y"
                                           {
        (yyval.mode).addr_mode = ASM_ADDR_MODE_INDIRECT_LONG_Y;
        (yyval.mode).param = (yyvsp[-3].i);
        }
#line 4567 "src/monitor/mon_parse.c"
    break;

  case 368: /* index_reg: REG_X  */
#line 1230 "src/monitor/mon_parse.y"
          { (yyval.i) = (0 << 5); printf("reg_x\n"); }
#line 4573 "src/monitor/mon_parse.c"
    break;

  case 369: /* index_reg: REG_Y  */
#line 1231 "src/monitor/mon_parse.y"
          { (yyval.i) = (1 << 5); printf("reg_y\n"); }
#line 4579 "src/monitor/mon_parse.c"
    break;

  case 370: /* index_reg: index_ureg  */
#line 1232 "src/monitor/mon_parse.y"
               { (yyval.i) = (yyvsp[0].i); }
#line 4585 "src/monitor/mon_parse.c"
    break;

  case 371: /* index_reg: REG_S  */
#line 1233 "src/monitor/mon_parse.y"
          { (yyval.i) = (3 << 5); printf("reg_s\n"); }
#line 4591 "src/monitor/mon_parse.c"
    break;

  case 372: /* index_ureg: REG_U  */
#line 1237 "src/monitor/mon_parse.y"
          { (yyval.i) = (2 << 5); printf("reg_u\n"); }
#line 4597 "src/monitor/mon_parse.c"
    break;


#line 4601 "src/monitor/mon_parse.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 1241 "src/monitor/mon_parse.y"


int parse_and_execute_line(char *input)
{
   char *temp_buf;
   int i, rc;

   if (default_memspace == e_comp_space) {
       /*
        * If the command is to be executed when the default address space is the main cpu,
        * Ensure drive CPU emulation is up to date with main cpu CLOCK.
        */
       /* FIXME: should this be drive_catch_up_hook() instead? */
       /*drive_cpu_execute_all(maincpu_clk);*/
       drive_catch_up_hook(maincpu_clk);
   }

   temp_buf = lib_malloc(strlen(input) + 3);
   strcpy(temp_buf,input);
   i = (int)strlen(input);
   temp_buf[i++] = '\n';
   temp_buf[i++] = '\0';
   temp_buf[i++] = '\0';

   make_buffer(temp_buf);
   mon_clear_buffer();
   if ((rc = yyparse()) != 0) {
       mon_out("ERROR -- ");
       switch(rc) {
         case ERR_BAD_CMD:
           mon_out("Bad command:\n");
           break;
         case ERR_RANGE_BAD_START:
           mon_out("Bad first address in range:\n");
           break;
         case ERR_RANGE_BAD_END:
           mon_out("Bad second address in range:\n");
           break;
         case ERR_EXPECT_CHECKNUM:
           mon_out("Checkpoint number expected:\n");
           break;
         case ERR_EXPECT_END_CMD:
           mon_out("Unexpected token:\n");
           break;
         case ERR_MISSING_CLOSE_PAREN:
           mon_out("')' expected:\n");
           break;
         case ERR_INCOMPLETE_COND_OP:
           mon_out("Conditional operation missing an operand:\n");
           break;
         case ERR_EXPECT_FILENAME:
           mon_out("Expecting a filename:\n");
           break;
         case ERR_ADDR_TOO_BIG:
           mon_out("Address too large:\n");
           break;
         case ERR_IMM_TOO_BIG:
           mon_out("Immediate argument too large:\n");
           break;
         case ERR_EXPECT_STRING:
           mon_out("Expecting a string.\n");
           break;
         case ERR_UNDEFINED_LABEL:
           mon_out("Found an undefined label.\n");
           break;
         case ERR_EXPECT_DEVICE_NUM:
           mon_out("Expecting a device number.\n");
           break;
         case ERR_EXPECT_ADDRESS:
           mon_out("Expecting an address.\n");
           break;
         case ERR_INVALID_REGISTER:
           mon_out("Invalid register.\n");
           break;
         case ERR_ILLEGAL_INPUT:
         default:
           mon_out("Wrong syntax:\n");
       }
       mon_out("  %s\n", input);
       for (i = 0; i < last_len; i++) {
           mon_out(" ");
       }
       mon_out("  ^\n");
       asm_mode = 0;
       new_cmd = 1;
   }
   lib_free(temp_buf);
   free_buffer();

   return rc;
}

void set_yydebug(int val)
{
#if YYDEBUG
    yydebug = val;
#else
#endif
}

static int yyerror(char *s)
{
#if 0
   fprintf(stderr, "ERR:%s\n", s);
#endif
   return 0;
}

/* convert the string in "num" to a numeric value, depending on radix. this 
   function does some magic conversion on addresses when radix is not hex.
*/
static int resolve_datatype(unsigned guess_type, const char *num)
{
    int binary = 1, hex = 0, octal = 0, decimal = 1;
    const char *c;

    /* FIXME: Handle cases when default type is non-numerical */
    if (default_radix == e_hexadecimal) {
        return (int)strtol(num, NULL, 16);
    }

    /* we do some educated guessing on what type of number we have here */
    if (num[0] == '0') {
        /* might be octal with leading zero */
        octal = 1;
    }
    /* a string containing any digits not 0 or 1 can't be a binary number */
    c = num;
    while (*c) {
        if ((*c != '0') && (*c != '1')) {
            binary = 0;
            break;
        }
        c++;
    }
    /* a string containing 8 or 9 can't be an octal number */
    c = num;
    while (*c) {
        if ((*c == '8') && (*c == '9')) {
            octal = 0;
            break;
        }
        c++;
    }
    /* a string containing any of A-F can only be a hex number */
    c = num;
    while (*c) {
        if ((tolower((unsigned char)*c) >= 'a') && (tolower((unsigned char)*c) <= 'f')) {
            hex = 1;
            octal = 0;
            binary = 0;
            decimal = 0;
            break;
        }
        c++;
    }

    /* a hex number can only be hex no matter what */
    if (hex) {
        return (int)strtol(num, NULL, 16);
    }

    /* first, if default radix and detected number matches, just do it */
    if (binary && (default_radix == e_binary)) {
        return (int)strtol(num, NULL, 2);
    }
    if (decimal && (default_radix == e_decimal)) {
        return (int)strtol(num, NULL, 10);
    }
    if (octal && (default_radix == e_octal)) {
        return (int)strtol(num, NULL, 8);
    }

    /* second, if detected number matches guess type, do it */
    if (binary && (guess_type == B_NUMBER)) {
        return (int)strtol(num, NULL, 2);
    }
    if (decimal && (guess_type == D_NUMBER)) {
        return (int)strtol(num, NULL, 10);
    }
    if (octal && (guess_type == O_NUMBER)) {
        return (int)strtol(num, NULL, 8);
    }

    /* third only use the detected type */
    if (binary) {
        return (int)strtol(num, NULL, 2);
    }
    if (decimal) {
        return (int)strtol(num, NULL, 10);
    }
    if (octal) {
        return (int)strtol(num, NULL, 8);
    }

    /* use hex as default, should we ever come here */
    return (int)strtol(num, NULL, 16);
}

/*
 * Resolve a character sequence containing 8 hex digits like "08001000".
 * This could be a lazy version of "0800 1000". If the default radix is not
 * hexadecimal, we handle it like a ordinary number, in the latter case there
 * is only one number in the range.
 */
static int resolve_range(enum t_memspace memspace, MON_ADDR range[2],
                         const char *num)
{
    char start[5];
    char end[5];
    long sa;

    range[1] = BAD_ADDR;

    switch (default_radix)
    {
    case e_hexadecimal:
        /* checked twice, but as the code must have exactly 8 digits: */
        if (strlen(num) == 8) {
            memcpy(start, num, 4);
            start[4] = '\0';
            memcpy(end, num + 4, 4);
            end[4] = '\0';
            sa = strtol(start, NULL, 16);
            range[1] = (int)new_addr(memspace, strtol(end, NULL, 16));
        }
        else
            sa = strtol(num, NULL, 16);
        break;

    case e_decimal:
       sa = strtol(num, NULL, 10);
       break;

    case e_octal:
       sa = strtol(num, NULL, 8);
       break;

    default:
       sa = strtol(num, NULL, 2);
    }

    if (!CHECK_ADDR(sa))
        return ERR_ADDR_TOO_BIG;

    range[0] = (int)new_addr(memspace, sa);
    return 0;
}
