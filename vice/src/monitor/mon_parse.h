/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_SRC_MONITOR_MON_PARSE_H_INCLUDED
# define YY_YY_SRC_MONITOR_MON_PARSE_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    H_NUMBER = 258,                /* H_NUMBER  */
    D_NUMBER = 259,                /* D_NUMBER  */
    O_NUMBER = 260,                /* O_NUMBER  */
    B_NUMBER = 261,                /* B_NUMBER  */
    CONVERT_OP = 262,              /* CONVERT_OP  */
    B_DATA = 263,                  /* B_DATA  */
    H_RANGE_GUESS = 264,           /* H_RANGE_GUESS  */
    D_NUMBER_GUESS = 265,          /* D_NUMBER_GUESS  */
    O_NUMBER_GUESS = 266,          /* O_NUMBER_GUESS  */
    B_NUMBER_GUESS = 267,          /* B_NUMBER_GUESS  */
    BAD_CMD = 268,                 /* BAD_CMD  */
    MEM_OP = 269,                  /* MEM_OP  */
    IF = 270,                      /* IF  */
    MEM_COMP = 271,                /* MEM_COMP  */
    MEM_DISK8 = 272,               /* MEM_DISK8  */
    MEM_DISK9 = 273,               /* MEM_DISK9  */
    MEM_DISK10 = 274,              /* MEM_DISK10  */
    MEM_DISK11 = 275,              /* MEM_DISK11  */
    EQUALS = 276,                  /* EQUALS  */
    TRAIL = 277,                   /* TRAIL  */
    CMD_SEP = 278,                 /* CMD_SEP  */
    LABEL_ASGN_COMMENT = 279,      /* LABEL_ASGN_COMMENT  */
    CMD_LOG = 280,                 /* CMD_LOG  */
    CMD_LOGNAME = 281,             /* CMD_LOGNAME  */
    CMD_SIDEFX = 282,              /* CMD_SIDEFX  */
    CMD_DUMMY = 283,               /* CMD_DUMMY  */
    CMD_RETURN = 284,              /* CMD_RETURN  */
    CMD_BLOCK_READ = 285,          /* CMD_BLOCK_READ  */
    CMD_BLOCK_WRITE = 286,         /* CMD_BLOCK_WRITE  */
    CMD_UP = 287,                  /* CMD_UP  */
    CMD_DOWN = 288,                /* CMD_DOWN  */
    CMD_LOAD = 289,                /* CMD_LOAD  */
    CMD_BASICLOAD = 290,           /* CMD_BASICLOAD  */
    CMD_SAVE = 291,                /* CMD_SAVE  */
    CMD_VERIFY = 292,              /* CMD_VERIFY  */
    CMD_BVERIFY = 293,             /* CMD_BVERIFY  */
    CMD_IGNORE = 294,              /* CMD_IGNORE  */
    CMD_HUNT = 295,                /* CMD_HUNT  */
    CMD_FILL = 296,                /* CMD_FILL  */
    CMD_MOVE = 297,                /* CMD_MOVE  */
    CMD_GOTO = 298,                /* CMD_GOTO  */
    CMD_REGISTERS = 299,           /* CMD_REGISTERS  */
    CMD_READSPACE = 300,           /* CMD_READSPACE  */
    CMD_WRITESPACE = 301,          /* CMD_WRITESPACE  */
    CMD_RADIX = 302,               /* CMD_RADIX  */
    CMD_MEM_DISPLAY = 303,         /* CMD_MEM_DISPLAY  */
    CMD_BREAK = 304,               /* CMD_BREAK  */
    CMD_TRACE = 305,               /* CMD_TRACE  */
    CMD_IO = 306,                  /* CMD_IO  */
    CMD_BRMON = 307,               /* CMD_BRMON  */
    CMD_COMPARE = 308,             /* CMD_COMPARE  */
    CMD_DUMP = 309,                /* CMD_DUMP  */
    CMD_UNDUMP = 310,              /* CMD_UNDUMP  */
    CMD_EXIT = 311,                /* CMD_EXIT  */
    CMD_DELETE = 312,              /* CMD_DELETE  */
    CMD_CONDITION = 313,           /* CMD_CONDITION  */
    CMD_COMMAND = 314,             /* CMD_COMMAND  */
    CMD_ASSEMBLE = 315,            /* CMD_ASSEMBLE  */
    CMD_DISASSEMBLE = 316,         /* CMD_DISASSEMBLE  */
    CMD_NEXT = 317,                /* CMD_NEXT  */
    CMD_STEP = 318,                /* CMD_STEP  */
    CMD_PRINT = 319,               /* CMD_PRINT  */
    CMD_DEVICE = 320,              /* CMD_DEVICE  */
    CMD_HELP = 321,                /* CMD_HELP  */
    CMD_WATCH = 322,               /* CMD_WATCH  */
    CMD_DISK = 323,                /* CMD_DISK  */
    CMD_QUIT = 324,                /* CMD_QUIT  */
    CMD_CHDIR = 325,               /* CMD_CHDIR  */
    CMD_BANK = 326,                /* CMD_BANK  */
    CMD_LOAD_LABELS = 327,         /* CMD_LOAD_LABELS  */
    CMD_SAVE_LABELS = 328,         /* CMD_SAVE_LABELS  */
    CMD_ADD_LABEL = 329,           /* CMD_ADD_LABEL  */
    CMD_DEL_LABEL = 330,           /* CMD_DEL_LABEL  */
    CMD_SHOW_LABELS = 331,         /* CMD_SHOW_LABELS  */
    CMD_CLEAR_LABELS = 332,        /* CMD_CLEAR_LABELS  */
    CMD_RECORD = 333,              /* CMD_RECORD  */
    CMD_MON_STOP = 334,            /* CMD_MON_STOP  */
    CMD_PLAYBACK = 335,            /* CMD_PLAYBACK  */
    CMD_CHAR_DISPLAY = 336,        /* CMD_CHAR_DISPLAY  */
    CMD_SPRITE_DISPLAY = 337,      /* CMD_SPRITE_DISPLAY  */
    CMD_TEXT_DISPLAY = 338,        /* CMD_TEXT_DISPLAY  */
    CMD_SCREENCODE_DISPLAY = 339,  /* CMD_SCREENCODE_DISPLAY  */
    CMD_ENTER_DATA = 340,          /* CMD_ENTER_DATA  */
    CMD_ENTER_BIN_DATA = 341,      /* CMD_ENTER_BIN_DATA  */
    CMD_KEYBUF = 342,              /* CMD_KEYBUF  */
    CMD_BLOAD = 343,               /* CMD_BLOAD  */
    CMD_BSAVE = 344,               /* CMD_BSAVE  */
    CMD_SCREEN = 345,              /* CMD_SCREEN  */
    CMD_UNTIL = 346,               /* CMD_UNTIL  */
    CMD_CPU = 347,                 /* CMD_CPU  */
    CMD_YYDEBUG = 348,             /* CMD_YYDEBUG  */
    CMD_BACKTRACE = 349,           /* CMD_BACKTRACE  */
    CMD_SCREENSHOT = 350,          /* CMD_SCREENSHOT  */
    CMD_PWD = 351,                 /* CMD_PWD  */
    CMD_DIR = 352,                 /* CMD_DIR  */
    CMD_MKDIR = 353,               /* CMD_MKDIR  */
    CMD_RMDIR = 354,               /* CMD_RMDIR  */
    CMD_RESOURCE_GET = 355,        /* CMD_RESOURCE_GET  */
    CMD_RESOURCE_SET = 356,        /* CMD_RESOURCE_SET  */
    CMD_LOAD_RESOURCES = 357,      /* CMD_LOAD_RESOURCES  */
    CMD_SAVE_RESOURCES = 358,      /* CMD_SAVE_RESOURCES  */
    CMD_ATTACH = 359,              /* CMD_ATTACH  */
    CMD_DETACH = 360,              /* CMD_DETACH  */
    CMD_MON_RESET = 361,           /* CMD_MON_RESET  */
    CMD_TAPECTRL = 362,            /* CMD_TAPECTRL  */
    CMD_TAPEOFFS = 363,            /* CMD_TAPEOFFS  */
    CMD_CARTFREEZE = 364,          /* CMD_CARTFREEZE  */
    CMD_UPDB = 365,                /* CMD_UPDB  */
    CMD_JPDB = 366,                /* CMD_JPDB  */
    CMD_CPUHISTORY = 367,          /* CMD_CPUHISTORY  */
    CMD_MEMMAPZAP = 368,           /* CMD_MEMMAPZAP  */
    CMD_MEMMAPSHOW = 369,          /* CMD_MEMMAPSHOW  */
    CMD_MEMMAPSAVE = 370,          /* CMD_MEMMAPSAVE  */
    CMD_COMMENT = 371,             /* CMD_COMMENT  */
    CMD_LIST = 372,                /* CMD_LIST  */
    CMD_STOPWATCH = 373,           /* CMD_STOPWATCH  */
    RESET = 374,                   /* RESET  */
    CMD_KEYMATRIX = 375,           /* CMD_KEYMATRIX  */
    CMD_JOYSTICK = 376,            /* CMD_JOYSTICK  */
    KEY_RESTORE = 377,             /* KEY_RESTORE  */
    CMD_EXPORT = 378,              /* CMD_EXPORT  */
    CMD_AUTOSTART = 379,           /* CMD_AUTOSTART  */
    CMD_AUTOLOAD = 380,            /* CMD_AUTOLOAD  */
    CMD_MAINCPU_TRACE = 381,       /* CMD_MAINCPU_TRACE  */
    CMD_WARP = 382,                /* CMD_WARP  */
    CMD_PROFILE = 383,             /* CMD_PROFILE  */
    FLAT = 384,                    /* FLAT  */
    GRAPH = 385,                   /* GRAPH  */
    FUNC = 386,                    /* FUNC  */
    DEPTH = 387,                   /* DEPTH  */
    DISASS = 388,                  /* DISASS  */
    PROFILE_CONTEXT = 389,         /* PROFILE_CONTEXT  */
    CLEAR = 390,                   /* CLEAR  */
    CMD_LABEL_ASGN = 391,          /* CMD_LABEL_ASGN  */
    L_PAREN = 392,                 /* L_PAREN  */
    R_PAREN = 393,                 /* R_PAREN  */
    ARG_IMMEDIATE = 394,           /* ARG_IMMEDIATE  */
    REG_A = 395,                   /* REG_A  */
    REG_X = 396,                   /* REG_X  */
    REG_Y = 397,                   /* REG_Y  */
    COMMA = 398,                   /* COMMA  */
    INST_SEP = 399,                /* INST_SEP  */
    L_BRACKET = 400,               /* L_BRACKET  */
    R_BRACKET = 401,               /* R_BRACKET  */
    LESS_THAN = 402,               /* LESS_THAN  */
    REG_U = 403,                   /* REG_U  */
    REG_S = 404,                   /* REG_S  */
    REG_PC = 405,                  /* REG_PC  */
    REG_PCR = 406,                 /* REG_PCR  */
    REG_B = 407,                   /* REG_B  */
    REG_C = 408,                   /* REG_C  */
    REG_D = 409,                   /* REG_D  */
    REG_E = 410,                   /* REG_E  */
    REG_H = 411,                   /* REG_H  */
    REG_L = 412,                   /* REG_L  */
    REG_AF = 413,                  /* REG_AF  */
    REG_BC = 414,                  /* REG_BC  */
    REG_DE = 415,                  /* REG_DE  */
    REG_HL = 416,                  /* REG_HL  */
    REG_IX = 417,                  /* REG_IX  */
    REG_IY = 418,                  /* REG_IY  */
    REG_SP = 419,                  /* REG_SP  */
    REG_IXH = 420,                 /* REG_IXH  */
    REG_IXL = 421,                 /* REG_IXL  */
    REG_IYH = 422,                 /* REG_IYH  */
    REG_IYL = 423,                 /* REG_IYL  */
    PLUS = 424,                    /* PLUS  */
    MINUS = 425,                   /* MINUS  */
    STRING = 426,                  /* STRING  */
    FILENAME = 427,                /* FILENAME  */
    R_O_L = 428,                   /* R_O_L  */
    R_O_L_Q = 429,                 /* R_O_L_Q  */
    OPCODE = 430,                  /* OPCODE  */
    LABEL = 431,                   /* LABEL  */
    BANKNAME = 432,                /* BANKNAME  */
    CPUTYPE = 433,                 /* CPUTYPE  */
    MON_REGISTER = 434,            /* MON_REGISTER  */
    COND_OP = 435,                 /* COND_OP  */
    RADIX_TYPE = 436,              /* RADIX_TYPE  */
    INPUT_SPEC = 437,              /* INPUT_SPEC  */
    CMD_CHECKPT_ON = 438,          /* CMD_CHECKPT_ON  */
    CMD_CHECKPT_OFF = 439,         /* CMD_CHECKPT_OFF  */
    TOGGLE = 440,                  /* TOGGLE  */
    MASK = 441                     /* MASK  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 124 "src/monitor/mon_parse.y"

    MON_ADDR a;
    MON_ADDR range[2];
    int i;
    REG_ID reg;
    CONDITIONAL cond_op;
    cond_node_t *cond_node;
    RADIXTYPE rt;
    ACTION action;
    char *str;
    asm_mode_addr_info_t mode;

#line 263 "src/monitor/mon_parse.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_SRC_MONITOR_MON_PARSE_H_INCLUDED  */
