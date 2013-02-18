#define NIL 257
#define BREAK 258
#define DO 259
#define MAPPING 260
#define ELSE 261
#define CASE 262
#define OBJECT 263
#define DEFAULT 264
#define STATIC 265
#define CONTINUE 266
#define INT 267
#define FLOAT 268
#define RLIMITS 269
#define FOR 270
#define INHERIT 271
#define IF 272
#define GOTO 273
#define RETURN 274
#define MIXED 275
#define STRING 276
#define WHILE 277
#define FUNCTION 278
#define CATCH 279
#define SWITCH 280
#define VOID 281
#define PRIVATE 282
#define ATOMIC 283
#define NOMASK 284
#define VARARGS 285
#define LARROW 286
#define RARROW 287
#define PLUS_PLUS 288
#define MIN_MIN 289
#define LSHIFT 290
#define RSHIFT 291
#define LE 292
#define GE 293
#define EQ 294
#define NE 295
#define LAND 296
#define LOR 297
#define PLUS_EQ 298
#define MIN_EQ 299
#define MULT_EQ 300
#define DIV_EQ 301
#define MOD_EQ 302
#define LSHIFT_EQ 303
#define RSHIFT_EQ 304
#define AND_EQ 305
#define XOR_EQ 306
#define OR_EQ 307
#define COLON_COLON 308
#define DOT_DOT 309
#define ELLIPSIS 310
#define STRING_CONST 311
#define IDENTIFIER 312
#define INT_CONST 313
#define FLOAT_CONST 314
#define MARK 315
#define HASH 316
#define HASH_HASH 317
#define INCL_CONST 318
#define NR_TOKENS 319
typedef union {
    Int number;			/* lex input */
    xfloat real;		/* lex input */
    unsigned short type;	/* internal */
    struct _node_ *node;	/* internal */
} YYSTYPE;
extern YYSTYPE yylval;