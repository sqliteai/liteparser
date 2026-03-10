/*
** liteparser_internal.h — Internal helpers used by parse.y grammar actions.
*/
#pragma once

#include "liteparser.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Growable string buffer (for JSON/SQL serialization)                */
/* ------------------------------------------------------------------ */
typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} LpBuf;

static inline void lp_buf_init(LpBuf *b) {
    b->data = NULL; b->len = 0; b->cap = 0;
}

static inline void lp_buf_ensure(LpBuf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap < 256 ? 256 : b->cap;
        while (nc < b->len + extra + 1) nc *= 2;
        char *p = (char *)realloc(b->data, nc);
        if (!p) return;  /* OOM: leave buffer unchanged */
        b->data = p;
        b->cap = nc;
    }
}

static inline void lp_buf_putc(LpBuf *b, char c) {
    lp_buf_ensure(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static inline void lp_buf_puts(LpBuf *b, const char *s) {
    if (!s) return;
    size_t n = strlen(s);
    lp_buf_ensure(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void lp_buf_printf(LpBuf *b, const char *fmt, ...);

/* Copy buffer contents to arena and free the buffer */
static inline char *lp_buf_finish(LpBuf *b, arena_t *arena) {
    if (!b->data) {
        char *empty = (char *)arena_alloc(arena, 1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    char *result = (char *)arena_alloc(arena, b->len + 1);
    if (result) {
        memcpy(result, b->data, b->len + 1);
    }
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
    return result;
}

/* ------------------------------------------------------------------ */
/*  Parse context (replaces SQLite's Parse struct)                     */
/* ------------------------------------------------------------------ */
typedef struct LpParseContext {
    arena_t    *arena;
    char       *error_msg;
    int         n_errors;
    LpNode     *result;        /* most recently completed statement */
    LpNodeList  stmts;         /* all completed statements (multi-statement support) */
    int         explain;       /* 0=normal, 1=EXPLAIN, 2=EXPLAIN QUERY PLAN */

    /* Source location tracking */
    const char *sql_start;     /* base pointer to original SQL string */
    LpSrcPos    cur_pos;       /* position of current token being processed */

    /* Current CREATE TABLE state */
    LpNode     *cur_table;     /* in-progress CREATE TABLE node */
    LpNode     *cur_column;    /* in-progress column def */
    char       *cur_constraint_name;

    /* Temporary: carries FILTER expr from filter_over grammar production */
    LpNode     *pending_filter;

    /* Tolerant mode (error recovery) */
    int         tolerant;      /* if non-zero, continue past errors */
    LpErrorList all_errors;    /* accumulated errors in tolerant mode */
} LpParseContext;

/* ------------------------------------------------------------------ */
/*  Node allocation                                                    */
/* ------------------------------------------------------------------ */
LpNode *lp_node_new(LpParseContext *ctx, LpNodeKind kind);

/* ------------------------------------------------------------------ */
/*  List operations                                                    */
/* ------------------------------------------------------------------ */
LpNodeList lp_list_new(void);
void       lp_list_append(LpParseContext *ctx, LpNodeList *list, LpNode *item);

/* ------------------------------------------------------------------ */
/*  Token utilities                                                    */
/* ------------------------------------------------------------------ */

/* Compute the exclusive end position of a token (single-line tokens only) */
static inline LpSrcPos lp_token_end(LpToken *tok) {
    LpSrcPos end = tok->pos;
    end.col += tok->n;
    end.offset += tok->n;
    return end;
}

/* Copy a token into an arena-allocated null-terminated string */
char *lp_token_str(LpParseContext *ctx, LpToken *tok);

/* Copy and dequote a token (remove surrounding quotes, unescape) */
char *lp_token_dequote(LpParseContext *ctx, LpToken *tok);

/* ------------------------------------------------------------------ */
/*  Error reporting                                                    */
/* ------------------------------------------------------------------ */
void lp_error(LpParseContext *ctx, LpErrorCode code, LpSrcPos end_pos,
              const char *fmt, ...);

/* ------------------------------------------------------------------ */
/*  Node builder helpers (called from grammar actions)                 */
/* ------------------------------------------------------------------ */

/* --- Transaction / Savepoint --- */
LpNode *lp_make_begin(LpParseContext *ctx, int trans_type);
LpNode *lp_make_commit(LpParseContext *ctx);
LpNode *lp_make_rollback(LpParseContext *ctx);
LpNode *lp_make_savepoint(LpParseContext *ctx, LpToken *name);
LpNode *lp_make_release(LpParseContext *ctx, LpToken *name);
LpNode *lp_make_rollback_to(LpParseContext *ctx, LpToken *name);

/* --- SELECT --- */
LpNode *lp_make_select(LpParseContext *ctx, int distinct,
                        LpNodeList *cols, LpNode *from,
                        LpNode *where, LpNodeList *group_by,
                        LpNode *having, LpNodeList *order_by,
                        LpNode *limit);
LpNode *lp_make_select_with_window(LpParseContext *ctx, int distinct,
                                    LpNodeList *cols, LpNode *from,
                                    LpNode *where, LpNodeList *group_by,
                                    LpNode *having, LpNodeList *window_defs,
                                    LpNodeList *order_by, LpNode *limit);
LpNode *lp_make_compound(LpParseContext *ctx, LpCompoundOp op,
                          LpNode *left, LpNode *right);
LpNode *lp_attach_with(LpParseContext *ctx, LpNode *select, LpNode *with);

/* --- Result columns --- */
LpNode *lp_make_result_column(LpParseContext *ctx, LpNode *expr, LpToken *alias);
LpNode *lp_make_result_star(LpParseContext *ctx);
LpNode *lp_make_result_table_star(LpParseContext *ctx, LpToken *table);

/* --- FROM / JOIN --- */
LpNode *lp_make_from_table(LpParseContext *ctx, LpToken *name, LpToken *schema,
                            LpToken *alias);
LpNode *lp_make_from_subquery(LpParseContext *ctx, LpNode *select, LpToken *alias);
LpNode *lp_make_join(LpParseContext *ctx, LpNode *left, LpNode *right,
                      int join_type, LpNode *on_expr, LpNodeList *using_cols);

/* --- ORDER BY / LIMIT --- */
LpNode *lp_make_order_term(LpParseContext *ctx, LpNode *expr, int direction, int nulls);
LpNode *lp_make_limit(LpParseContext *ctx, LpNode *count, LpNode *offset);

/* --- Expressions --- */
LpNode *lp_make_literal_int(LpParseContext *ctx, LpToken *tok);
LpNode *lp_make_literal_float(LpParseContext *ctx, LpToken *tok);
LpNode *lp_make_literal_string(LpParseContext *ctx, LpToken *tok);
LpNode *lp_make_literal_blob(LpParseContext *ctx, LpToken *tok);
LpNode *lp_make_literal_null(LpParseContext *ctx);
LpNode *lp_make_literal_bool(LpParseContext *ctx, int value);
LpNode *lp_make_column_ref(LpParseContext *ctx, LpToken *name);
LpNode *lp_make_column_ref2(LpParseContext *ctx, LpToken *table, LpToken *column);
LpNode *lp_make_column_ref3(LpParseContext *ctx, LpToken *schema,
                             LpToken *table, LpToken *column);
LpNode *lp_make_binary(LpParseContext *ctx, LpBinOp op, LpNode *left, LpNode *right);
LpNode *lp_make_unary(LpParseContext *ctx, LpUnaryOp op, LpNode *operand);
LpNode *lp_make_function(LpParseContext *ctx, LpToken *name,
                          LpNodeList *args, int distinct);
LpNode *lp_make_function_star(LpParseContext *ctx, LpToken *name);
LpNode *lp_make_cast(LpParseContext *ctx, LpNode *expr, LpToken *type_name);
LpNode *lp_make_collate(LpParseContext *ctx, LpNode *expr, LpToken *collation);
LpNode *lp_make_between(LpParseContext *ctx, LpNode *expr,
                         LpNode *low, LpNode *high, int is_not);
LpNode *lp_make_in_list(LpParseContext *ctx, LpNode *expr,
                         LpNodeList *values, int is_not);
LpNode *lp_make_in_select(LpParseContext *ctx, LpNode *expr,
                           LpNode *select, int is_not);
LpNode *lp_make_exists(LpParseContext *ctx, LpNode *select);
LpNode *lp_make_subquery(LpParseContext *ctx, LpNode *select);
LpNode *lp_make_case(LpParseContext *ctx, LpNode *operand,
                      LpNodeList *when_list, LpNode *else_expr);
LpNode *lp_make_raise(LpParseContext *ctx, LpRaiseType type, LpNode *message);
LpNode *lp_make_variable(LpParseContext *ctx, LpToken *tok);
LpNode *lp_make_star(LpParseContext *ctx);
LpNode *lp_make_table_star(LpParseContext *ctx, LpToken *table);
LpNode *lp_make_like(LpParseContext *ctx, LpNode *expr, LpNode *pattern,
                      LpNode *escape, LpToken *op, int is_not);
LpNode *lp_make_isnull(LpParseContext *ctx, LpNode *expr, int is_notnull);
LpNode *lp_make_is(LpParseContext *ctx, LpNode *left, LpNode *right, int is_not);
LpNode *lp_make_vector(LpParseContext *ctx, LpNodeList *values);

/* --- INSERT --- */
LpNode *lp_make_insert(LpParseContext *ctx, int or_conflict,
                        LpToken *table, LpToken *schema, LpToken *alias,
                        LpNodeList *columns, LpNode *source,
                        LpNode *upsert, LpNodeList *returning);

/* --- UPDATE --- */
LpNode *lp_make_update(LpParseContext *ctx, int or_conflict,
                        LpToken *table, LpToken *schema, LpToken *alias,
                        LpNodeList *set_clauses, LpNode *from,
                        LpNode *where, LpNodeList *order_by, LpNode *limit,
                        LpNodeList *returning);

/* --- DELETE --- */
LpNode *lp_make_delete(LpParseContext *ctx,
                        LpToken *table, LpToken *schema, LpToken *alias,
                        LpNode *where, LpNodeList *order_by, LpNode *limit,
                        LpNodeList *returning);

/* --- SET clause --- */
LpNode *lp_make_set_clause(LpParseContext *ctx, LpToken *column, LpNode *expr);
LpNode *lp_make_set_clause_multi(LpParseContext *ctx, LpNodeList *columns, LpNode *expr);

/* --- CREATE TABLE --- */
void lp_begin_create_table(LpParseContext *ctx, LpToken *name, LpToken *schema,
                            int temp, int if_not_exists);
void lp_end_create_table(LpParseContext *ctx, int options);
void lp_create_table_as(LpParseContext *ctx, LpNode *select);

/* --- Column definitions --- */
void lp_add_column(LpParseContext *ctx, LpToken *name, LpToken *type_name);
void lp_add_column_constraint_pk(LpParseContext *ctx, int sort_order, int conflict, int autoinc);
void lp_add_column_constraint_notnull(LpParseContext *ctx, int conflict);
void lp_add_column_constraint_unique(LpParseContext *ctx, int conflict);
void lp_add_column_constraint_check(LpParseContext *ctx, LpNode *expr);
void lp_add_column_constraint_default(LpParseContext *ctx, LpNode *expr);
void lp_add_column_constraint_default_id(LpParseContext *ctx, LpToken *id);
void lp_add_column_constraint_references(LpParseContext *ctx, LpNode *fk);
void lp_add_column_constraint_collate(LpParseContext *ctx, LpToken *collation);
void lp_add_column_constraint_generated(LpParseContext *ctx, LpNode *expr, int stored);
void lp_add_column_constraint_null(LpParseContext *ctx);
void lp_set_constraint_name(LpParseContext *ctx, LpToken *name);

/* --- Table constraints --- */
void lp_add_table_constraint_pk(LpParseContext *ctx, LpNodeList *columns,
                                 int conflict, int autoinc);
void lp_add_table_constraint_unique(LpParseContext *ctx, LpNodeList *columns,
                                     int conflict);
void lp_add_table_constraint_check(LpParseContext *ctx, LpNode *expr, int conflict);
void lp_add_table_constraint_fk(LpParseContext *ctx, LpNodeList *from_cols,
                                 LpNode *fk, int defer);

/* --- Foreign key --- */
LpNode *lp_make_foreign_key(LpParseContext *ctx, LpToken *table,
                             LpNodeList *columns, int refargs);
int  lp_fk_refargs_combine(int prev, int action, int mask);

/* --- CREATE INDEX --- */
LpNode *lp_make_create_index(LpParseContext *ctx, LpToken *name, LpToken *schema,
                              LpToken *table, LpNodeList *columns,
                              int unique, int if_not_exists, LpNode *where);

/* --- Index column --- */
LpNode *lp_make_index_column(LpParseContext *ctx, LpNode *expr,
                              LpToken *collation, int sort_order);

/* --- CREATE VIEW --- */
LpNode *lp_make_create_view(LpParseContext *ctx, LpToken *name, LpToken *schema,
                             LpNodeList *col_names, LpNode *select,
                             int temp, int if_not_exists);

/* --- DROP --- */
LpNode *lp_make_drop(LpParseContext *ctx, LpDropType target,
                      LpToken *name, LpToken *schema, int if_exists);

/* --- CREATE TRIGGER --- */
LpNode *lp_make_create_trigger(LpParseContext *ctx, LpToken *name, LpToken *schema,
                                int temp, int if_not_exists,
                                int time, int event, LpToken *table,
                                LpNodeList *update_cols, LpNode *when,
                                LpNodeList *body);
LpNode *lp_make_trigger_cmd(LpParseContext *ctx, LpNode *stmt);

/* --- CREATE VIRTUAL TABLE --- */
LpNode *lp_make_create_vtable(LpParseContext *ctx, LpToken *name, LpToken *schema,
                               LpToken *module, int if_not_exists);
void lp_vtable_add_arg(LpParseContext *ctx, LpNode *vtable, LpToken *arg);

/* --- PRAGMA --- */
LpNode *lp_make_pragma(LpParseContext *ctx, LpToken *name, LpToken *schema,
                        LpToken *value, int is_neg);

/* --- VACUUM --- */
LpNode *lp_make_vacuum(LpParseContext *ctx, LpToken *schema, LpNode *into);

/* --- REINDEX / ANALYZE --- */
LpNode *lp_make_reindex(LpParseContext *ctx, LpToken *name, LpToken *schema);
LpNode *lp_make_analyze(LpParseContext *ctx, LpToken *name, LpToken *schema);

/* --- ATTACH / DETACH --- */
LpNode *lp_make_attach(LpParseContext *ctx, LpNode *filename, LpNode *dbname, LpNode *key);
LpNode *lp_make_detach(LpParseContext *ctx, LpNode *dbname);

/* --- ALTER TABLE --- */
LpNode *lp_make_alter_rename(LpParseContext *ctx, LpToken *table, LpToken *schema,
                              LpToken *new_name);
LpNode *lp_make_alter_add_column(LpParseContext *ctx, LpToken *table, LpToken *schema);
LpNode *lp_make_alter_drop_column(LpParseContext *ctx, LpToken *table, LpToken *schema,
                                   LpToken *column);
LpNode *lp_make_alter_rename_column(LpParseContext *ctx, LpToken *table, LpToken *schema,
                                     LpToken *old_name, LpToken *new_name);

/* --- EXPLAIN --- */
LpNode *lp_make_explain(LpParseContext *ctx, int is_query_plan, LpNode *stmt);

/* --- CTE (WITH) --- */
LpNode *lp_make_cte(LpParseContext *ctx, LpToken *name, LpNodeList *columns,
                     LpNode *select, LpMaterialized mat);
LpNode *lp_make_with(LpParseContext *ctx, int recursive, LpNodeList *ctes);

/* --- UPSERT --- */
LpNode *lp_make_upsert(LpParseContext *ctx, LpNodeList *target,
                        LpNode *target_where, LpNodeList *set_clauses,
                        LpNode *where, LpNode *next);

/* --- RETURNING --- */
LpNode *lp_make_returning(LpParseContext *ctx, LpNodeList *columns);

/* --- Window functions --- */
LpNode *lp_make_window_def(LpParseContext *ctx, LpToken *name,
                            LpNodeList *partition_by, LpNodeList *order_by,
                            LpNode *frame, LpToken *base_name);
LpNode *lp_make_window_frame(LpParseContext *ctx, LpFrameType type,
                              LpNode *start, LpNode *end, LpExcludeType exclude);
LpNode *lp_make_frame_bound(LpParseContext *ctx, LpBoundType type, LpNode *expr);
LpNode *lp_make_window_ref(LpParseContext *ctx, LpToken *name);
void    lp_attach_window(LpParseContext *ctx, LpNode *func_node, LpNode *window);
void    lp_attach_filter(LpParseContext *ctx, LpNode *func_node, LpNode *filter_expr);

/* --- VALUES --- */
LpNode *lp_make_values(LpParseContext *ctx, LpNodeList *exprs);
LpNode *lp_make_values_row(LpParseContext *ctx, LpNodeList *exprs);

/* --- Table function args --- */
void lp_from_table_set_indexed(LpParseContext *ctx, LpNode *from, LpToken *idx);
void lp_from_table_set_not_indexed(LpParseContext *ctx, LpNode *from);
void lp_from_table_set_args(LpParseContext *ctx, LpNode *from, LpNodeList *args);

/* --- Helpers for grammar join type parsing --- */
int lp_parse_join_type(LpParseContext *ctx, LpToken *a, LpToken *b, LpToken *c);

/* --- ID list to node list --- */
LpNode *lp_make_id_node(LpParseContext *ctx, LpToken *name);

#ifdef __cplusplus
}
#endif
