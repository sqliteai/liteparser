/*
** liteparser.h — Public API: AST types, node kinds, parser, visitor, JSON
**
** Self-contained SQL parser producing a visitable AST.
** All AST memory is managed via arena allocation.
*/
#pragma once

#define LITEPARSER_VERSION "1.0.0"

#include <stdio.h>
#include "arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */
typedef struct LpNode     LpNode;
typedef struct LpNodeList LpNodeList;
typedef struct LpVisitor  LpVisitor;

/* ------------------------------------------------------------------ */
/*  Source position                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    unsigned int offset;    /* byte offset from start of SQL string */
    unsigned int line;      /* 1-based line number */
    unsigned int col;       /* 1-based column (byte offset within line) */
} LpSrcPos;

/* ------------------------------------------------------------------ */
/*  Token (matches Lemon grammar token type)                           */
/* ------------------------------------------------------------------ */
typedef struct {
    const char  *z;
    unsigned int n;
    LpSrcPos     pos;       /* source position of token start */
} LpToken;

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

/* Transaction types */
#define LP_TRANS_DEFERRED   0
#define LP_TRANS_IMMEDIATE  1
#define LP_TRANS_EXCLUSIVE  2

/* Conflict resolution */
#define LP_CONFLICT_NONE      0
#define LP_CONFLICT_ROLLBACK  1
#define LP_CONFLICT_ABORT     2
#define LP_CONFLICT_FAIL      3
#define LP_CONFLICT_IGNORE    4
#define LP_CONFLICT_REPLACE   5

/* Sort order */
#define LP_SORT_ASC        0
#define LP_SORT_DESC       1
#define LP_SORT_UNDEFINED (-1)

/* Join types (bitmask) */
#define LP_JOIN_INNER    0x01
#define LP_JOIN_CROSS    0x02
#define LP_JOIN_NATURAL  0x04
#define LP_JOIN_LEFT     0x08
#define LP_JOIN_RIGHT    0x10
#define LP_JOIN_OUTER    0x20
#define LP_JOIN_FULL     0x40

/* Binary operator codes */
typedef enum {
    LP_OP_ADD, LP_OP_SUB, LP_OP_MUL, LP_OP_DIV, LP_OP_MOD,
    LP_OP_AND, LP_OP_OR,
    LP_OP_EQ, LP_OP_NE, LP_OP_LT, LP_OP_LE, LP_OP_GT, LP_OP_GE,
    LP_OP_BITAND, LP_OP_BITOR, LP_OP_LSHIFT, LP_OP_RSHIFT,
    LP_OP_CONCAT,
    LP_OP_IS, LP_OP_ISNOT,
    LP_OP_LIKE, LP_OP_GLOB, LP_OP_MATCH, LP_OP_REGEXP,
    LP_OP_PTR, LP_OP_PTR2
} LpBinOp;

/* Unary operator codes */
typedef enum {
    LP_UOP_MINUS, LP_UOP_PLUS, LP_UOP_NOT, LP_UOP_BITNOT
} LpUnaryOp;

/* ALTER TABLE sub-types */
typedef enum {
    LP_ALTER_RENAME_TABLE,
    LP_ALTER_ADD_COLUMN,
    LP_ALTER_DROP_COLUMN,
    LP_ALTER_RENAME_COLUMN
} LpAlterType;

/* Drop target types */
typedef enum {
    LP_DROP_TABLE,
    LP_DROP_INDEX,
    LP_DROP_VIEW,
    LP_DROP_TRIGGER
} LpDropType;

/* Column constraint types */
typedef enum {
    LP_CCONS_PRIMARY_KEY,
    LP_CCONS_NOT_NULL,
    LP_CCONS_UNIQUE,
    LP_CCONS_CHECK,
    LP_CCONS_DEFAULT,
    LP_CCONS_REFERENCES,
    LP_CCONS_COLLATE,
    LP_CCONS_GENERATED,
    LP_CCONS_NULL
} LpColConsType;

/* Table constraint types */
typedef enum {
    LP_TCONS_PRIMARY_KEY,
    LP_TCONS_UNIQUE,
    LP_TCONS_CHECK,
    LP_TCONS_FOREIGN_KEY
} LpTableConsType;

/* Foreign key actions */
typedef enum {
    LP_FK_NO_ACTION,
    LP_FK_SET_NULL,
    LP_FK_SET_DEFAULT,
    LP_FK_CASCADE,
    LP_FK_RESTRICT
} LpFKAction;

/* Trigger timing */
typedef enum {
    LP_TRIGGER_BEFORE,
    LP_TRIGGER_AFTER,
    LP_TRIGGER_INSTEAD_OF
} LpTriggerTime;

/* Trigger events */
typedef enum {
    LP_TRIGGER_INSERT,
    LP_TRIGGER_UPDATE,
    LP_TRIGGER_DELETE
} LpTriggerEvent;

/* Window frame type */
typedef enum {
    LP_FRAME_ROWS,
    LP_FRAME_RANGE,
    LP_FRAME_GROUPS
} LpFrameType;

/* Frame bound type */
typedef enum {
    LP_BOUND_CURRENT_ROW,
    LP_BOUND_UNBOUNDED_PRECEDING,
    LP_BOUND_UNBOUNDED_FOLLOWING,
    LP_BOUND_PRECEDING,
    LP_BOUND_FOLLOWING
} LpBoundType;

/* Frame exclude */
typedef enum {
    LP_EXCLUDE_NONE,
    LP_EXCLUDE_NO_OTHERS,
    LP_EXCLUDE_CURRENT_ROW,
    LP_EXCLUDE_GROUP,
    LP_EXCLUDE_TIES
} LpExcludeType;

/* CTE materialization hint */
typedef enum {
    LP_MATERIALIZE_ANY,
    LP_MATERIALIZE_YES,
    LP_MATERIALIZE_NO
} LpMaterialized;

/* RAISE type */
typedef enum {
    LP_RAISE_IGNORE,
    LP_RAISE_ROLLBACK,
    LP_RAISE_ABORT,
    LP_RAISE_FAIL
} LpRaiseType;

/* Table options (bitmask) */
#define LP_TBL_WITHOUT_ROWID  0x01
#define LP_TBL_STRICT         0x02

/* Compound select operator */
typedef enum {
    LP_COMPOUND_UNION,
    LP_COMPOUND_UNION_ALL,
    LP_COMPOUND_INTERSECT,
    LP_COMPOUND_EXCEPT
} LpCompoundOp;

/* ------------------------------------------------------------------ */
/*  Node Kind Enum                                                     */
/* ------------------------------------------------------------------ */
typedef enum {
    /* Statements */
    LP_STMT_SELECT,
    LP_STMT_INSERT,
    LP_STMT_UPDATE,
    LP_STMT_DELETE,
    LP_STMT_CREATE_TABLE,
    LP_STMT_CREATE_INDEX,
    LP_STMT_CREATE_VIEW,
    LP_STMT_CREATE_TRIGGER,
    LP_STMT_CREATE_VTABLE,
    LP_STMT_DROP,
    LP_STMT_BEGIN,
    LP_STMT_COMMIT,
    LP_STMT_ROLLBACK,
    LP_STMT_SAVEPOINT,
    LP_STMT_RELEASE,
    LP_STMT_ROLLBACK_TO,
    LP_STMT_PRAGMA,
    LP_STMT_VACUUM,
    LP_STMT_REINDEX,
    LP_STMT_ANALYZE,
    LP_STMT_ATTACH,
    LP_STMT_DETACH,
    LP_STMT_ALTER,
    LP_STMT_EXPLAIN,

    /* Expressions */
    LP_EXPR_LITERAL_INT,
    LP_EXPR_LITERAL_FLOAT,
    LP_EXPR_LITERAL_STRING,
    LP_EXPR_LITERAL_BLOB,
    LP_EXPR_LITERAL_NULL,
    LP_EXPR_LITERAL_BOOL,
    LP_EXPR_COLUMN_REF,
    LP_EXPR_BINARY_OP,
    LP_EXPR_UNARY_OP,
    LP_EXPR_FUNCTION,
    LP_EXPR_CAST,
    LP_EXPR_COLLATE,
    LP_EXPR_BETWEEN,
    LP_EXPR_IN,
    LP_EXPR_EXISTS,
    LP_EXPR_SUBQUERY,
    LP_EXPR_CASE,
    LP_EXPR_RAISE,
    LP_EXPR_VARIABLE,
    LP_EXPR_STAR,
    LP_EXPR_VECTOR,

    /* Clauses / sub-structures */
    LP_COMPOUND_SELECT,
    LP_RESULT_COLUMN,
    LP_FROM_TABLE,
    LP_FROM_SUBQUERY,
    LP_JOIN_CLAUSE,
    LP_ORDER_TERM,
    LP_LIMIT,
    LP_COLUMN_DEF,
    LP_COLUMN_CONSTRAINT,
    LP_TABLE_CONSTRAINT,
    LP_FOREIGN_KEY,
    LP_CTE,
    LP_WITH,
    LP_UPSERT,
    LP_RETURNING,
    LP_WINDOW_DEF,
    LP_WINDOW_FRAME,
    LP_FRAME_BOUND,
    LP_SET_CLAUSE,
    LP_INDEX_COLUMN,
    LP_VALUES_ROW,
    LP_TRIGGER_CMD,

    LP_NODE_KIND_COUNT
} LpNodeKind;

/* ------------------------------------------------------------------ */
/*  Node List                                                          */
/* ------------------------------------------------------------------ */
struct LpNodeList {
    LpNode **items;
    int      count;
    int      capacity;
};

/* ------------------------------------------------------------------ */
/*  AST Node (tagged union)                                            */
/* ------------------------------------------------------------------ */
struct LpNode {
    LpNodeKind kind;
    LpSrcPos   pos;         /* source position of the node's leading token */
    LpNode    *parent;      /* parent node (NULL for root); set by lp_fix_parents */
    union {
        /* ----- Statements ----- */

        struct {
            int           distinct;
            LpNodeList    result_columns;
            LpNode       *from;
            LpNode       *where;
            LpNodeList    group_by;
            LpNode       *having;
            LpNodeList    order_by;
            LpNode       *limit;
            LpNodeList    window_defs;
            LpNode       *with;
        } select;

        struct {
            LpCompoundOp  op;
            LpNode       *left;
            LpNode       *right;
        } compound;

        struct {
            char         *table;
            char         *schema;
            char         *alias;
            int           or_conflict;
            LpNodeList    columns;     /* column name list */
            LpNode       *source;     /* SELECT or VALUES */
            LpNode       *upsert;
            LpNodeList    returning;
        } insert;

        struct {
            char         *table;
            char         *schema;
            char         *alias;
            int           or_conflict;
            LpNodeList    set_clauses;
            LpNode       *from;
            LpNode       *where;
            LpNodeList    order_by;
            LpNode       *limit;
            LpNodeList    returning;
        } update;

        struct {
            char         *table;
            char         *schema;
            char         *alias;
            LpNode       *where;
            LpNodeList    order_by;
            LpNode       *limit;
            LpNodeList    returning;
        } del;

        struct {
            char         *name;
            char         *schema;
            int           if_not_exists;
            int           temp;
            int           options;     /* LP_TBL_WITHOUT_ROWID | LP_TBL_STRICT */
            LpNodeList    columns;
            LpNodeList    constraints;
            LpNode       *as_select;
        } create_table;

        struct {
            char         *name;
            char         *schema;
            char         *table;
            int           if_not_exists;
            int           is_unique;
            LpNodeList    columns;
            LpNode       *where;
        } create_index;

        struct {
            char         *name;
            char         *schema;
            int           if_not_exists;
            int           temp;
            LpNodeList    col_names;   /* optional column names */
            LpNode       *select;
        } create_view;

        struct {
            char           *name;
            char           *schema;
            int             if_not_exists;
            int             temp;
            LpTriggerTime   time;
            LpTriggerEvent  event;
            char           *table_name;
            LpNodeList      update_columns;
            LpNode         *when;
            LpNodeList      body;
        } create_trigger;

        struct {
            char         *name;
            char         *schema;
            int           if_not_exists;
            char         *module;
            char         *module_args;
        } create_vtable;

        struct {
            LpDropType    target;
            char         *name;
            char         *schema;
            int           if_exists;
        } drop;

        struct {
            int           trans_type;  /* LP_TRANS_DEFERRED etc. */
        } begin;

        struct {
            char         *name;
        } savepoint;

        struct {
            char         *name;
            char         *schema;
            char         *value;
            int           is_neg;
        } pragma;

        struct {
            char         *schema;
            LpNode       *into;
        } vacuum;

        struct {
            char         *name;
            char         *schema;
        } reindex;  /* also used for ANALYZE */

        struct {
            LpNode       *filename;
            LpNode       *dbname;
            LpNode       *key;
        } attach;

        struct {
            LpNode       *dbname;
        } detach;

        struct {
            char         *table_name;
            char         *schema;
            LpAlterType   alter_type;
            char         *column_name;
            char         *new_name;
            LpNode       *column_def;
        } alter;

        struct {
            int           is_query_plan;
            LpNode       *stmt;
        } explain;

        /* ----- Expressions ----- */

        struct {
            char         *value;
        } literal;

        struct {
            char         *schema;
            char         *table;
            char         *column;
        } column_ref;

        struct {
            LpBinOp       op;
            LpNode       *left;
            LpNode       *right;
            LpNode       *escape;      /* for LIKE ... ESCAPE */
        } binary;

        struct {
            LpUnaryOp     op;
            LpNode       *operand;
        } unary;

        struct {
            char         *name;
            LpNodeList    args;
            int           distinct;
            int           is_ctime_kw; /* CURRENT_TIME/DATE/TIMESTAMP keyword */
            LpNodeList    order_by;
            LpNode       *filter;
            LpNode       *over;        /* window def or window name ref */
        } function;

        struct {
            LpNode       *expr;
            char         *type_name;
        } cast;

        struct {
            LpNode       *expr;
            char         *collation;
        } collate;

        struct {
            LpNode       *expr;
            LpNode       *low;
            LpNode       *high;
            int           is_not;
        } between;

        struct {
            LpNode       *expr;
            LpNodeList    values;
            LpNode       *select;      /* for IN (SELECT ...) */
            int           is_not;
        } in;

        struct {
            LpNode       *select;
        } exists;

        struct {
            LpNode       *select;
        } subquery;

        struct {
            LpNode       *operand;     /* optional CASE operand */
            LpNodeList    when_exprs;  /* WHEN/THEN pairs: items[0]=when, [1]=then, ... */
            LpNode       *else_expr;
        } case_;

        struct {
            LpRaiseType   type;
            LpNode       *message;
        } raise;

        struct {
            char         *name;
        } variable;

        struct {
            char         *table;       /* for table.*, NULL for bare * */
        } star;

        struct {
            LpNodeList    values;
        } vector;

        /* ----- Clauses / sub-structures ----- */

        struct {
            LpNode       *expr;
            char         *alias;
        } result_column;

        struct {
            char         *name;
            char         *schema;
            char         *alias;
            char         *indexed_by;
            int           not_indexed;
            LpNodeList    func_args;
        } from_table;

        struct {
            LpNode       *select;
            char         *alias;
        } from_subquery;

        struct {
            LpNode       *left;
            LpNode       *right;
            int           join_type;
            LpNode       *on_expr;
            LpNodeList    using_columns;
        } join;

        struct {
            LpNode       *expr;
            int           direction;
            int           nulls;
        } order_term;

        struct {
            LpNode       *count;
            LpNode       *offset;
        } limit;

        struct {
            char         *name;
            char         *type_name;
            LpNodeList    constraints;
        } column_def;

        struct {
            LpColConsType type;
            char         *name;
            LpNode       *expr;
            LpNode       *fk;
            char         *collation;
            int           sort_order;
            int           conflict_action;
            int           is_autoinc;
            int           generated_type;   /* 0=virtual, 1=stored */
        } column_constraint;

        struct {
            LpTableConsType type;
            char           *name;
            LpNodeList      columns;
            LpNode         *expr;
            LpNode         *fk;
            int             conflict_action;
            int             is_autoinc;
        } table_constraint;

        struct {
            char         *table;
            LpNodeList    columns;
            LpFKAction    on_delete;
            LpFKAction    on_update;
            int           deferrable;       /* 0=none, 1=deferred, 2=immediate */
        } foreign_key;

        struct {
            char           *name;
            LpNodeList      columns;
            LpNode         *select;
            LpMaterialized  materialized;
        } cte;

        struct {
            int           recursive;
            LpNodeList    ctes;
        } with;

        struct {
            LpNodeList    conflict_target;
            LpNode       *conflict_where;
            LpNodeList    set_clauses;
            LpNode       *where;
            LpNode       *next;
        } upsert;

        struct {
            LpNodeList    columns;
        } returning;

        struct {
            char         *name;
            char         *base_name;
            LpNodeList    partition_by;
            LpNodeList    order_by;
            LpNode       *frame;
        } window_def;

        struct {
            LpFrameType   type;
            LpNode       *start;
            LpNode       *end;
            LpExcludeType exclude;
        } window_frame;

        struct {
            LpBoundType   type;
            LpNode       *expr;
        } frame_bound;

        struct {
            char         *column;
            LpNodeList    columns;     /* for multi-column set (a,b)=expr */
            LpNode       *expr;
        } set_clause;

        struct {
            LpNode       *expr;
            char         *collation;
            int           sort_order;
        } index_column;

        struct {
            LpNodeList    values;      /* list of expressions in this row */
        } values_row;

        struct {
            LpNode       *stmt;        /* wrapped statement node */
        } trigger_cmd;

    } u;
};

/* ------------------------------------------------------------------ */
/*  Parse diagnostics (for tolerant/IDE parsing)                       */
/* ------------------------------------------------------------------ */

/* Error category codes */
typedef enum {
    LP_ERR_SYNTAX,          /* grammar-level syntax error */
    LP_ERR_ILLEGAL_TOKEN,   /* unrecognized token from lexer */
    LP_ERR_INCOMPLETE,      /* unexpected end of input */
    LP_ERR_STACK_OVERFLOW   /* parser stack overflow */
} LpErrorCode;

/* A single parse error/diagnostic */
typedef struct {
    LpSrcPos    pos;        /* start of error range */
    LpSrcPos    end_pos;    /* end of error range (exclusive) */
    LpErrorCode code;       /* error category */
    const char *message;    /* arena-allocated error message */
} LpError;

/* List of diagnostics */
typedef struct {
    LpError    *items;
    int         count;
    int         capacity;
} LpErrorList;

/* Result of a tolerant parse — contains both statements and errors */
typedef struct {
    LpNodeList  stmts;      /* successfully parsed statements */
    LpErrorList errors;     /* errors encountered (may be non-empty even with stmts) */
} LpParseResult;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
** Parse a SQL string into an AST.
** Returns the root node on success, NULL on error.
** For multi-statement input, returns only the first statement.
** Use lp_parse_all() to get all statements.
** On error, *error_msg points to an arena-allocated error string.
*/
LpNode *lp_parse(const char *sql, arena_t *arena, const char **error_msg);

/*
** Parse a SQL string that may contain multiple semicolon-separated statements.
** Returns an arena-allocated list of statement nodes.
** On error, returns NULL and *error_msg describes the first error.
*/
LpNodeList *lp_parse_all(const char *sql, arena_t *arena, const char **error_msg);

/*
** Tolerant parse: continues past syntax errors for IDE/linter use.
** Skips to the next semicolon after each error and tries to parse
** the remaining statements. Returns all successfully parsed statements
** and all errors encountered.
** Always succeeds (never returns NULL). The caller checks result->errors.count.
*/
LpParseResult *lp_parse_tolerant(const char *sql, arena_t *arena);

/*
** Serialize a tolerant parse result (statements + errors) to JSON.
** Returns an arena-allocated JSON string. Returns NULL on error.
*/
char *lp_parse_result_to_json(LpParseResult *result, arena_t *arena, int pretty);

/*
** Return the string name of an error code (e.g., "syntax", "illegal_token").
*/
const char *lp_error_code_name(LpErrorCode code);

/*
** AST Visitor for depth-first traversal.
** enter() return values:
**   0 = continue (visit children)
**   1 = skip children
**   2 = abort traversal
** leave() return values:
**   0 = continue
**   2 = abort
*/
struct LpVisitor {
    void *user_data;
    int (*enter)(LpVisitor *v, LpNode *node);
    int (*leave)(LpVisitor *v, LpNode *node);
};

void lp_ast_walk(LpNode *root, LpVisitor *visitor);

/*
** Serialize AST to JSON string.
** If pretty != 0, output is indented for readability.
** Returns an arena-allocated string. Returns NULL on error.
*/
char *lp_ast_to_json(LpNode *root, arena_t *arena, int pretty);

/*
** Convert AST back to SQL text.
** Returns an arena-allocated string. Returns NULL on error.
*/
char *lp_ast_to_sql(LpNode *root, arena_t *arena);

/*
** Return the string name for a node kind (e.g., "STMT_SELECT").
*/
const char *lp_node_kind_name(LpNodeKind kind);

/*
** Return the string name for a binary operator.
*/
const char *lp_binop_name(LpBinOp op);

/*
** Return the string name for a unary operator.
*/
const char *lp_unaryop_name(LpUnaryOp op);

/*
** Return a pointer to the source SQL text for a node.
** The returned pointer points into the original SQL string passed to lp_parse().
** *len is set to the length of the token that started this node.
** Returns NULL if position info is unavailable.
*/
const char *lp_node_source(const LpNode *node, const char *sql, unsigned int *len);

/*
** Return the library version string (e.g., "1.0.0").
*/
const char *lp_version(void);

/*
** Count the total number of nodes in the AST subtree rooted at `node`.
** Returns 0 if node is NULL.
*/
int lp_node_count(const LpNode *node);

/*
** Deep structural equality comparison. Returns 1 if `a` and `b` have
** identical kinds and all child nodes/fields match recursively.
** Source positions and parent pointers are ignored.
** Both NULL returns 1. One NULL returns 0.
*/
int lp_node_equal(const LpNode *a, const LpNode *b);

/*
** Set parent pointers throughout an AST. Walks the tree depth-first,
** setting each child's `parent` to its owning node. The root's parent
** is set to NULL.
**
** Called automatically after lp_parse/lp_parse_all/lp_parse_tolerant
** and lp_node_clone. After using the mutation API (lp_list_push, etc.),
** call this to refresh parent pointers.
*/
void lp_fix_parents(LpNode *root);

/* ------------------------------------------------------------------ */
/*  AST Mutation API                                                   */
/* ------------------------------------------------------------------ */

/*
** Allocate a new zero-initialized node of the given kind.
** All fields in the union are zeroed; caller fills them in.
*/
LpNode *lp_node_alloc(arena_t *arena, LpNodeKind kind);

/*
** Duplicate an arena-allocated string. Thin wrapper around arena_strdup.
** Use this when setting string fields on nodes (e.g., table names, aliases).
*/
char *lp_strdup(arena_t *arena, const char *s);

/*
** List mutation: append an item to a node list.
** The list grows using arena-allocated arrays.
*/
void lp_list_push(arena_t *arena, LpNodeList *list, LpNode *item);

/*
** List mutation: insert an item at position `index`.
** Items at index..count-1 are shifted right. Clamps to [0, count].
*/
void lp_list_insert(arena_t *arena, LpNodeList *list, int index, LpNode *item);

/*
** List mutation: replace the item at position `index`.
** Returns the old item, or NULL if index is out of bounds.
*/
LpNode *lp_list_replace(LpNodeList *list, int index, LpNode *new_item);

/*
** List mutation: remove the item at position `index`.
** Items after it are shifted left. Returns the removed item, or NULL.
** Note: the removed node's memory is not freed (arena-managed).
*/
LpNode *lp_list_remove(LpNodeList *list, int index);

/*
** Deep-copy an AST node and all its children into the given arena.
** Useful for moving nodes between arenas or duplicating subtrees.
*/
LpNode *lp_node_clone(arena_t *arena, const LpNode *node);

#ifdef __cplusplus
}
#endif
