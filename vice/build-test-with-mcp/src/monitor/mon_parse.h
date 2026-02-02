/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

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

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     H_NUMBER = 258,
     D_NUMBER = 259,
     O_NUMBER = 260,
     B_NUMBER = 261,
     CONVERT_OP = 262,
     B_DATA = 263,
     H_RANGE_GUESS = 264,
     D_NUMBER_GUESS = 265,
     O_NUMBER_GUESS = 266,
     B_NUMBER_GUESS = 267,
     BAD_CMD = 268,
     MEM_OP = 269,
     IF = 270,
     MEM_COMP = 271,
     MEM_DISK8 = 272,
     MEM_DISK9 = 273,
     MEM_DISK10 = 274,
     MEM_DISK11 = 275,
     EQUALS = 276,
     TRAIL = 277,
     CMD_SEP = 278,
     LABEL_ASGN_COMMENT = 279,
     CMD_LOG = 280,
     CMD_LOGNAME = 281,
     CMD_SIDEFX = 282,
     CMD_DUMMY = 283,
     CMD_RETURN = 284,
     CMD_BLOCK_READ = 285,
     CMD_BLOCK_WRITE = 286,
     CMD_UP = 287,
     CMD_DOWN = 288,
     CMD_LOAD = 289,
     CMD_BASICLOAD = 290,
     CMD_SAVE = 291,
     CMD_VERIFY = 292,
     CMD_BVERIFY = 293,
     CMD_IGNORE = 294,
     CMD_HUNT = 295,
     CMD_FILL = 296,
     CMD_MOVE = 297,
     CMD_GOTO = 298,
     CMD_REGISTERS = 299,
     CMD_READSPACE = 300,
     CMD_WRITESPACE = 301,
     CMD_RADIX = 302,
     CMD_MEM_DISPLAY = 303,
     CMD_BREAK = 304,
     CMD_TRACE = 305,
     CMD_IO = 306,
     CMD_BRMON = 307,
     CMD_COMPARE = 308,
     CMD_DUMP = 309,
     CMD_UNDUMP = 310,
     CMD_EXIT = 311,
     CMD_DELETE = 312,
     CMD_CONDITION = 313,
     CMD_COMMAND = 314,
     CMD_ASSEMBLE = 315,
     CMD_DISASSEMBLE = 316,
     CMD_NEXT = 317,
     CMD_STEP = 318,
     CMD_PRINT = 319,
     CMD_DEVICE = 320,
     CMD_HELP = 321,
     CMD_WATCH = 322,
     CMD_DISK = 323,
     CMD_QUIT = 324,
     CMD_CHDIR = 325,
     CMD_BANK = 326,
     CMD_LOAD_LABELS = 327,
     CMD_SAVE_LABELS = 328,
     CMD_ADD_LABEL = 329,
     CMD_DEL_LABEL = 330,
     CMD_SHOW_LABELS = 331,
     CMD_CLEAR_LABELS = 332,
     CMD_RECORD = 333,
     CMD_MON_STOP = 334,
     CMD_PLAYBACK = 335,
     CMD_CHAR_DISPLAY = 336,
     CMD_SPRITE_DISPLAY = 337,
     CMD_TEXT_DISPLAY = 338,
     CMD_SCREENCODE_DISPLAY = 339,
     CMD_ENTER_DATA = 340,
     CMD_ENTER_BIN_DATA = 341,
     CMD_KEYBUF = 342,
     CMD_BLOAD = 343,
     CMD_BSAVE = 344,
     CMD_SCREEN = 345,
     CMD_UNTIL = 346,
     CMD_CPU = 347,
     CMD_YYDEBUG = 348,
     CMD_BACKTRACE = 349,
     CMD_SCREENSHOT = 350,
     CMD_PWD = 351,
     CMD_DIR = 352,
     CMD_MKDIR = 353,
     CMD_RMDIR = 354,
     CMD_RESOURCE_GET = 355,
     CMD_RESOURCE_SET = 356,
     CMD_LOAD_RESOURCES = 357,
     CMD_SAVE_RESOURCES = 358,
     CMD_ATTACH = 359,
     CMD_DETACH = 360,
     CMD_MON_RESET = 361,
     CMD_TAPECTRL = 362,
     CMD_TAPEOFFS = 363,
     CMD_CARTFREEZE = 364,
     CMD_UPDB = 365,
     CMD_JPDB = 366,
     CMD_CPUHISTORY = 367,
     CMD_MEMMAPZAP = 368,
     CMD_MEMMAPSHOW = 369,
     CMD_MEMMAPSAVE = 370,
     CMD_COMMENT = 371,
     CMD_LIST = 372,
     CMD_STOPWATCH = 373,
     RESET = 374,
     CMD_EXPORT = 375,
     CMD_AUTOSTART = 376,
     CMD_AUTOLOAD = 377,
     CMD_MAINCPU_TRACE = 378,
     CMD_WARP = 379,
     CMD_PROFILE = 380,
     FLAT = 381,
     GRAPH = 382,
     FUNC = 383,
     DEPTH = 384,
     DISASS = 385,
     PROFILE_CONTEXT = 386,
     CLEAR = 387,
     CMD_LABEL_ASGN = 388,
     L_PAREN = 389,
     R_PAREN = 390,
     ARG_IMMEDIATE = 391,
     REG_A = 392,
     REG_X = 393,
     REG_Y = 394,
     COMMA = 395,
     INST_SEP = 396,
     L_BRACKET = 397,
     R_BRACKET = 398,
     LESS_THAN = 399,
     REG_U = 400,
     REG_S = 401,
     REG_PC = 402,
     REG_PCR = 403,
     REG_B = 404,
     REG_C = 405,
     REG_D = 406,
     REG_E = 407,
     REG_H = 408,
     REG_L = 409,
     REG_AF = 410,
     REG_BC = 411,
     REG_DE = 412,
     REG_HL = 413,
     REG_IX = 414,
     REG_IY = 415,
     REG_SP = 416,
     REG_IXH = 417,
     REG_IXL = 418,
     REG_IYH = 419,
     REG_IYL = 420,
     PLUS = 421,
     MINUS = 422,
     STRING = 423,
     FILENAME = 424,
     R_O_L = 425,
     R_O_L_Q = 426,
     OPCODE = 427,
     LABEL = 428,
     BANKNAME = 429,
     CPUTYPE = 430,
     MON_REGISTER = 431,
     COND_OP = 432,
     RADIX_TYPE = 433,
     INPUT_SPEC = 434,
     CMD_CHECKPT_ON = 435,
     CMD_CHECKPT_OFF = 436,
     TOGGLE = 437,
     MASK = 438
   };
#endif
/* Tokens.  */
#define H_NUMBER 258
#define D_NUMBER 259
#define O_NUMBER 260
#define B_NUMBER 261
#define CONVERT_OP 262
#define B_DATA 263
#define H_RANGE_GUESS 264
#define D_NUMBER_GUESS 265
#define O_NUMBER_GUESS 266
#define B_NUMBER_GUESS 267
#define BAD_CMD 268
#define MEM_OP 269
#define IF 270
#define MEM_COMP 271
#define MEM_DISK8 272
#define MEM_DISK9 273
#define MEM_DISK10 274
#define MEM_DISK11 275
#define EQUALS 276
#define TRAIL 277
#define CMD_SEP 278
#define LABEL_ASGN_COMMENT 279
#define CMD_LOG 280
#define CMD_LOGNAME 281
#define CMD_SIDEFX 282
#define CMD_DUMMY 283
#define CMD_RETURN 284
#define CMD_BLOCK_READ 285
#define CMD_BLOCK_WRITE 286
#define CMD_UP 287
#define CMD_DOWN 288
#define CMD_LOAD 289
#define CMD_BASICLOAD 290
#define CMD_SAVE 291
#define CMD_VERIFY 292
#define CMD_BVERIFY 293
#define CMD_IGNORE 294
#define CMD_HUNT 295
#define CMD_FILL 296
#define CMD_MOVE 297
#define CMD_GOTO 298
#define CMD_REGISTERS 299
#define CMD_READSPACE 300
#define CMD_WRITESPACE 301
#define CMD_RADIX 302
#define CMD_MEM_DISPLAY 303
#define CMD_BREAK 304
#define CMD_TRACE 305
#define CMD_IO 306
#define CMD_BRMON 307
#define CMD_COMPARE 308
#define CMD_DUMP 309
#define CMD_UNDUMP 310
#define CMD_EXIT 311
#define CMD_DELETE 312
#define CMD_CONDITION 313
#define CMD_COMMAND 314
#define CMD_ASSEMBLE 315
#define CMD_DISASSEMBLE 316
#define CMD_NEXT 317
#define CMD_STEP 318
#define CMD_PRINT 319
#define CMD_DEVICE 320
#define CMD_HELP 321
#define CMD_WATCH 322
#define CMD_DISK 323
#define CMD_QUIT 324
#define CMD_CHDIR 325
#define CMD_BANK 326
#define CMD_LOAD_LABELS 327
#define CMD_SAVE_LABELS 328
#define CMD_ADD_LABEL 329
#define CMD_DEL_LABEL 330
#define CMD_SHOW_LABELS 331
#define CMD_CLEAR_LABELS 332
#define CMD_RECORD 333
#define CMD_MON_STOP 334
#define CMD_PLAYBACK 335
#define CMD_CHAR_DISPLAY 336
#define CMD_SPRITE_DISPLAY 337
#define CMD_TEXT_DISPLAY 338
#define CMD_SCREENCODE_DISPLAY 339
#define CMD_ENTER_DATA 340
#define CMD_ENTER_BIN_DATA 341
#define CMD_KEYBUF 342
#define CMD_BLOAD 343
#define CMD_BSAVE 344
#define CMD_SCREEN 345
#define CMD_UNTIL 346
#define CMD_CPU 347
#define CMD_YYDEBUG 348
#define CMD_BACKTRACE 349
#define CMD_SCREENSHOT 350
#define CMD_PWD 351
#define CMD_DIR 352
#define CMD_MKDIR 353
#define CMD_RMDIR 354
#define CMD_RESOURCE_GET 355
#define CMD_RESOURCE_SET 356
#define CMD_LOAD_RESOURCES 357
#define CMD_SAVE_RESOURCES 358
#define CMD_ATTACH 359
#define CMD_DETACH 360
#define CMD_MON_RESET 361
#define CMD_TAPECTRL 362
#define CMD_TAPEOFFS 363
#define CMD_CARTFREEZE 364
#define CMD_UPDB 365
#define CMD_JPDB 366
#define CMD_CPUHISTORY 367
#define CMD_MEMMAPZAP 368
#define CMD_MEMMAPSHOW 369
#define CMD_MEMMAPSAVE 370
#define CMD_COMMENT 371
#define CMD_LIST 372
#define CMD_STOPWATCH 373
#define RESET 374
#define CMD_EXPORT 375
#define CMD_AUTOSTART 376
#define CMD_AUTOLOAD 377
#define CMD_MAINCPU_TRACE 378
#define CMD_WARP 379
#define CMD_PROFILE 380
#define FLAT 381
#define GRAPH 382
#define FUNC 383
#define DEPTH 384
#define DISASS 385
#define PROFILE_CONTEXT 386
#define CLEAR 387
#define CMD_LABEL_ASGN 388
#define L_PAREN 389
#define R_PAREN 390
#define ARG_IMMEDIATE 391
#define REG_A 392
#define REG_X 393
#define REG_Y 394
#define COMMA 395
#define INST_SEP 396
#define L_BRACKET 397
#define R_BRACKET 398
#define LESS_THAN 399
#define REG_U 400
#define REG_S 401
#define REG_PC 402
#define REG_PCR 403
#define REG_B 404
#define REG_C 405
#define REG_D 406
#define REG_E 407
#define REG_H 408
#define REG_L 409
#define REG_AF 410
#define REG_BC 411
#define REG_DE 412
#define REG_HL 413
#define REG_IX 414
#define REG_IY 415
#define REG_SP 416
#define REG_IXH 417
#define REG_IXL 418
#define REG_IYH 419
#define REG_IYL 420
#define PLUS 421
#define MINUS 422
#define STRING 423
#define FILENAME 424
#define R_O_L 425
#define R_O_L_Q 426
#define OPCODE 427
#define LABEL 428
#define BANKNAME 429
#define CPUTYPE 430
#define MON_REGISTER 431
#define COND_OP 432
#define RADIX_TYPE 433
#define INPUT_SPEC 434
#define CMD_CHECKPT_ON 435
#define CMD_CHECKPT_OFF 436
#define TOGGLE 437
#define MASK 438




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 123 "../../../src/monitor/mon_parse.y"
{
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
}
/* Line 1529 of yacc.c.  */
#line 428 "mon_parse.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

