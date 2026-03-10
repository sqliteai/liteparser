/*
** lp_tokenize.c — Tokenizer for liteparser, adapted from SQLite's tokenize.c.
**
** The original code is in the public domain.  This adaptation likewise
** carries no copyright restrictions.
*/
#include "liteparser.h"
#include "liteparser_internal.h"
#include "parse.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Replacement macros for SQLite internals                            */
/* ------------------------------------------------------------------ */

#define testcase(X)
#define SQLITE_DIGIT_SEPARATOR '_'

#define sqlite3Isspace(c)  ((c)==' '||(c)=='\t'||(c)=='\n'||(c)=='\r'||(c)=='\f')
#define sqlite3Isdigit(c)  ((c)>='0'&&(c)<='9')
#define sqlite3Isxdigit(c) (((c)>='0'&&(c)<='9')||((c)>='a'&&(c)<='f')||((c)>='A'&&(c)<='F'))

/* ------------------------------------------------------------------ */
/*  Character-type map (from SQLite global.c, ASCII only)              */
/*                                                                     */
/*  Bit 0x01 = space                                                   */
/*  Bit 0x02 = alpha                                                   */
/*  Bit 0x04 = digit                                                   */
/*  Bit 0x08 = hex digit (A-F / a-f)                                   */
/*  Bit 0x20 = needs upper-case translation (i.e. lowercase letter)    */
/*  Bit 0x40 = non-alphanumeric identifier char ($, _, or >= 0x80)     */
/*  Bit 0x80 = quote character (" ' ` [)                               */
/* ------------------------------------------------------------------ */
static const unsigned char lp_CtypeMap[256] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 00..07    ........ */
  0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,  /* 08..0f    ........ */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 10..17    ........ */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 18..1f    ........ */
  0x01, 0x00, 0x80, 0x00, 0x40, 0x00, 0x00, 0x80,  /* 20..27     !"#$%&' */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 28..2f    ()*+,-./ */
  0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,  /* 30..37    01234567 */
  0x0c, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 38..3f    89:;<=>? */
  0x00, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x02,  /* 40..47    @ABCDEFG */
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  /* 48..4f    HIJKLMNO */
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  /* 50..57    PQRSTUVW */
  0x02, 0x02, 0x02, 0x80, 0x00, 0x00, 0x00, 0x40,  /* 58..5f    XYZ[\]^_ */
  0x80, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x22,  /* 60..67    `abcdefg */
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,  /* 68..6f    hijklmno */
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,  /* 70..77    pqrstuvw */
  0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 78..7f    xyz{|}~. */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* 80..87    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* 88..8f    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* 90..97    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* 98..9f    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* a0..a7    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* a8..af    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* b0..b7    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* b8..bf    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* c0..c7    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* c8..cf    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* d0..d7    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* d8..df    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* e0..e7    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* e8..ef    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* f0..f7    ........ */
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,  /* f8..ff    ........ */
};

/* Identifier character: alphanumeric, '_', '$', or any byte >= 0x80.
** Uses the 0x46 mask: 0x40 (special id chars) | 0x04 (digit) | 0x02 (alpha). */
#define IdChar(C) ((lp_CtypeMap[(unsigned char)(C)] & 0x46) != 0)

/* ------------------------------------------------------------------ */
/*  Upper-to-lower mapping table (from SQLite global.c, ASCII only)    */
/* ------------------------------------------------------------------ */
static const unsigned char lp_upper_to_lower[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
   32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
   48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
   64, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111,
  112,113,114,115,116,117,118,119,120,121,122, 91, 92, 93, 94, 95,
   96, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111,
  112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
  128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
  144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
  160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
  176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
  192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
  208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
  224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
  240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,
};
#define charMap(X) lp_upper_to_lower[(unsigned char)(X)]

/* ------------------------------------------------------------------ */
/*  Character class table (from SQLite tokenize.c, ASCII only)         */
/* ------------------------------------------------------------------ */
#define CC_X          0
#define CC_KYWD0      1
#define CC_KYWD       2
#define CC_DIGIT      3
#define CC_DOLLAR     4
#define CC_VARALPHA   5
#define CC_VARNUM     6
#define CC_SPACE      7
#define CC_QUOTE      8
#define CC_QUOTE2     9
#define CC_PIPE      10
#define CC_MINUS     11
#define CC_LT        12
#define CC_GT        13
#define CC_EQ        14
#define CC_BANG      15
#define CC_SLASH     16
#define CC_LP        17
#define CC_RP        18
#define CC_SEMI      19
#define CC_PLUS      20
#define CC_STAR      21
#define CC_PERCENT   22
#define CC_COMMA     23
#define CC_AND       24
#define CC_TILDA     25
#define CC_DOT       26
#define CC_ID        27
#define CC_ILLEGAL   28
#define CC_NUL       29
#define CC_BOM       30

static const unsigned char aiClass[] = {
/*         x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xa  xb  xc  xd  xe  xf */
/* 0x */   29, 28, 28, 28, 28, 28, 28, 28, 28,  7,  7, 28,  7,  7, 28, 28,
/* 1x */   28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
/* 2x */    7, 15,  8,  5,  4, 22, 24,  8, 17, 18, 21, 20, 23, 11, 26, 16,
/* 3x */    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  5, 19, 12, 14, 13,  6,
/* 4x */    5,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
/* 5x */    1,  1,  1,  1,  1,  1,  1,  1,  0,  2,  2,  9, 28, 28, 28,  2,
/* 6x */    8,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
/* 7x */    1,  1,  1,  1,  1,  1,  1,  1,  0,  2,  2, 28, 10, 28, 25, 28,
/* 8x */   27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
/* 9x */   27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
/* Ax */   27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
/* Bx */   27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
/* Cx */   27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
/* Dx */   27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
/* Ex */   27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 30,
/* Fx */   27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27
};

/* ------------------------------------------------------------------ */
/*  Keyword lookup — sorted table with binary search                   */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;   /* Upper-case keyword text */
    short       len;    /* strlen(name) */
    short       code;   /* TK_ token code */
} LpKeyword;

/*
** Sorted alphabetically by name.
** NOTE: COLUMN uses TK_COLUMNKW, CROSS/FULL/INNER/LEFT/NATURAL/OUTER/RIGHT
** use TK_JOIN_KW, CURRENT_DATE/TIME/TIMESTAMP use TK_CTIME_KW,
** GLOB/LIKE/REGEXP use TK_LIKE_KW, TEMP and TEMPORARY both use TK_TEMP.
*/
static const LpKeyword lp_keywords[] = {
    { "ABORT",             5,  TK_ABORT },
    { "ACTION",            6,  TK_ACTION },
    { "ADD",               3,  TK_ADD },
    { "AFTER",             5,  TK_AFTER },
    { "ALL",               3,  TK_ALL },
    { "ALTER",             5,  TK_ALTER },
    { "ALWAYS",            6,  TK_ALWAYS },
    { "ANALYZE",           7,  TK_ANALYZE },
    { "AND",               3,  TK_AND },
    { "AS",                2,  TK_AS },
    { "ASC",               3,  TK_ASC },
    { "ATTACH",            6,  TK_ATTACH },
    { "AUTOINCREMENT",    13,  TK_AUTOINCR },
    { "BEFORE",            6,  TK_BEFORE },
    { "BEGIN",             5,  TK_BEGIN },
    { "BETWEEN",           7,  TK_BETWEEN },
    { "BY",                2,  TK_BY },
    { "CASCADE",           7,  TK_CASCADE },
    { "CASE",              4,  TK_CASE },
    { "CAST",              4,  TK_CAST },
    { "CHECK",             5,  TK_CHECK },
    { "COLLATE",           7,  TK_COLLATE },
    { "COLUMN",            6,  TK_COLUMNKW },
    { "COMMIT",            6,  TK_COMMIT },
    { "CONFLICT",          8,  TK_CONFLICT },
    { "CONSTRAINT",       10,  TK_CONSTRAINT },
    { "CREATE",            6,  TK_CREATE },
    { "CROSS",             5,  TK_JOIN_KW },
    { "CURRENT",           7,  TK_CURRENT },
    { "CURRENT_DATE",     12,  TK_CTIME_KW },
    { "CURRENT_TIME",     12,  TK_CTIME_KW },
    { "CURRENT_TIMESTAMP",17,  TK_CTIME_KW },
    { "DATABASE",          8,  TK_DATABASE },
    { "DEFAULT",           7,  TK_DEFAULT },
    { "DEFERRABLE",       10,  TK_DEFERRABLE },
    { "DEFERRED",          8,  TK_DEFERRED },
    { "DELETE",            6,  TK_DELETE },
    { "DESC",              4,  TK_DESC },
    { "DETACH",            6,  TK_DETACH },
    { "DISTINCT",          8,  TK_DISTINCT },
    { "DO",                2,  TK_DO },
    { "DROP",              4,  TK_DROP },
    { "EACH",              4,  TK_EACH },
    { "ELSE",              4,  TK_ELSE },
    { "END",               3,  TK_END },
    { "ESCAPE",            6,  TK_ESCAPE },
    { "EXCEPT",            6,  TK_EXCEPT },
    { "EXCLUDE",           7,  TK_EXCLUDE },
    { "EXCLUSIVE",         9,  TK_EXCLUSIVE },
    { "EXISTS",            6,  TK_EXISTS },
    { "EXPLAIN",           7,  TK_EXPLAIN },
    { "FAIL",              4,  TK_FAIL },
    { "FILTER",            6,  TK_FILTER },
    { "FIRST",             5,  TK_FIRST },
    { "FOLLOWING",         9,  TK_FOLLOWING },
    { "FOR",               3,  TK_FOR },
    { "FOREIGN",           7,  TK_FOREIGN },
    { "FROM",              4,  TK_FROM },
    { "FULL",              4,  TK_JOIN_KW },
    { "GENERATED",         9,  TK_GENERATED },
    { "GLOB",              4,  TK_LIKE_KW },
    { "GROUP",             5,  TK_GROUP },
    { "GROUPS",            6,  TK_GROUPS },
    { "HAVING",            6,  TK_HAVING },
    { "IF",                2,  TK_IF },
    { "IGNORE",            6,  TK_IGNORE },
    { "IMMEDIATE",         9,  TK_IMMEDIATE },
    { "IN",                2,  TK_IN },
    { "INDEX",             5,  TK_INDEX },
    { "INDEXED",           7,  TK_INDEXED },
    { "INITIALLY",         9,  TK_INITIALLY },
    { "INNER",             5,  TK_JOIN_KW },
    { "INSERT",            6,  TK_INSERT },
    { "INSTEAD",           7,  TK_INSTEAD },
    { "INTERSECT",         9,  TK_INTERSECT },
    { "INTO",              4,  TK_INTO },
    { "IS",                2,  TK_IS },
    { "ISNULL",            6,  TK_ISNULL },
    { "JOIN",              4,  TK_JOIN },
    { "KEY",               3,  TK_KEY },
    { "LAST",              4,  TK_LAST },
    { "LEFT",              4,  TK_JOIN_KW },
    { "LIKE",              4,  TK_LIKE_KW },
    { "LIMIT",             5,  TK_LIMIT },
    { "MATCH",             5,  TK_MATCH },
    { "MATERIALIZED",     12,  TK_MATERIALIZED },
    { "NATURAL",           7,  TK_JOIN_KW },
    { "NO",                2,  TK_NO },
    { "NOT",               3,  TK_NOT },
    { "NOTHING",           7,  TK_NOTHING },
    { "NOTNULL",           7,  TK_NOTNULL },
    { "NULL",              4,  TK_NULL },
    { "NULLS",             5,  TK_NULLS },
    { "OF",                2,  TK_OF },
    { "OFFSET",            6,  TK_OFFSET },
    { "ON",                2,  TK_ON },
    { "OR",                2,  TK_OR },
    { "ORDER",             5,  TK_ORDER },
    { "OTHERS",            6,  TK_OTHERS },
    { "OUTER",             5,  TK_JOIN_KW },
    { "OVER",              4,  TK_OVER },
    { "PARTITION",         9,  TK_PARTITION },
    { "PLAN",              4,  TK_PLAN },
    { "PRAGMA",            6,  TK_PRAGMA },
    { "PRECEDING",         9,  TK_PRECEDING },
    { "PRIMARY",           7,  TK_PRIMARY },
    { "QUERY",             5,  TK_QUERY },
    { "RAISE",             5,  TK_RAISE },
    { "RANGE",             5,  TK_RANGE },
    { "RECURSIVE",         9,  TK_RECURSIVE },
    { "REFERENCES",       10,  TK_REFERENCES },
    { "REGEXP",            6,  TK_LIKE_KW },
    { "REINDEX",           7,  TK_REINDEX },
    { "RELEASE",           7,  TK_RELEASE },
    { "RENAME",            6,  TK_RENAME },
    { "REPLACE",           7,  TK_REPLACE },
    { "RESTRICT",          8,  TK_RESTRICT },
    { "RETURNING",         9,  TK_RETURNING },
    { "RIGHT",             5,  TK_JOIN_KW },
    { "ROLLBACK",          8,  TK_ROLLBACK },
    { "ROW",               3,  TK_ROW },
    { "ROWS",              4,  TK_ROWS },
    { "SAVEPOINT",         9,  TK_SAVEPOINT },
    { "SELECT",            6,  TK_SELECT },
    { "SET",               3,  TK_SET },
    { "TABLE",             5,  TK_TABLE },
    { "TEMP",              4,  TK_TEMP },
    { "TEMPORARY",         9,  TK_TEMP },
    { "THEN",              4,  TK_THEN },
    { "TIES",              4,  TK_TIES },
    { "TO",                2,  TK_TO },
    { "TRANSACTION",      11,  TK_TRANSACTION },
    { "TRIGGER",           7,  TK_TRIGGER },
    { "UNBOUNDED",         9,  TK_UNBOUNDED },
    { "UNION",             5,  TK_UNION },
    { "UNIQUE",            6,  TK_UNIQUE },
    { "UPDATE",            6,  TK_UPDATE },
    { "USING",             5,  TK_USING },
    { "VACUUM",            6,  TK_VACUUM },
    { "VALUES",            6,  TK_VALUES },
    { "VIEW",              4,  TK_VIEW },
    { "VIRTUAL",           7,  TK_VIRTUAL },
    { "WHEN",              4,  TK_WHEN },
    { "WHERE",             5,  TK_WHERE },
    { "WINDOW",            6,  TK_WINDOW },
    { "WITH",              4,  TK_WITH },
    { "WITHIN",            6,  TK_WITHIN },
    { "WITHOUT",           7,  TK_WITHOUT },
};

#define LP_NUM_KEYWORDS \
    ((int)(sizeof(lp_keywords) / sizeof(lp_keywords[0])))

/*
** Case-insensitive comparison of z[0..n-1] against the upper-case keyword kw.
*/
static int lp_keyword_cmp(const char *z, int n, const char *kw, int kwlen) {
    int minlen = n < kwlen ? n : kwlen;
    for (int i = 0; i < minlen; i++) {
        int a = lp_upper_to_lower[(unsigned char)z[i]];
        int b = lp_upper_to_lower[(unsigned char)kw[i]];
        if (a != b) return a - b;
    }
    return n - kwlen;
}

/*
** Look up z[0..n-1] in the keyword table using binary search.
** If found, set *pType to the token code and return n.
** If not found, leave *pType unchanged and return n.
*/
static int keywordCode(const char *z, int n, int *pType) {
    int lo = 0;
    int hi = LP_NUM_KEYWORDS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = lp_keyword_cmp(z, n, lp_keywords[mid].name, lp_keywords[mid].len);
        if (cmp < 0) {
            hi = mid - 1;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            *pType = lp_keywords[mid].code;
            return n;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/*  lp_get_token — adapted from sqlite3GetToken()                      */
/* ------------------------------------------------------------------ */

int lp_get_token(const unsigned char *z, int *tokenType) {
    int i;
    int c;
    switch (aiClass[*z]) {
    case CC_SPACE: {
        testcase( z[0]==' ' );
        testcase( z[0]=='\t' );
        testcase( z[0]=='\n' );
        testcase( z[0]=='\f' );
        testcase( z[0]=='\r' );
        for (i = 1; sqlite3Isspace(z[i]); i++) {}
        *tokenType = TK_SPACE;
        return i;
    }
    case CC_MINUS: {
        if (z[1] == '-') {
            for (i = 2; (c = z[i]) != 0 && c != '\n'; i++) {}
            *tokenType = TK_COMMENT;
            return i;
        } else if (z[1] == '>') {
            *tokenType = TK_PTR;
            return 2 + (z[2] == '>');
        }
        *tokenType = TK_MINUS;
        return 1;
    }
    case CC_LP: {
        *tokenType = TK_LP;
        return 1;
    }
    case CC_RP: {
        *tokenType = TK_RP;
        return 1;
    }
    case CC_SEMI: {
        *tokenType = TK_SEMI;
        return 1;
    }
    case CC_PLUS: {
        *tokenType = TK_PLUS;
        return 1;
    }
    case CC_STAR: {
        *tokenType = TK_STAR;
        return 1;
    }
    case CC_SLASH: {
        if (z[1] != '*' || z[2] == 0) {
            *tokenType = TK_SLASH;
            return 1;
        }
        for (i = 3, c = z[2]; (c != '*' || z[i] != '/') && (c = z[i]) != 0; i++) {}
        if (c) i++;
        *tokenType = TK_COMMENT;
        return i;
    }
    case CC_PERCENT: {
        *tokenType = TK_REM;
        return 1;
    }
    case CC_EQ: {
        *tokenType = TK_EQ;
        return 1 + (z[1] == '=');
    }
    case CC_LT: {
        if ((c = z[1]) == '=') {
            *tokenType = TK_LE;
            return 2;
        } else if (c == '>') {
            *tokenType = TK_NE;
            return 2;
        } else if (c == '<') {
            *tokenType = TK_LSHIFT;
            return 2;
        } else {
            *tokenType = TK_LT;
            return 1;
        }
    }
    case CC_GT: {
        if ((c = z[1]) == '=') {
            *tokenType = TK_GE;
            return 2;
        } else if (c == '>') {
            *tokenType = TK_RSHIFT;
            return 2;
        } else {
            *tokenType = TK_GT;
            return 1;
        }
    }
    case CC_BANG: {
        if (z[1] != '=') {
            *tokenType = TK_ILLEGAL;
            return 1;
        } else {
            *tokenType = TK_NE;
            return 2;
        }
    }
    case CC_PIPE: {
        if (z[1] != '|') {
            *tokenType = TK_BITOR;
            return 1;
        } else {
            *tokenType = TK_CONCAT;
            return 2;
        }
    }
    case CC_COMMA: {
        *tokenType = TK_COMMA;
        return 1;
    }
    case CC_AND: {
        *tokenType = TK_BITAND;
        return 1;
    }
    case CC_TILDA: {
        *tokenType = TK_BITNOT;
        return 1;
    }
    case CC_QUOTE: {
        int delim = z[0];
        testcase( delim=='`' );
        testcase( delim=='\'' );
        testcase( delim=='"' );
        for (i = 1; (c = z[i]) != 0; i++) {
            if (c == delim) {
                if (z[i + 1] == delim) {
                    i++;
                } else {
                    break;
                }
            }
        }
        if (c == '\'') {
            *tokenType = TK_STRING;
            return i + 1;
        } else if (c != 0) {
            *tokenType = TK_ID;
            return i + 1;
        } else {
            *tokenType = TK_ILLEGAL;
            return i;
        }
    }
    case CC_DOT: {
        if (!sqlite3Isdigit(z[1])) {
            *tokenType = TK_DOT;
            return 1;
        }
        /* Fall thru into the next case — floating point starting with "." */
        /* fall through */
    }
    case CC_DIGIT: {
        testcase( z[0]=='0' );
        testcase( z[0]=='.' );
        *tokenType = TK_INTEGER;
        if (z[0] == '0' && (z[1] == 'x' || z[1] == 'X') && sqlite3Isxdigit(z[2])) {
            for (i = 3; ; i++) {
                if (sqlite3Isxdigit(z[i]) == 0) {
                    if (z[i] == SQLITE_DIGIT_SEPARATOR) {
                        *tokenType = TK_QNUMBER;
                    } else {
                        break;
                    }
                }
            }
        } else {
            for (i = 0; ; i++) {
                if (sqlite3Isdigit(z[i]) == 0) {
                    if (z[i] == SQLITE_DIGIT_SEPARATOR) {
                        *tokenType = TK_QNUMBER;
                    } else {
                        break;
                    }
                }
            }
            if (z[i] == '.') {
                if (*tokenType == TK_INTEGER) *tokenType = TK_FLOAT;
                for (i++; ; i++) {
                    if (sqlite3Isdigit(z[i]) == 0) {
                        if (z[i] == SQLITE_DIGIT_SEPARATOR) {
                            *tokenType = TK_QNUMBER;
                        } else {
                            break;
                        }
                    }
                }
            }
            if ((z[i] == 'e' || z[i] == 'E') &&
                (sqlite3Isdigit(z[i + 1])
                 || ((z[i + 1] == '+' || z[i + 1] == '-') && sqlite3Isdigit(z[i + 2])))
            ) {
                if (*tokenType == TK_INTEGER) *tokenType = TK_FLOAT;
                for (i += 2; ; i++) {
                    if (sqlite3Isdigit(z[i]) == 0) {
                        if (z[i] == SQLITE_DIGIT_SEPARATOR) {
                            *tokenType = TK_QNUMBER;
                        } else {
                            break;
                        }
                    }
                }
            }
        }
        while (IdChar(z[i])) {
            *tokenType = TK_ILLEGAL;
            i++;
        }
        return i;
    }
    case CC_QUOTE2: {
        for (i = 1, c = z[0]; c != ']' && (c = z[i]) != 0; i++) {}
        *tokenType = c == ']' ? TK_ID : TK_ILLEGAL;
        return i;
    }
    case CC_VARNUM: {
        *tokenType = TK_VARIABLE;
        for (i = 1; sqlite3Isdigit(z[i]); i++) {}
        return i;
    }
    case CC_DOLLAR:
    case CC_VARALPHA: {
        int n = 0;
        testcase( z[0]=='$' );  testcase( z[0]=='@' );
        testcase( z[0]==':' );  testcase( z[0]=='#' );
        *tokenType = TK_VARIABLE;
        for (i = 1; (c = z[i]) != 0; i++) {
            if (IdChar(c)) {
                n++;
            } else if (c == '(' && n > 0) {
                /* TCL variable syntax: $name(index) */
                do {
                    i++;
                } while ((c = z[i]) != 0 && !sqlite3Isspace(c) && c != ')');
                if (c == ')') {
                    i++;
                } else {
                    *tokenType = TK_ILLEGAL;
                }
                break;
            } else if (c == ':' && z[i + 1] == ':') {
                i++;
            } else {
                break;
            }
        }
        if (n == 0) *tokenType = TK_ILLEGAL;
        return i;
    }
    case CC_KYWD0: {
        if (aiClass[z[1]] > CC_KYWD) { i = 1; break; }
        for (i = 2; aiClass[z[i]] <= CC_KYWD; i++) {}
        if (IdChar(z[i])) {
            /* Token started with keyword chars but has a non-keyword id char */
            i++;
            break;
        }
        *tokenType = TK_ID;
        return keywordCode((char *)z, i, tokenType);
    }
    case CC_X: {
        testcase( z[0]=='x' ); testcase( z[0]=='X' );
        if (z[1] == '\'') {
            *tokenType = TK_BLOB;
            for (i = 2; sqlite3Isxdigit(z[i]); i++) {}
            if (z[i] != '\'' || i % 2) {
                *tokenType = TK_ILLEGAL;
                while (z[i] && z[i] != '\'') { i++; }
            }
            if (z[i]) i++;
            return i;
        }
        /* Not a blob literal — must be an identifier.  Fall through. */
        /* fall through */
    }
    case CC_KYWD:
    case CC_ID: {
        i = 1;
        break;
    }
    case CC_BOM: {
        if (z[1] == 0xbb && z[2] == 0xbf) {
            *tokenType = TK_SPACE;
            return 3;
        }
        i = 1;
        break;
    }
    case CC_NUL: {
        *tokenType = TK_ILLEGAL;
        return 0;
    }
    default: {
        *tokenType = TK_ILLEGAL;
        return 1;
    }
    }
    while (IdChar(z[i])) { i++; }
    *tokenType = TK_ID;
    return i;
}

/* ------------------------------------------------------------------ */
/*  Window-function keyword disambiguation helpers                     */
/* ------------------------------------------------------------------ */

/* Forward declaration of the Lemon-generated fallback function */
extern int lp_ParserFallback(int);

/*
** Internal getToken: skip spaces/comments, collapse IDs.
*/
static int lp_getToken(const unsigned char **pz) {
    const unsigned char *z = *pz;
    int t;
    do {
        z += lp_get_token(z, &t);
    } while (t == TK_SPACE || t == TK_COMMENT);
    if (t == TK_ID
     || t == TK_STRING
     || t == TK_JOIN_KW
     || t == TK_WINDOW
     || t == TK_OVER
     || lp_ParserFallback(t) == TK_ID
    ) {
        t = TK_ID;
    }
    *pz = z;
    return t;
}

/*
** WINDOW is a keyword if the next token is an identifier and the one
** after that is AS.
*/
static int analyzeWindowKeyword(const unsigned char *z) {
    int t;
    t = lp_getToken(&z);
    if (t != TK_ID) return TK_ID;
    t = lp_getToken(&z);
    if (t != TK_AS) return TK_ID;
    return TK_WINDOW;
}

/*
** OVER is a keyword if the previous token was TK_RP and the next token
** is TK_LP or an identifier.
*/
static int analyzeOverKeyword(const unsigned char *z, int lastToken) {
    if (lastToken == TK_RP) {
        int t = lp_getToken(&z);
        if (t == TK_LP || t == TK_ID) return TK_OVER;
    }
    return TK_ID;
}

/*
** FILTER is a keyword if the previous token was TK_RP and the next
** token is TK_LP.
*/
static int analyzeFilterKeyword(const unsigned char *z, int lastToken) {
    if (lastToken == TK_RP && lp_getToken(&z) == TK_LP) {
        return TK_FILTER;
    }
    return TK_ID;
}

/* ------------------------------------------------------------------ */
/*  lp_parse — main entry point                                        */
/* ------------------------------------------------------------------ */

/* Advance position tracking past n bytes starting at p. */
static void advance_pos(LpSrcPos *pos, const char *p, int n) {
    for (int i = 0; i < n; i++) {
        if (p[i] == '\n') {
            pos->line++;
            pos->col = 1;
        } else {
            pos->col++;
        }
        pos->offset++;
    }
}

/* Declare the Lemon-generated parser interface */
extern void *lp_ParserAlloc(void *(*)(size_t), LpParseContext *);
extern void  lp_ParserFree(void *, void (*)(void *));
extern void  lp_Parser(void *, int, LpToken, LpParseContext *);

/* Shared parse implementation — returns all statements via ctx.stmts */
static int lp_parse_internal(const char *sql, arena_t *arena,
                              LpParseContext *ctx, int tolerant) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->arena = arena;
    ctx->tolerant = tolerant;
    ctx->sql_start = sql;
    ctx->cur_pos.offset = 0;
    ctx->cur_pos.line = 1;
    ctx->cur_pos.col = 1;

    void *parser = lp_ParserAlloc(malloc, ctx);
    if (!parser) return -1;

    int lastTokenParsed = -1;
    const char *z = sql;

    while (1) {
        int tokenType;
        int n = lp_get_token((const unsigned char *)z, &tokenType);

        /* Handle high-value tokens: WINDOW, OVER, FILTER, SPACE, COMMENT,
        ** ILLEGAL, QNUMBER.  These all have token codes >= TK_WINDOW in the
        ** Lemon grammar (arranged so they sort last). */
        if (tokenType >= TK_WINDOW) {
            if (tokenType == TK_SPACE) {
                advance_pos(&ctx->cur_pos, z, n);
                z += n;
                continue;
            }
            if (z[0] == 0) {
                /* End of input — feed TK_SEMI then 0 to close the parser. */
                if (lastTokenParsed == TK_SEMI) {
                    tokenType = 0;
                } else if (lastTokenParsed == 0) {
                    break;
                } else {
                    tokenType = TK_SEMI;
                }
                n = 0;
            } else if (tokenType == TK_WINDOW) {
                tokenType = analyzeWindowKeyword((const unsigned char *)&z[6]);
            } else if (tokenType == TK_OVER) {
                tokenType = analyzeOverKeyword((const unsigned char *)&z[4], lastTokenParsed);
            } else if (tokenType == TK_FILTER) {
                tokenType = analyzeFilterKeyword((const unsigned char *)&z[6], lastTokenParsed);
            } else if (tokenType == TK_COMMENT) {
                advance_pos(&ctx->cur_pos, z, n);
                z += n;
                continue;  /* always skip comments */
            } else if (tokenType != TK_QNUMBER) {
                /* ILLEGAL token */
                {
                    LpSrcPos end = ctx->cur_pos;
                    advance_pos(&end, z, n);
                    lp_error(ctx, LP_ERR_ILLEGAL_TOKEN, end,
                             "%u:%u: unrecognized token: \"%.*s\"",
                             ctx->cur_pos.line, ctx->cur_pos.col, n, z);
                    if (ctx->tolerant) {
                        ctx->cur_pos = end;
                        z += n;
                        goto recover;
                    }
                }
                break;
            }
        }

        LpToken token;
        token.z = z;
        token.n = (unsigned int)n;
        token.pos = ctx->cur_pos;

        lp_Parser(parser, tokenType, token, ctx);
        lastTokenParsed = tokenType;
        advance_pos(&ctx->cur_pos, z, n);
        z += n;

        if (ctx->n_errors > 0) {
            if (!ctx->tolerant) break;
recover:
            /* Collect any pending result from before the error.
            ** A statement may have been fully parsed but its reduce
            ** action not yet triggered (waiting for lookahead). */
            if (ctx->result) {
                lp_list_append(ctx, &ctx->stmts, ctx->result);
                ctx->result = NULL;
            }
            /* Error recovery: skip to next semicolon, reset parser.
            ** If the last token parsed was already a semicolon (or the
            ** synthetic end-of-input semi), we are at a statement boundary
            ** and do not need to skip further. */
            if (lastTokenParsed != TK_SEMI) {
                int found_semi = 0;
                while (*z) {
                    int tt;
                    int nn = lp_get_token((const unsigned char *)z, &tt);
                    advance_pos(&ctx->cur_pos, z, nn);
                    z += nn;
                    if (tt == TK_SEMI) { found_semi = 1; break; }
                }
                /* If at end of input without finding a semicolon,
                ** there is nothing more to parse — stop. */
                if (!found_semi) break;
            } else if (*z == 0) {
                /* Already at a semicolon boundary AND end of input */
                break;
            }
            /* Destroy the broken parser and start fresh */
            lp_ParserFree(parser, free);
            parser = lp_ParserAlloc(malloc, ctx);
            if (!parser) return -1;
            lastTokenParsed = TK_SEMI;
            ctx->n_errors = 0;
            ctx->result = NULL;
            ctx->explain = 0;
            ctx->cur_table = NULL;
            ctx->cur_column = NULL;
            ctx->cur_constraint_name = NULL;
        }
    }

    lp_ParserFree(parser, free);

    /* If there's a pending result not yet collected (e.g. input without
    ** trailing semicolon), collect it now. */
    if (ctx->result && ctx->n_errors == 0) {
        lp_list_append(ctx, &ctx->stmts, ctx->result);
        ctx->result = NULL;
    }

    return (ctx->tolerant ? ctx->all_errors.count : ctx->n_errors) > 0 ? -1 : 0;
}

LpNode *lp_parse(const char *sql, arena_t *arena, const char **error_msg) {
    LpParseContext ctx;
    if (lp_parse_internal(sql, arena, &ctx, 0) != 0) {
        if (error_msg) *error_msg = ctx.error_msg;
        return NULL;
    }
    if (error_msg) *error_msg = NULL;
    /* Return first statement for backwards compatibility */
    LpNode *node = ctx.stmts.count > 0 ? ctx.stmts.items[0] : NULL;
    if (node) lp_fix_parents(node);
    return node;
}

LpNodeList *lp_parse_all(const char *sql, arena_t *arena, const char **error_msg) {
    LpParseContext ctx;
    if (lp_parse_internal(sql, arena, &ctx, 0) != 0) {
        if (error_msg) *error_msg = ctx.error_msg;
        return NULL;
    }
    if (error_msg) *error_msg = NULL;
    /* Copy the list into arena so it outlives the stack context */
    LpNodeList *result = (LpNodeList *)arena_alloc(arena, sizeof(LpNodeList));
    if (!result) return NULL;
    *result = ctx.stmts;
    for (int i = 0; i < result->count; i++) {
        lp_fix_parents(result->items[i]);
    }
    return result;
}

LpParseResult *lp_parse_tolerant(const char *sql, arena_t *arena) {
    LpParseContext ctx;
    lp_parse_internal(sql, arena, &ctx, 1);

    LpParseResult *r = (LpParseResult *)arena_zeroalloc(arena, sizeof(LpParseResult));
    if (!r) return NULL;
    r->stmts = ctx.stmts;
    r->errors = ctx.all_errors;
    for (int i = 0; i < r->stmts.count; i++) {
        lp_fix_parents(r->stmts.items[i]);
    }
    return r;
}
