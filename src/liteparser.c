/*
** liteparser.c — Main implementation: node builders, visitor, JSON serializer.
*/
//
// (c) 2026 Marco Bambini - SQLite AI
//

#include "liteparser.h"
#include "liteparser_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* ================================================================== */
/*  String buffer                                                      */
/* ================================================================== */

void lp_buf_printf(LpBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n > 0) {
        lp_buf_ensure(b, (size_t)n);
        vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
        b->len += (size_t)n;
    }
    va_end(ap);
}

/* ================================================================== */
/*  Core utilities                                                     */
/* ================================================================== */

LpNode *lp_node_new(LpParseContext *ctx, LpNodeKind kind) {
    LpNode *n = (LpNode *)arena_zeroalloc(ctx->arena, sizeof(LpNode));
    if (n) n->kind = kind;
    /* pos is left zeroed — builders set it from relevant token */
    return n;
}

LpNodeList lp_list_new(void) {
    LpNodeList list;
    list.items = NULL;
    list.count = 0;
    list.capacity = 0;
    return list;
}

void lp_list_append(LpParseContext *ctx, LpNodeList *list, LpNode *item) {
    if (!list || !item) return;
    if (list->count >= list->capacity) {
        int new_cap = list->capacity < 4 ? 4 : list->capacity * 2;
        LpNode **new_items = (LpNode **)arena_alloc(ctx->arena, sizeof(LpNode *) * new_cap);
        if (!new_items) return;
        if (list->items && list->count > 0) {
            memcpy(new_items, list->items, sizeof(LpNode *) * list->count);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = item;
}

/* Set node position from a token */
static inline void node_pos_tok(LpNode *n, LpToken *tok) {
    if (n && tok) n->pos = tok->pos;
}

/* Set node position from a child node */
static inline void node_pos_node(LpNode *n, LpNode *child) {
    if (n && child) n->pos = child->pos;
}

/* Grammar uses nm(X) dbnm(Y): X is first identifier, Y is after DOT.
   For "schema.name": X="schema", Y="name". When Y is present, swap. */
static void resolve_nm_dbnm(LpParseContext *ctx, LpToken *nm, LpToken *dbnm,
                             char **out_name, char **out_schema) {
    if (dbnm && dbnm->z && dbnm->n > 0) {
        *out_name = lp_token_dequote(ctx, dbnm);
        *out_schema = lp_token_dequote(ctx, nm);
    } else {
        *out_name = lp_token_dequote(ctx, nm);
        *out_schema = NULL;
    }
}

char *lp_token_str(LpParseContext *ctx, LpToken *tok) {
    if (!tok || !tok->z || tok->n == 0) return NULL;
    char *s = (char *)arena_alloc(ctx->arena, tok->n + 1);
    if (!s) return NULL;
    memcpy(s, tok->z, tok->n);
    s[tok->n] = '\0';
    return s;
}

char *lp_token_dequote(LpParseContext *ctx, LpToken *tok) {
    if (!tok || !tok->z || tok->n == 0) return NULL;
    char q = tok->z[0];
    if (q != '\'' && q != '"' && q != '`' && q != '[') {
        return lp_token_str(ctx, tok);
    }
    if (q == '[') {
        /* Strip brackets [name] */
        if (tok->n < 2) return lp_token_str(ctx, tok);
        unsigned int inner_len = tok->n - 2;
        char *s = (char *)arena_alloc(ctx->arena, inner_len + 1);
        if (!s) return NULL;
        memcpy(s, tok->z + 1, inner_len);
        s[inner_len] = '\0';
        return s;
    }
    /* For ' " ` — strip quotes and unescape doubled quotes */
    const char *src = tok->z + 1;
    unsigned int src_len = tok->n - 2; /* skip opening and closing quote */
    char *s = (char *)arena_alloc(ctx->arena, src_len + 1);
    if (!s) return NULL;
    unsigned int j = 0;
    for (unsigned int i = 0; i < src_len; i++) {
        s[j++] = src[i];
        if (src[i] == q && i + 1 < src_len && src[i + 1] == q) {
            i++; /* skip the doubled quote */
        }
    }
    s[j] = '\0';
    return s;
}

void lp_error(LpParseContext *ctx, LpErrorCode code, LpSrcPos end_pos,
              const char *fmt, ...) {
    ctx->n_errors++;
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t len = strlen(buf);
    char *msg = (char *)arena_alloc(ctx->arena, len + 1);
    if (msg) memcpy(msg, buf, len + 1);

    /* Store first error message (for non-tolerant callers) */
    if (!ctx->error_msg) ctx->error_msg = msg;

    /* In tolerant mode, also accumulate into the error list */
    if (ctx->tolerant && msg) {
        LpErrorList *el = &ctx->all_errors;
        if (el->count >= el->capacity) {
            int new_cap = el->capacity < 4 ? 4 : el->capacity * 2;
            LpError *new_items = (LpError *)arena_alloc(ctx->arena,
                                    sizeof(LpError) * new_cap);
            if (new_items) {
                if (el->items && el->count > 0)
                    memcpy(new_items, el->items, sizeof(LpError) * el->count);
                el->items = new_items;
                el->capacity = new_cap;
            }
        }
        if (el->count < el->capacity) {
            el->items[el->count].pos = ctx->cur_pos;
            el->items[el->count].end_pos = end_pos;
            el->items[el->count].code = code;
            el->items[el->count].message = msg;
            el->count++;
        }
    }
}

/* ================================================================== */
/*  Node builders — Transaction / Savepoint                            */
/* ================================================================== */

LpNode *lp_make_begin(LpParseContext *ctx, int trans_type) {
    LpNode *n = lp_node_new(ctx, LP_STMT_BEGIN);
    if (n) n->u.begin.trans_type = trans_type;
    return n;
}

LpNode *lp_make_commit(LpParseContext *ctx) {
    return lp_node_new(ctx, LP_STMT_COMMIT);
}

LpNode *lp_make_rollback(LpParseContext *ctx) {
    return lp_node_new(ctx, LP_STMT_ROLLBACK);
}

LpNode *lp_make_savepoint(LpParseContext *ctx, LpToken *name) {
    LpNode *n = lp_node_new(ctx, LP_STMT_SAVEPOINT);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.savepoint.name = lp_token_str(ctx, name);
    return n;
}

LpNode *lp_make_release(LpParseContext *ctx, LpToken *name) {
    LpNode *n = lp_node_new(ctx, LP_STMT_RELEASE);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.savepoint.name = lp_token_str(ctx, name);
    return n;
}

LpNode *lp_make_rollback_to(LpParseContext *ctx, LpToken *name) {
    LpNode *n = lp_node_new(ctx, LP_STMT_ROLLBACK_TO);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.savepoint.name = lp_token_str(ctx, name);
    return n;
}

/* ================================================================== */
/*  Node builders — SELECT                                             */
/* ================================================================== */

LpNode *lp_make_select(LpParseContext *ctx, int distinct,
                        LpNodeList *cols, LpNode *from,
                        LpNode *where, LpNodeList *group_by,
                        LpNode *having, LpNodeList *order_by,
                        LpNode *limit) {
    LpNode *n = lp_node_new(ctx, LP_STMT_SELECT);
    if (!n) return NULL;
    /* Position inherited from first result column */
    if (cols && cols->count > 0 && cols->items[0])
        node_pos_node(n, cols->items[0]);
    n->u.select.distinct = distinct;
    if (cols) n->u.select.result_columns = *cols;
    n->u.select.from = from;
    n->u.select.where = where;
    if (group_by) n->u.select.group_by = *group_by;
    n->u.select.having = having;
    if (order_by) n->u.select.order_by = *order_by;
    n->u.select.limit = limit;
    return n;
}

LpNode *lp_make_select_with_window(LpParseContext *ctx, int distinct,
                                    LpNodeList *cols, LpNode *from,
                                    LpNode *where, LpNodeList *group_by,
                                    LpNode *having, LpNodeList *window_defs,
                                    LpNodeList *order_by, LpNode *limit) {
    LpNode *n = lp_node_new(ctx, LP_STMT_SELECT);
    if (!n) return NULL;
    if (cols && cols->count > 0 && cols->items[0])
        node_pos_node(n, cols->items[0]);
    n->u.select.distinct = distinct;
    if (cols) n->u.select.result_columns = *cols;
    n->u.select.from = from;
    n->u.select.where = where;
    if (group_by) n->u.select.group_by = *group_by;
    n->u.select.having = having;
    if (window_defs) n->u.select.window_defs = *window_defs;
    if (order_by) n->u.select.order_by = *order_by;
    n->u.select.limit = limit;
    return n;
}

LpNode *lp_make_compound(LpParseContext *ctx, LpCompoundOp op,
                          LpNode *left, LpNode *right) {
    LpNode *n = lp_node_new(ctx, LP_COMPOUND_SELECT);
    if (!n) return NULL;
    node_pos_node(n, left);
    n->u.compound.op = op;
    n->u.compound.left = left;
    n->u.compound.right = right;
    return n;
}

LpNode *lp_attach_with(LpParseContext *ctx, LpNode *select, LpNode *with) {
    (void)ctx;
    if (!select || !with) return select;
    if (select->kind == LP_STMT_SELECT) {
        select->u.select.with = with;
    } else if (select->kind == LP_COMPOUND_SELECT) {
        LpNode *cur = select;
        while (cur->kind == LP_COMPOUND_SELECT && cur->u.compound.left) {
            if (cur->u.compound.left->kind == LP_STMT_SELECT) {
                cur->u.compound.left->u.select.with = with;
                break;
            }
            cur = cur->u.compound.left;
        }
    }
    return select;
}

/* ================================================================== */
/*  Node builders — Result columns                                     */
/* ================================================================== */

LpNode *lp_make_result_column(LpParseContext *ctx, LpNode *expr, LpToken *alias) {
    LpNode *n = lp_node_new(ctx, LP_RESULT_COLUMN);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.result_column.expr = expr;
    n->u.result_column.alias = alias ? lp_token_str(ctx, alias) : NULL;
    return n;
}

LpNode *lp_make_result_star(LpParseContext *ctx) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_STAR);
    if (n) n->u.star.table = NULL;
    return n;
}

LpNode *lp_make_result_table_star(LpParseContext *ctx, LpToken *table) {
    LpNode *star = lp_node_new(ctx, LP_EXPR_STAR);
    if (!star) return NULL;
    node_pos_tok(star, table);
    star->u.star.table = lp_token_str(ctx, table);
    LpNode *rc = lp_node_new(ctx, LP_RESULT_COLUMN);
    if (!rc) return NULL;
    node_pos_tok(rc, table);
    rc->u.result_column.expr = star;
    rc->u.result_column.alias = NULL;
    return rc;
}

/* ================================================================== */
/*  Node builders — FROM / JOIN                                        */
/* ================================================================== */

LpNode *lp_make_from_table(LpParseContext *ctx, LpToken *name, LpToken *schema,
                            LpToken *alias) {
    LpNode *n = lp_node_new(ctx, LP_FROM_TABLE);
    if (!n) return NULL;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.from_table.name, &n->u.from_table.schema);
    n->u.from_table.alias = alias ? lp_token_str(ctx, alias) : NULL;
    return n;
}

LpNode *lp_make_from_subquery(LpParseContext *ctx, LpNode *select, LpToken *alias) {
    LpNode *n = lp_node_new(ctx, LP_FROM_SUBQUERY);
    if (!n) return NULL;
    node_pos_node(n, select);
    n->u.from_subquery.select = select;
    n->u.from_subquery.alias = alias ? lp_token_str(ctx, alias) : NULL;
    return n;
}

LpNode *lp_make_join(LpParseContext *ctx, LpNode *left, LpNode *right,
                      int join_type, LpNode *on_expr, LpNodeList *using_cols) {
    LpNode *n = lp_node_new(ctx, LP_JOIN_CLAUSE);
    if (!n) return NULL;
    node_pos_node(n, left);
    n->u.join.left = left;
    n->u.join.right = right;
    n->u.join.join_type = join_type;
    n->u.join.on_expr = on_expr;
    if (using_cols) n->u.join.using_columns = *using_cols;
    return n;
}

/* ================================================================== */
/*  Node builders — ORDER BY / LIMIT                                   */
/* ================================================================== */

LpNode *lp_make_order_term(LpParseContext *ctx, LpNode *expr, int direction, int nulls) {
    LpNode *n = lp_node_new(ctx, LP_ORDER_TERM);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.order_term.expr = expr;
    n->u.order_term.direction = direction;
    n->u.order_term.nulls = nulls;
    return n;
}

LpNode *lp_make_limit(LpParseContext *ctx, LpNode *count, LpNode *offset) {
    LpNode *n = lp_node_new(ctx, LP_LIMIT);
    if (!n) return NULL;
    node_pos_node(n, count);
    n->u.limit.count = count;
    n->u.limit.offset = offset;
    return n;
}

/* ================================================================== */
/*  Node builders — Expressions                                        */
/* ================================================================== */

LpNode *lp_make_literal_int(LpParseContext *ctx, LpToken *tok) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_LITERAL_INT);
    if (!n) return NULL;
    node_pos_tok(n, tok);
    n->u.literal.value = lp_token_str(ctx, tok);
    return n;
}

LpNode *lp_make_literal_float(LpParseContext *ctx, LpToken *tok) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_LITERAL_FLOAT);
    if (!n) return NULL;
    node_pos_tok(n, tok);
    n->u.literal.value = lp_token_str(ctx, tok);
    return n;
}

LpNode *lp_make_literal_string(LpParseContext *ctx, LpToken *tok) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_LITERAL_STRING);
    if (!n) return NULL;
    node_pos_tok(n, tok);
    n->u.literal.value = lp_token_dequote(ctx, tok);
    return n;
}

LpNode *lp_make_literal_blob(LpParseContext *ctx, LpToken *tok) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_LITERAL_BLOB);
    if (!n) return NULL;
    node_pos_tok(n, tok);
    n->u.literal.value = lp_token_str(ctx, tok);
    return n;
}

LpNode *lp_make_literal_null(LpParseContext *ctx) {
    return lp_node_new(ctx, LP_EXPR_LITERAL_NULL);
}

LpNode *lp_make_literal_bool(LpParseContext *ctx, int value) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_LITERAL_BOOL);
    if (n) n->u.literal.value = value ? arena_strdup(ctx->arena, "TRUE") : arena_strdup(ctx->arena, "FALSE");
    return n;
}

LpNode *lp_make_column_ref(LpParseContext *ctx, LpToken *name) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_COLUMN_REF);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.column_ref.column = lp_token_dequote(ctx, name);
    return n;
}

LpNode *lp_make_column_ref2(LpParseContext *ctx, LpToken *table, LpToken *column) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_COLUMN_REF);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.column_ref.table = lp_token_dequote(ctx, table);
    n->u.column_ref.column = lp_token_dequote(ctx, column);
    return n;
}

LpNode *lp_make_column_ref3(LpParseContext *ctx, LpToken *schema,
                             LpToken *table, LpToken *column) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_COLUMN_REF);
    if (!n) return NULL;
    node_pos_tok(n, schema);
    n->u.column_ref.schema = lp_token_dequote(ctx, schema);
    n->u.column_ref.table = lp_token_dequote(ctx, table);
    n->u.column_ref.column = lp_token_dequote(ctx, column);
    return n;
}

LpNode *lp_make_binary(LpParseContext *ctx, LpBinOp op, LpNode *left, LpNode *right) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_BINARY_OP);
    if (!n) return NULL;
    node_pos_node(n, left);
    n->u.binary.op = op;
    n->u.binary.left = left;
    n->u.binary.right = right;
    return n;
}

LpNode *lp_make_unary(LpParseContext *ctx, LpUnaryOp op, LpNode *operand) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_UNARY_OP);
    if (!n) return NULL;
    node_pos_node(n, operand);
    n->u.unary.op = op;
    n->u.unary.operand = operand;
    return n;
}

LpNode *lp_make_function(LpParseContext *ctx, LpToken *name,
                          LpNodeList *args, int distinct) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_FUNCTION);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.function.name = lp_token_str(ctx, name);
    if (args) n->u.function.args = *args;
    n->u.function.distinct = distinct;
    return n;
}

LpNode *lp_make_function_star(LpParseContext *ctx, LpToken *name) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_FUNCTION);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.function.name = lp_token_str(ctx, name);
    /* Add LP_EXPR_STAR as the single argument */
    LpNode *star = lp_node_new(ctx, LP_EXPR_STAR);
    if (star) lp_list_append(ctx, &n->u.function.args, star);
    return n;
}

LpNode *lp_make_cast(LpParseContext *ctx, LpNode *expr, LpToken *type_name) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_CAST);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.cast.expr = expr;
    n->u.cast.type_name = lp_token_str(ctx, type_name);
    return n;
}

LpNode *lp_make_collate(LpParseContext *ctx, LpNode *expr, LpToken *collation) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_COLLATE);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.collate.expr = expr;
    n->u.collate.collation = lp_token_str(ctx, collation);
    return n;
}

LpNode *lp_make_between(LpParseContext *ctx, LpNode *expr,
                         LpNode *low, LpNode *high, int is_not) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_BETWEEN);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.between.expr = expr;
    n->u.between.low = low;
    n->u.between.high = high;
    n->u.between.is_not = is_not;
    return n;
}

LpNode *lp_make_in_list(LpParseContext *ctx, LpNode *expr,
                         LpNodeList *values, int is_not) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_IN);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.in.expr = expr;
    if (values) n->u.in.values = *values;
    n->u.in.is_not = is_not;
    return n;
}

LpNode *lp_make_in_select(LpParseContext *ctx, LpNode *expr,
                           LpNode *select, int is_not) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_IN);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.in.expr = expr;
    n->u.in.select = select;
    n->u.in.is_not = is_not;
    return n;
}

LpNode *lp_make_exists(LpParseContext *ctx, LpNode *select) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_EXISTS);
    if (!n) return NULL;
    node_pos_node(n, select);
    n->u.exists.select = select;
    return n;
}

LpNode *lp_make_subquery(LpParseContext *ctx, LpNode *select) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_SUBQUERY);
    if (!n) return NULL;
    node_pos_node(n, select);
    n->u.subquery.select = select;
    return n;
}

LpNode *lp_make_case(LpParseContext *ctx, LpNode *operand,
                      LpNodeList *when_list, LpNode *else_expr) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_CASE);
    if (!n) return NULL;
    node_pos_node(n, operand); /* NULL operand is ok — pos stays zero */
    n->u.case_.operand = operand;
    if (when_list) n->u.case_.when_exprs = *when_list;
    n->u.case_.else_expr = else_expr;
    return n;
}

LpNode *lp_make_raise(LpParseContext *ctx, LpRaiseType type, LpNode *message) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_RAISE);
    if (!n) return NULL;
    n->u.raise.type = type;
    n->u.raise.message = message;
    return n;
}

LpNode *lp_make_variable(LpParseContext *ctx, LpToken *tok) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_VARIABLE);
    if (!n) return NULL;
    node_pos_tok(n, tok);
    n->u.variable.name = lp_token_str(ctx, tok);
    return n;
}

LpNode *lp_make_star(LpParseContext *ctx) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_STAR);
    if (n) n->u.star.table = NULL;
    return n;
}

LpNode *lp_make_table_star(LpParseContext *ctx, LpToken *table) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_STAR);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.star.table = lp_token_str(ctx, table);
    return n;
}

static int token_ci_eq(LpToken *tok, const char *s) {
    size_t slen = strlen(s);
    if (tok->n != (unsigned int)slen) return 0;
    for (unsigned int i = 0; i < tok->n; i++) {
        if (toupper((unsigned char)tok->z[i]) != toupper((unsigned char)s[i])) return 0;
    }
    return 1;
}

LpNode *lp_make_like(LpParseContext *ctx, LpNode *expr, LpNode *pattern,
                      LpNode *escape, LpToken *op, int is_not) {
    LpBinOp binop = LP_OP_LIKE;
    if (token_ci_eq(op, "GLOB")) binop = LP_OP_GLOB;
    else if (token_ci_eq(op, "MATCH")) binop = LP_OP_MATCH;
    else if (token_ci_eq(op, "REGEXP")) binop = LP_OP_REGEXP;

    LpNode *n = lp_node_new(ctx, LP_EXPR_BINARY_OP);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.binary.op = binop;
    n->u.binary.left = expr;
    n->u.binary.right = pattern;
    n->u.binary.escape = escape;

    if (is_not) {
        LpNode *not_node = lp_node_new(ctx, LP_EXPR_UNARY_OP);
        if (!not_node) return n;
        not_node->u.unary.op = LP_UOP_NOT;
        not_node->u.unary.operand = n;
        return not_node;
    }
    return n;
}

LpNode *lp_make_isnull(LpParseContext *ctx, LpNode *expr, int is_notnull) {
    LpNode *null_lit = lp_node_new(ctx, LP_EXPR_LITERAL_NULL);
    if (!null_lit) return NULL;
    LpNode *n = lp_node_new(ctx, LP_EXPR_BINARY_OP);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.binary.op = is_notnull ? LP_OP_ISNOT : LP_OP_IS;
    n->u.binary.left = expr;
    n->u.binary.right = null_lit;
    return n;
}

LpNode *lp_make_is(LpParseContext *ctx, LpNode *left, LpNode *right, int is_not) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_BINARY_OP);
    if (!n) return NULL;
    node_pos_node(n, left);
    n->u.binary.op = is_not ? LP_OP_ISNOT : LP_OP_IS;
    n->u.binary.left = left;
    n->u.binary.right = right;
    return n;
}

LpNode *lp_make_vector(LpParseContext *ctx, LpNodeList *values) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_VECTOR);
    if (!n) return NULL;
    if (values) n->u.vector.values = *values;
    return n;
}

/* ================================================================== */
/*  Node builders — INSERT                                             */
/* ================================================================== */

LpNode *lp_make_insert(LpParseContext *ctx, int or_conflict,
                        LpToken *table, LpToken *schema, LpToken *alias,
                        LpNodeList *columns, LpNode *source,
                        LpNode *upsert, LpNodeList *returning) {
    LpNode *n = lp_node_new(ctx, LP_STMT_INSERT);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.insert.or_conflict = or_conflict;
    /* Called from xfullname — name/schema already resolved */
    n->u.insert.table = lp_token_dequote(ctx, table);
    n->u.insert.schema = schema ? lp_token_dequote(ctx, schema) : NULL;
    n->u.insert.alias = alias ? lp_token_dequote(ctx, alias) : NULL;
    if (columns) n->u.insert.columns = *columns;
    n->u.insert.source = source;
    n->u.insert.upsert = upsert;
    if (returning) n->u.insert.returning = *returning;
    return n;
}

/* ================================================================== */
/*  Node builders — UPDATE                                             */
/* ================================================================== */

LpNode *lp_make_update(LpParseContext *ctx, int or_conflict,
                        LpToken *table, LpToken *schema, LpToken *alias,
                        LpNodeList *set_clauses, LpNode *from,
                        LpNode *where, LpNodeList *order_by, LpNode *limit,
                        LpNodeList *returning) {
    LpNode *n = lp_node_new(ctx, LP_STMT_UPDATE);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.update.or_conflict = or_conflict;
    /* Called from xfullname — name/schema already resolved */
    n->u.update.table = lp_token_dequote(ctx, table);
    n->u.update.schema = schema ? lp_token_dequote(ctx, schema) : NULL;
    n->u.update.alias = alias ? lp_token_dequote(ctx, alias) : NULL;
    if (set_clauses) n->u.update.set_clauses = *set_clauses;
    n->u.update.from = from;
    n->u.update.where = where;
    if (order_by) n->u.update.order_by = *order_by;
    n->u.update.limit = limit;
    if (returning) n->u.update.returning = *returning;
    return n;
}

/* ================================================================== */
/*  Node builders — DELETE                                             */
/* ================================================================== */

LpNode *lp_make_delete(LpParseContext *ctx,
                        LpToken *table, LpToken *schema, LpToken *alias,
                        LpNode *where, LpNodeList *order_by, LpNode *limit,
                        LpNodeList *returning) {
    LpNode *n = lp_node_new(ctx, LP_STMT_DELETE);
    if (!n) return NULL;
    node_pos_tok(n, table);
    /* Called from xfullname — name/schema already resolved */
    n->u.del.table = lp_token_dequote(ctx, table);
    n->u.del.schema = schema ? lp_token_dequote(ctx, schema) : NULL;
    n->u.del.alias = alias ? lp_token_dequote(ctx, alias) : NULL;
    n->u.del.where = where;
    if (order_by) n->u.del.order_by = *order_by;
    n->u.del.limit = limit;
    if (returning) n->u.del.returning = *returning;
    return n;
}

/* ================================================================== */
/*  Node builders — SET clause                                         */
/* ================================================================== */

LpNode *lp_make_set_clause(LpParseContext *ctx, LpToken *column, LpNode *expr) {
    LpNode *n = lp_node_new(ctx, LP_SET_CLAUSE);
    if (!n) return NULL;
    node_pos_tok(n, column);
    n->u.set_clause.column = lp_token_str(ctx, column);
    n->u.set_clause.expr = expr;
    return n;
}

LpNode *lp_make_set_clause_multi(LpParseContext *ctx, LpNodeList *columns, LpNode *expr) {
    LpNode *n = lp_node_new(ctx, LP_SET_CLAUSE);
    if (!n) return NULL;
    if (columns) n->u.set_clause.columns = *columns;
    n->u.set_clause.expr = expr;
    return n;
}

/* ================================================================== */
/*  Node builders — CREATE TABLE                                       */
/* ================================================================== */

void lp_begin_create_table(LpParseContext *ctx, LpToken *name, LpToken *schema,
                            int temp, int if_not_exists) {
    LpNode *n = lp_node_new(ctx, LP_STMT_CREATE_TABLE);
    if (!n) return;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.create_table.name, &n->u.create_table.schema);
    n->u.create_table.temp = temp;
    n->u.create_table.if_not_exists = if_not_exists;
    ctx->cur_table = n;
    ctx->cur_column = NULL;
}

void lp_end_create_table(LpParseContext *ctx, int options) {
    if (!ctx->cur_table) return;
    if (ctx->cur_table->kind == LP_STMT_CREATE_TABLE) {
        ctx->cur_table->u.create_table.options = options;
    }
    ctx->result = ctx->cur_table;
}

void lp_create_table_as(LpParseContext *ctx, LpNode *select) {
    if (!ctx->cur_table) return;
    ctx->cur_table->u.create_table.as_select = select;
}

/* ================================================================== */
/*  Node builders — Column definitions                                 */
/* ================================================================== */

void lp_add_column(LpParseContext *ctx, LpToken *name, LpToken *type_name) {
    LpNode *col = lp_node_new(ctx, LP_COLUMN_DEF);
    if (!col) return;
    node_pos_tok(col, name);
    col->u.column_def.name = lp_token_dequote(ctx, name);
    col->u.column_def.type_name = type_name ? lp_token_str(ctx, type_name) : NULL;
    if (ctx->cur_table && ctx->cur_table->kind == LP_STMT_CREATE_TABLE) {
        lp_list_append(ctx, &ctx->cur_table->u.create_table.columns, col);
    }
    ctx->cur_column = col;
}

static LpNode *make_column_constraint(LpParseContext *ctx, LpColConsType type) {
    LpNode *c = lp_node_new(ctx, LP_COLUMN_CONSTRAINT);
    if (!c) return NULL;
    c->u.column_constraint.type = type;
    c->u.column_constraint.name = ctx->cur_constraint_name;
    ctx->cur_constraint_name = NULL;
    return c;
}

void lp_add_column_constraint_pk(LpParseContext *ctx, int sort_order, int conflict, int autoinc) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_PRIMARY_KEY);
    if (!c) return;
    c->u.column_constraint.sort_order = sort_order;
    c->u.column_constraint.conflict_action = conflict;
    c->u.column_constraint.is_autoinc = autoinc;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_notnull(LpParseContext *ctx, int conflict) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_NOT_NULL);
    if (!c) return;
    c->u.column_constraint.conflict_action = conflict;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_unique(LpParseContext *ctx, int conflict) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_UNIQUE);
    if (!c) return;
    c->u.column_constraint.conflict_action = conflict;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_check(LpParseContext *ctx, LpNode *expr) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_CHECK);
    if (!c) return;
    c->u.column_constraint.expr = expr;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_default(LpParseContext *ctx, LpNode *expr) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_DEFAULT);
    if (!c) return;
    c->u.column_constraint.expr = expr;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_default_id(LpParseContext *ctx, LpToken *id) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_DEFAULT);
    if (!c) return;
    /* Wrap the id as a column ref expression */
    LpNode *expr = lp_make_column_ref(ctx, id);
    c->u.column_constraint.expr = expr;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_references(LpParseContext *ctx, LpNode *fk) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_REFERENCES);
    if (!c) return;
    c->u.column_constraint.fk = fk;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_collate(LpParseContext *ctx, LpToken *collation) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_COLLATE);
    if (!c) return;
    c->u.column_constraint.collation = lp_token_str(ctx, collation);
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_generated(LpParseContext *ctx, LpNode *expr, int stored) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_GENERATED);
    if (!c) return;
    c->u.column_constraint.expr = expr;
    c->u.column_constraint.generated_type = stored;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_add_column_constraint_null(LpParseContext *ctx) {
    if (!ctx->cur_column) return;
    LpNode *c = make_column_constraint(ctx, LP_CCONS_NULL);
    if (!c) return;
    lp_list_append(ctx, &ctx->cur_column->u.column_def.constraints, c);
}

void lp_set_constraint_name(LpParseContext *ctx, LpToken *name) {
    ctx->cur_constraint_name = lp_token_str(ctx, name);
}

/* ================================================================== */
/*  Node builders — Table constraints                                  */
/* ================================================================== */

static LpNode *make_table_constraint(LpParseContext *ctx, LpTableConsType type) {
    LpNode *c = lp_node_new(ctx, LP_TABLE_CONSTRAINT);
    if (!c) return NULL;
    c->u.table_constraint.type = type;
    c->u.table_constraint.name = ctx->cur_constraint_name;
    ctx->cur_constraint_name = NULL;
    return c;
}

void lp_add_table_constraint_pk(LpParseContext *ctx, LpNodeList *columns,
                                 int conflict, int autoinc) {
    if (!ctx->cur_table) return;
    LpNode *c = make_table_constraint(ctx, LP_TCONS_PRIMARY_KEY);
    if (!c) return;
    if (columns) c->u.table_constraint.columns = *columns;
    c->u.table_constraint.conflict_action = conflict;
    c->u.table_constraint.is_autoinc = autoinc;
    lp_list_append(ctx, &ctx->cur_table->u.create_table.constraints, c);
}

void lp_add_table_constraint_unique(LpParseContext *ctx, LpNodeList *columns,
                                     int conflict) {
    if (!ctx->cur_table) return;
    LpNode *c = make_table_constraint(ctx, LP_TCONS_UNIQUE);
    if (!c) return;
    if (columns) c->u.table_constraint.columns = *columns;
    c->u.table_constraint.conflict_action = conflict;
    lp_list_append(ctx, &ctx->cur_table->u.create_table.constraints, c);
}

void lp_add_table_constraint_check(LpParseContext *ctx, LpNode *expr, int conflict) {
    if (!ctx->cur_table) return;
    LpNode *c = make_table_constraint(ctx, LP_TCONS_CHECK);
    if (!c) return;
    c->u.table_constraint.expr = expr;
    c->u.table_constraint.conflict_action = conflict;
    lp_list_append(ctx, &ctx->cur_table->u.create_table.constraints, c);
}

void lp_add_table_constraint_fk(LpParseContext *ctx, LpNodeList *from_cols,
                                 LpNode *fk, int defer) {
    if (!ctx->cur_table) return;
    LpNode *c = make_table_constraint(ctx, LP_TCONS_FOREIGN_KEY);
    if (!c) return;
    if (from_cols) c->u.table_constraint.columns = *from_cols;
    c->u.table_constraint.fk = fk;
    if (fk && fk->kind == LP_FOREIGN_KEY) {
        fk->u.foreign_key.deferrable = defer;
    }
    lp_list_append(ctx, &ctx->cur_table->u.create_table.constraints, c);
}

/* ================================================================== */
/*  Node builders — Foreign key                                        */
/* ================================================================== */

LpNode *lp_make_foreign_key(LpParseContext *ctx, LpToken *table,
                             LpNodeList *columns, int refargs) {
    LpNode *n = lp_node_new(ctx, LP_FOREIGN_KEY);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.foreign_key.table = lp_token_str(ctx, table);
    if (columns) n->u.foreign_key.columns = *columns;
    /* Decode refargs: bits 0-7 = on_delete, bits 8-15 = on_update */
    n->u.foreign_key.on_delete = (LpFKAction)(refargs & 0xff);
    n->u.foreign_key.on_update = (LpFKAction)((refargs >> 8) & 0xff);
    return n;
}

int lp_fk_refargs_combine(int prev, int action, int mask) {
    return (prev & ~mask) | action;
}

/* ================================================================== */
/*  Node builders — CREATE INDEX                                       */
/* ================================================================== */

LpNode *lp_make_create_index(LpParseContext *ctx, LpToken *name, LpToken *schema,
                              LpToken *table, LpNodeList *columns,
                              int unique, int if_not_exists, LpNode *where) {
    LpNode *n = lp_node_new(ctx, LP_STMT_CREATE_INDEX);
    if (!n) return NULL;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.create_index.name, &n->u.create_index.schema);
    n->u.create_index.table = lp_token_str(ctx, table);
    if (columns) n->u.create_index.columns = *columns;
    n->u.create_index.is_unique = unique;
    n->u.create_index.if_not_exists = if_not_exists;
    n->u.create_index.where = where;
    return n;
}

/* ================================================================== */
/*  Node builders — Index column                                       */
/* ================================================================== */

LpNode *lp_make_index_column(LpParseContext *ctx, LpNode *expr,
                              LpToken *collation, int sort_order) {
    LpNode *n = lp_node_new(ctx, LP_INDEX_COLUMN);
    if (!n) return NULL;
    node_pos_node(n, expr);
    n->u.index_column.expr = expr;
    n->u.index_column.collation = collation ? lp_token_str(ctx, collation) : NULL;
    n->u.index_column.sort_order = sort_order;
    return n;
}

/* ================================================================== */
/*  Node builders — CREATE VIEW                                        */
/* ================================================================== */

LpNode *lp_make_create_view(LpParseContext *ctx, LpToken *name, LpToken *schema,
                             LpNodeList *col_names, LpNode *select,
                             int temp, int if_not_exists) {
    LpNode *n = lp_node_new(ctx, LP_STMT_CREATE_VIEW);
    if (!n) return NULL;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.create_view.name, &n->u.create_view.schema);
    if (col_names) n->u.create_view.col_names = *col_names;
    n->u.create_view.select = select;
    n->u.create_view.temp = temp;
    n->u.create_view.if_not_exists = if_not_exists;
    return n;
}

/* ================================================================== */
/*  Node builders — DROP                                               */
/* ================================================================== */

LpNode *lp_make_drop(LpParseContext *ctx, LpDropType target,
                      LpToken *name, LpToken *schema, int if_exists) {
    LpNode *n = lp_node_new(ctx, LP_STMT_DROP);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.drop.target = target;
    /* Called from fullname — name/schema already resolved */
    n->u.drop.name = lp_token_dequote(ctx, name);
    n->u.drop.schema = schema ? lp_token_dequote(ctx, schema) : NULL;
    n->u.drop.if_exists = if_exists;
    return n;
}

/* ================================================================== */
/*  Node builders — CREATE TRIGGER                                     */
/* ================================================================== */

LpNode *lp_make_create_trigger(LpParseContext *ctx, LpToken *name, LpToken *schema,
                                int temp, int if_not_exists,
                                int time, int event, LpToken *table,
                                LpNodeList *update_cols, LpNode *when,
                                LpNodeList *body) {
    LpNode *n = lp_node_new(ctx, LP_STMT_CREATE_TRIGGER);
    if (!n) return NULL;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.create_trigger.name, &n->u.create_trigger.schema);
    n->u.create_trigger.temp = temp;
    n->u.create_trigger.if_not_exists = if_not_exists;
    n->u.create_trigger.time = (LpTriggerTime)time;
    n->u.create_trigger.event = (LpTriggerEvent)event;
    n->u.create_trigger.table_name = lp_token_str(ctx, table);
    if (update_cols) n->u.create_trigger.update_columns = *update_cols;
    n->u.create_trigger.when = when;
    if (body) n->u.create_trigger.body = *body;
    return n;
}

LpNode *lp_make_trigger_cmd(LpParseContext *ctx, LpNode *stmt) {
    LpNode *n = lp_node_new(ctx, LP_TRIGGER_CMD);
    if (!n) return NULL;
    node_pos_node(n, stmt);
    n->u.trigger_cmd.stmt = stmt;
    return n;
}

/* ================================================================== */
/*  Node builders — CREATE VIRTUAL TABLE                               */
/* ================================================================== */

LpNode *lp_make_create_vtable(LpParseContext *ctx, LpToken *name, LpToken *schema,
                               LpToken *module, int if_not_exists) {
    LpNode *n = lp_node_new(ctx, LP_STMT_CREATE_VTABLE);
    if (!n) return NULL;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.create_vtable.name, &n->u.create_vtable.schema);
    n->u.create_vtable.if_not_exists = if_not_exists;
    n->u.create_vtable.module = lp_token_str(ctx, module);
    return n;
}

void lp_vtable_add_arg(LpParseContext *ctx, LpNode *vtable, LpToken *arg) {
    if (!vtable || vtable->kind != LP_STMT_CREATE_VTABLE) return;
    char *arg_str = lp_token_str(ctx, arg);
    if (!arg_str) return;
    if (vtable->u.create_vtable.module_args) {
        /* Append with comma separator */
        size_t old_len = strlen(vtable->u.create_vtable.module_args);
        size_t arg_len = strlen(arg_str);
        char *new_args = (char *)arena_alloc(ctx->arena, old_len + arg_len + 3);
        if (!new_args) return;
        memcpy(new_args, vtable->u.create_vtable.module_args, old_len);
        new_args[old_len] = ',';
        new_args[old_len + 1] = ' ';
        memcpy(new_args + old_len + 2, arg_str, arg_len);
        new_args[old_len + arg_len + 2] = '\0';
        vtable->u.create_vtable.module_args = new_args;
    } else {
        vtable->u.create_vtable.module_args = arg_str;
    }
}

/* ================================================================== */
/*  Node builders — PRAGMA                                             */
/* ================================================================== */

LpNode *lp_make_pragma(LpParseContext *ctx, LpToken *name, LpToken *schema,
                        LpToken *value, int is_neg) {
    LpNode *n = lp_node_new(ctx, LP_STMT_PRAGMA);
    if (!n) return NULL;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.pragma.name, &n->u.pragma.schema);
    n->u.pragma.value = value ? lp_token_str(ctx, value) : NULL;
    n->u.pragma.is_neg = is_neg;
    return n;
}

/* ================================================================== */
/*  Node builders — VACUUM                                             */
/* ================================================================== */

LpNode *lp_make_vacuum(LpParseContext *ctx, LpToken *schema, LpNode *into) {
    LpNode *n = lp_node_new(ctx, LP_STMT_VACUUM);
    if (!n) return NULL;
    node_pos_tok(n, schema);
    n->u.vacuum.schema = schema ? lp_token_str(ctx, schema) : NULL;
    n->u.vacuum.into = into;
    return n;
}

/* ================================================================== */
/*  Node builders — REINDEX / ANALYZE                                  */
/* ================================================================== */

LpNode *lp_make_reindex(LpParseContext *ctx, LpToken *name, LpToken *schema) {
    LpNode *n = lp_node_new(ctx, LP_STMT_REINDEX);
    if (!n) return NULL;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.reindex.name, &n->u.reindex.schema);
    return n;
}

LpNode *lp_make_analyze(LpParseContext *ctx, LpToken *name, LpToken *schema) {
    LpNode *n = lp_node_new(ctx, LP_STMT_ANALYZE);
    if (!n) return NULL;
    node_pos_tok(n, name);
    resolve_nm_dbnm(ctx, name, schema, &n->u.reindex.name, &n->u.reindex.schema);
    return n;
}

/* ================================================================== */
/*  Node builders — ATTACH / DETACH                                    */
/* ================================================================== */

LpNode *lp_make_attach(LpParseContext *ctx, LpNode *filename, LpNode *dbname, LpNode *key) {
    LpNode *n = lp_node_new(ctx, LP_STMT_ATTACH);
    if (!n) return NULL;
    node_pos_node(n, filename);
    n->u.attach.filename = filename;
    n->u.attach.dbname = dbname;
    n->u.attach.key = key;
    return n;
}

LpNode *lp_make_detach(LpParseContext *ctx, LpNode *dbname) {
    LpNode *n = lp_node_new(ctx, LP_STMT_DETACH);
    if (!n) return NULL;
    node_pos_node(n, dbname);
    n->u.detach.dbname = dbname;
    return n;
}

/* ================================================================== */
/*  Node builders — ALTER TABLE                                        */
/* ================================================================== */

LpNode *lp_make_alter_rename(LpParseContext *ctx, LpToken *table, LpToken *schema,
                              LpToken *new_name) {
    LpNode *n = lp_node_new(ctx, LP_STMT_ALTER);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.alter.alter_type = LP_ALTER_RENAME_TABLE;
    /* fullname already resolves name/schema correctly */
    n->u.alter.table_name = lp_token_dequote(ctx, table);
    n->u.alter.schema = schema ? lp_token_dequote(ctx, schema) : NULL;
    n->u.alter.new_name = lp_token_dequote(ctx, new_name);
    return n;
}

LpNode *lp_make_alter_add_column(LpParseContext *ctx, LpToken *table, LpToken *schema) {
    LpNode *n = lp_node_new(ctx, LP_STMT_ALTER);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.alter.alter_type = LP_ALTER_ADD_COLUMN;
    n->u.alter.table_name = lp_token_dequote(ctx, table);
    n->u.alter.schema = schema ? lp_token_dequote(ctx, schema) : NULL;
    n->u.alter.column_def = ctx->cur_column;
    return n;
}

LpNode *lp_make_alter_drop_column(LpParseContext *ctx, LpToken *table, LpToken *schema,
                                   LpToken *column) {
    LpNode *n = lp_node_new(ctx, LP_STMT_ALTER);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.alter.alter_type = LP_ALTER_DROP_COLUMN;
    n->u.alter.table_name = lp_token_dequote(ctx, table);
    n->u.alter.schema = schema ? lp_token_dequote(ctx, schema) : NULL;
    n->u.alter.column_name = lp_token_dequote(ctx, column);
    return n;
}

LpNode *lp_make_alter_rename_column(LpParseContext *ctx, LpToken *table, LpToken *schema,
                                     LpToken *old_name, LpToken *new_name) {
    LpNode *n = lp_node_new(ctx, LP_STMT_ALTER);
    if (!n) return NULL;
    node_pos_tok(n, table);
    n->u.alter.alter_type = LP_ALTER_RENAME_COLUMN;
    n->u.alter.table_name = lp_token_dequote(ctx, table);
    n->u.alter.schema = schema ? lp_token_dequote(ctx, schema) : NULL;
    n->u.alter.column_name = lp_token_dequote(ctx, old_name);
    n->u.alter.new_name = lp_token_dequote(ctx, new_name);
    return n;
}

/* ================================================================== */
/*  Node builders — EXPLAIN                                            */
/* ================================================================== */

LpNode *lp_make_explain(LpParseContext *ctx, int is_query_plan, LpNode *stmt) {
    LpNode *n = lp_node_new(ctx, LP_STMT_EXPLAIN);
    if (!n) return NULL;
    node_pos_node(n, stmt);
    n->u.explain.is_query_plan = is_query_plan;
    n->u.explain.stmt = stmt;
    return n;
}

/* ================================================================== */
/*  Node builders — CTE (WITH)                                         */
/* ================================================================== */

LpNode *lp_make_cte(LpParseContext *ctx, LpToken *name, LpNodeList *columns,
                     LpNode *select, LpMaterialized mat) {
    LpNode *n = lp_node_new(ctx, LP_CTE);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.cte.name = lp_token_str(ctx, name);
    if (columns) n->u.cte.columns = *columns;
    n->u.cte.select = select;
    n->u.cte.materialized = mat;
    return n;
}

LpNode *lp_make_with(LpParseContext *ctx, int recursive, LpNodeList *ctes) {
    LpNode *n = lp_node_new(ctx, LP_WITH);
    if (!n) return NULL;
    n->u.with.recursive = recursive;
    if (ctes) n->u.with.ctes = *ctes;
    return n;
}

/* ================================================================== */
/*  Node builders — UPSERT                                             */
/* ================================================================== */

LpNode *lp_make_upsert(LpParseContext *ctx, LpNodeList *target,
                        LpNode *target_where, LpNodeList *set_clauses,
                        LpNode *where, LpNode *next) {
    LpNode *n = lp_node_new(ctx, LP_UPSERT);
    if (!n) return NULL;
    if (target) n->u.upsert.conflict_target = *target;
    n->u.upsert.conflict_where = target_where;
    if (set_clauses) n->u.upsert.set_clauses = *set_clauses;
    n->u.upsert.where = where;
    n->u.upsert.next = next;
    return n;
}

/* ================================================================== */
/*  Node builders — RETURNING                                          */
/* ================================================================== */

LpNode *lp_make_returning(LpParseContext *ctx, LpNodeList *columns) {
    LpNode *n = lp_node_new(ctx, LP_RETURNING);
    if (!n) return NULL;
    if (columns) n->u.returning.columns = *columns;
    return n;
}

/* ================================================================== */
/*  Node builders — Window functions                                   */
/* ================================================================== */

LpNode *lp_make_window_def(LpParseContext *ctx, LpToken *name,
                            LpNodeList *partition_by, LpNodeList *order_by,
                            LpNode *frame, LpToken *base_name) {
    LpNode *n = lp_node_new(ctx, LP_WINDOW_DEF);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.window_def.name = name ? lp_token_str(ctx, name) : NULL;
    n->u.window_def.base_name = base_name ? lp_token_str(ctx, base_name) : NULL;
    if (partition_by) n->u.window_def.partition_by = *partition_by;
    if (order_by) n->u.window_def.order_by = *order_by;
    n->u.window_def.frame = frame;
    return n;
}

LpNode *lp_make_window_frame(LpParseContext *ctx, LpFrameType type,
                              LpNode *start, LpNode *end, LpExcludeType exclude) {
    LpNode *n = lp_node_new(ctx, LP_WINDOW_FRAME);
    if (!n) return NULL;
    node_pos_node(n, start);
    n->u.window_frame.type = type;
    n->u.window_frame.start = start;
    n->u.window_frame.end = end;
    n->u.window_frame.exclude = exclude;
    return n;
}

LpNode *lp_make_frame_bound(LpParseContext *ctx, LpBoundType type, LpNode *expr) {
    LpNode *n = lp_node_new(ctx, LP_FRAME_BOUND);
    if (!n) return NULL;
    node_pos_node(n, expr); /* expr can be NULL for CURRENT ROW etc */
    n->u.frame_bound.type = type;
    n->u.frame_bound.expr = expr;
    return n;
}

LpNode *lp_make_window_ref(LpParseContext *ctx, LpToken *name) {
    LpNode *n = lp_node_new(ctx, LP_WINDOW_DEF);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.window_def.name = lp_token_str(ctx, name);
    return n;
}

void lp_attach_window(LpParseContext *ctx, LpNode *func_node, LpNode *window) {
    (void)ctx;
    if (!func_node || func_node->kind != LP_EXPR_FUNCTION) return;
    func_node->u.function.over = window;
}

void lp_attach_filter(LpParseContext *ctx, LpNode *func_node, LpNode *filter_expr) {
    (void)ctx;
    if (!func_node || func_node->kind != LP_EXPR_FUNCTION) return;
    func_node->u.function.filter = filter_expr;
}

/* ================================================================== */
/*  Node builders — VALUES                                             */
/* ================================================================== */

LpNode *lp_make_values(LpParseContext *ctx, LpNodeList *exprs) {
    LpNode *n = lp_node_new(ctx, LP_STMT_SELECT);
    if (!n) return NULL;
    /* Store values rows as result_columns */
    if (exprs) n->u.select.result_columns = *exprs;
    return n;
}

LpNode *lp_make_values_row(LpParseContext *ctx, LpNodeList *exprs) {
    LpNode *n = lp_node_new(ctx, LP_VALUES_ROW);
    if (!n) return NULL;
    if (exprs) n->u.values_row.values = *exprs;
    return n;
}

/* ================================================================== */
/*  FROM table helpers                                                 */
/* ================================================================== */

void lp_from_table_set_indexed(LpParseContext *ctx, LpNode *from, LpToken *idx) {
    (void)ctx;
    if (!from || from->kind != LP_FROM_TABLE) return;
    from->u.from_table.indexed_by = lp_token_str(ctx, idx);
}

void lp_from_table_set_not_indexed(LpParseContext *ctx, LpNode *from) {
    (void)ctx;
    if (!from || from->kind != LP_FROM_TABLE) return;
    from->u.from_table.not_indexed = 1;
}

void lp_from_table_set_args(LpParseContext *ctx, LpNode *from, LpNodeList *args) {
    (void)ctx;
    if (!from || from->kind != LP_FROM_TABLE) return;
    if (args) from->u.from_table.func_args = *args;
}

/* ================================================================== */
/*  Join type parsing                                                  */
/* ================================================================== */

int lp_parse_join_type(LpParseContext *ctx, LpToken *a, LpToken *b, LpToken *c) {
    (void)ctx;
    int jt = 0;
    LpToken *tokens[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        if (!tokens[i] || tokens[i]->n == 0) continue;
        if (token_ci_eq(tokens[i], "INNER"))    jt |= LP_JOIN_INNER;
        else if (token_ci_eq(tokens[i], "CROSS"))    jt |= LP_JOIN_CROSS;
        else if (token_ci_eq(tokens[i], "NATURAL"))  jt |= LP_JOIN_NATURAL;
        else if (token_ci_eq(tokens[i], "LEFT"))     jt |= LP_JOIN_LEFT;
        else if (token_ci_eq(tokens[i], "RIGHT"))    jt |= LP_JOIN_RIGHT;
        else if (token_ci_eq(tokens[i], "FULL"))     jt |= LP_JOIN_FULL;
        else if (token_ci_eq(tokens[i], "OUTER"))    jt |= LP_JOIN_OUTER;
        else if (token_ci_eq(tokens[i], ","))        jt |= LP_JOIN_INNER;
    }
    /* If no explicit type set, default to INNER */
    if (jt == 0 || jt == LP_JOIN_NATURAL) {
        jt |= LP_JOIN_INNER;
    }
    return jt;
}

/* ================================================================== */
/*  ID node                                                            */
/* ================================================================== */

LpNode *lp_make_id_node(LpParseContext *ctx, LpToken *name) {
    LpNode *n = lp_node_new(ctx, LP_EXPR_COLUMN_REF);
    if (!n) return NULL;
    node_pos_tok(n, name);
    n->u.column_ref.column = lp_token_str(ctx, name);
    return n;
}

/* ================================================================== */
/*  Name functions                                                     */
/* ================================================================== */

const char *lp_node_kind_name(LpNodeKind kind) {
    switch (kind) {
        case LP_STMT_SELECT:         return "STMT_SELECT";
        case LP_STMT_INSERT:         return "STMT_INSERT";
        case LP_STMT_UPDATE:         return "STMT_UPDATE";
        case LP_STMT_DELETE:         return "STMT_DELETE";
        case LP_STMT_CREATE_TABLE:   return "STMT_CREATE_TABLE";
        case LP_STMT_CREATE_INDEX:   return "STMT_CREATE_INDEX";
        case LP_STMT_CREATE_VIEW:    return "STMT_CREATE_VIEW";
        case LP_STMT_CREATE_TRIGGER: return "STMT_CREATE_TRIGGER";
        case LP_STMT_CREATE_VTABLE:  return "STMT_CREATE_VTABLE";
        case LP_STMT_DROP:           return "STMT_DROP";
        case LP_STMT_BEGIN:          return "STMT_BEGIN";
        case LP_STMT_COMMIT:         return "STMT_COMMIT";
        case LP_STMT_ROLLBACK:       return "STMT_ROLLBACK";
        case LP_STMT_SAVEPOINT:      return "STMT_SAVEPOINT";
        case LP_STMT_RELEASE:        return "STMT_RELEASE";
        case LP_STMT_ROLLBACK_TO:    return "STMT_ROLLBACK_TO";
        case LP_STMT_PRAGMA:         return "STMT_PRAGMA";
        case LP_STMT_VACUUM:         return "STMT_VACUUM";
        case LP_STMT_REINDEX:        return "STMT_REINDEX";
        case LP_STMT_ANALYZE:        return "STMT_ANALYZE";
        case LP_STMT_ATTACH:         return "STMT_ATTACH";
        case LP_STMT_DETACH:         return "STMT_DETACH";
        case LP_STMT_ALTER:          return "STMT_ALTER";
        case LP_STMT_EXPLAIN:        return "STMT_EXPLAIN";
        case LP_EXPR_LITERAL_INT:    return "EXPR_LITERAL_INT";
        case LP_EXPR_LITERAL_FLOAT:  return "EXPR_LITERAL_FLOAT";
        case LP_EXPR_LITERAL_STRING: return "EXPR_LITERAL_STRING";
        case LP_EXPR_LITERAL_BLOB:   return "EXPR_LITERAL_BLOB";
        case LP_EXPR_LITERAL_NULL:   return "EXPR_LITERAL_NULL";
        case LP_EXPR_LITERAL_BOOL:   return "EXPR_LITERAL_BOOL";
        case LP_EXPR_COLUMN_REF:     return "EXPR_COLUMN_REF";
        case LP_EXPR_BINARY_OP:      return "EXPR_BINARY_OP";
        case LP_EXPR_UNARY_OP:       return "EXPR_UNARY_OP";
        case LP_EXPR_FUNCTION:       return "EXPR_FUNCTION";
        case LP_EXPR_CAST:           return "EXPR_CAST";
        case LP_EXPR_COLLATE:        return "EXPR_COLLATE";
        case LP_EXPR_BETWEEN:        return "EXPR_BETWEEN";
        case LP_EXPR_IN:             return "EXPR_IN";
        case LP_EXPR_EXISTS:         return "EXPR_EXISTS";
        case LP_EXPR_SUBQUERY:       return "EXPR_SUBQUERY";
        case LP_EXPR_CASE:           return "EXPR_CASE";
        case LP_EXPR_RAISE:          return "EXPR_RAISE";
        case LP_EXPR_VARIABLE:       return "EXPR_VARIABLE";
        case LP_EXPR_STAR:           return "EXPR_STAR";
        case LP_EXPR_VECTOR:         return "EXPR_VECTOR";
        case LP_COMPOUND_SELECT:     return "COMPOUND_SELECT";
        case LP_RESULT_COLUMN:       return "RESULT_COLUMN";
        case LP_FROM_TABLE:          return "FROM_TABLE";
        case LP_FROM_SUBQUERY:       return "FROM_SUBQUERY";
        case LP_JOIN_CLAUSE:         return "JOIN_CLAUSE";
        case LP_ORDER_TERM:          return "ORDER_TERM";
        case LP_LIMIT:               return "LIMIT";
        case LP_COLUMN_DEF:          return "COLUMN_DEF";
        case LP_COLUMN_CONSTRAINT:   return "COLUMN_CONSTRAINT";
        case LP_TABLE_CONSTRAINT:    return "TABLE_CONSTRAINT";
        case LP_FOREIGN_KEY:         return "FOREIGN_KEY";
        case LP_CTE:                 return "CTE";
        case LP_WITH:                return "WITH";
        case LP_UPSERT:              return "UPSERT";
        case LP_RETURNING:           return "RETURNING";
        case LP_WINDOW_DEF:          return "WINDOW_DEF";
        case LP_WINDOW_FRAME:        return "WINDOW_FRAME";
        case LP_FRAME_BOUND:         return "FRAME_BOUND";
        case LP_SET_CLAUSE:          return "SET_CLAUSE";
        case LP_INDEX_COLUMN:        return "INDEX_COLUMN";
        case LP_VALUES_ROW:          return "VALUES_ROW";
        case LP_TRIGGER_CMD:         return "TRIGGER_CMD";
        case LP_NODE_KIND_COUNT:     return "NODE_KIND_COUNT";
    }
    return "UNKNOWN";
}

const char *lp_binop_name(LpBinOp op) {
    switch (op) {
        case LP_OP_ADD:     return "+";
        case LP_OP_SUB:     return "-";
        case LP_OP_MUL:     return "*";
        case LP_OP_DIV:     return "/";
        case LP_OP_MOD:     return "%";
        case LP_OP_AND:     return "AND";
        case LP_OP_OR:      return "OR";
        case LP_OP_EQ:      return "=";
        case LP_OP_NE:      return "!=";
        case LP_OP_LT:      return "<";
        case LP_OP_LE:      return "<=";
        case LP_OP_GT:      return ">";
        case LP_OP_GE:      return ">=";
        case LP_OP_BITAND:  return "&";
        case LP_OP_BITOR:   return "|";
        case LP_OP_LSHIFT:  return "<<";
        case LP_OP_RSHIFT:  return ">>";
        case LP_OP_CONCAT:  return "||";
        case LP_OP_IS:      return "IS";
        case LP_OP_ISNOT:   return "IS NOT";
        case LP_OP_LIKE:    return "LIKE";
        case LP_OP_GLOB:    return "GLOB";
        case LP_OP_MATCH:   return "MATCH";
        case LP_OP_REGEXP:  return "REGEXP";
        case LP_OP_PTR:     return "->";
        case LP_OP_PTR2:    return "->>";
    }
    return "?";
}

const char *lp_unaryop_name(LpUnaryOp op) {
    switch (op) {
        case LP_UOP_MINUS:  return "-";
        case LP_UOP_PLUS:   return "+";
        case LP_UOP_NOT:    return "NOT";
        case LP_UOP_BITNOT: return "~";
    }
    return "?";
}

/* ================================================================== */
/*  Source span utility                                                 */
/* ================================================================== */

const char *lp_node_source(const LpNode *node, const char *sql, unsigned int *len) {
    if (!node || !sql) return NULL;
    const char *p = sql + node->pos.offset;
    if (len) *len = 0;
    /* Return pointer to the start of this node's token in the original SQL.
       The caller can use pos.offset to locate it. We don't track end offset,
       so len is set to 0 — the caller knows the token start. */
    return p;
}

/* ================================================================== */
/*  lp_version                                                         */
/* ================================================================== */

const char *lp_version(void) {
    return LITEPARSER_VERSION;
}

/* ================================================================== */
/*  lp_node_count                                                      */
/* ================================================================== */

static int count_enter(LpVisitor *v, LpNode *node) {
    (void)node;
    (*(int *)v->user_data)++;
    return 0;
}

int lp_node_count(const LpNode *node) {
    if (!node) return 0;
    int count = 0;
    LpVisitor v = { .user_data = &count, .enter = count_enter, .leave = NULL };
    lp_ast_walk((LpNode *)node, &v);
    return count;
}

/* ================================================================== */
/*  lp_node_equal                                                      */
/* ================================================================== */

static int str_eq(const char *a, const char *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static int list_eq(const LpNodeList *a, const LpNodeList *b) {
    if (a->count != b->count) return 0;
    for (int i = 0; i < a->count; i++) {
        if (!lp_node_equal(a->items[i], b->items[i])) return 0;
    }
    return 1;
}

int lp_node_equal(const LpNode *a, const LpNode *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;

    #define SE(x, y) if (!str_eq((x), (y))) return 0
    #define NE(x, y) if (!lp_node_equal((x), (y))) return 0
    #define LE(x, y) if (!list_eq(&(x), &(y))) return 0
    #define IE(x, y) if ((x) != (y)) return 0

    switch (a->kind) {
        case LP_STMT_SELECT:
            IE(a->u.select.distinct, b->u.select.distinct);
            LE(a->u.select.result_columns, b->u.select.result_columns);
            NE(a->u.select.from, b->u.select.from);
            NE(a->u.select.where, b->u.select.where);
            LE(a->u.select.group_by, b->u.select.group_by);
            NE(a->u.select.having, b->u.select.having);
            LE(a->u.select.order_by, b->u.select.order_by);
            NE(a->u.select.limit, b->u.select.limit);
            LE(a->u.select.window_defs, b->u.select.window_defs);
            NE(a->u.select.with, b->u.select.with);
            break;

        case LP_COMPOUND_SELECT:
            IE(a->u.compound.op, b->u.compound.op);
            NE(a->u.compound.left, b->u.compound.left);
            NE(a->u.compound.right, b->u.compound.right);
            break;

        case LP_STMT_INSERT:
            SE(a->u.insert.table, b->u.insert.table);
            SE(a->u.insert.schema, b->u.insert.schema);
            SE(a->u.insert.alias, b->u.insert.alias);
            IE(a->u.insert.or_conflict, b->u.insert.or_conflict);
            LE(a->u.insert.columns, b->u.insert.columns);
            NE(a->u.insert.source, b->u.insert.source);
            NE(a->u.insert.upsert, b->u.insert.upsert);
            LE(a->u.insert.returning, b->u.insert.returning);
            break;

        case LP_STMT_UPDATE:
            SE(a->u.update.table, b->u.update.table);
            SE(a->u.update.schema, b->u.update.schema);
            SE(a->u.update.alias, b->u.update.alias);
            IE(a->u.update.or_conflict, b->u.update.or_conflict);
            LE(a->u.update.set_clauses, b->u.update.set_clauses);
            NE(a->u.update.from, b->u.update.from);
            NE(a->u.update.where, b->u.update.where);
            LE(a->u.update.order_by, b->u.update.order_by);
            NE(a->u.update.limit, b->u.update.limit);
            LE(a->u.update.returning, b->u.update.returning);
            break;

        case LP_STMT_DELETE:
            SE(a->u.del.table, b->u.del.table);
            SE(a->u.del.schema, b->u.del.schema);
            SE(a->u.del.alias, b->u.del.alias);
            NE(a->u.del.where, b->u.del.where);
            LE(a->u.del.order_by, b->u.del.order_by);
            NE(a->u.del.limit, b->u.del.limit);
            LE(a->u.del.returning, b->u.del.returning);
            break;

        case LP_STMT_CREATE_TABLE:
            SE(a->u.create_table.name, b->u.create_table.name);
            SE(a->u.create_table.schema, b->u.create_table.schema);
            IE(a->u.create_table.if_not_exists, b->u.create_table.if_not_exists);
            IE(a->u.create_table.temp, b->u.create_table.temp);
            IE(a->u.create_table.options, b->u.create_table.options);
            LE(a->u.create_table.columns, b->u.create_table.columns);
            LE(a->u.create_table.constraints, b->u.create_table.constraints);
            NE(a->u.create_table.as_select, b->u.create_table.as_select);
            break;

        case LP_STMT_CREATE_INDEX:
            SE(a->u.create_index.name, b->u.create_index.name);
            SE(a->u.create_index.schema, b->u.create_index.schema);
            SE(a->u.create_index.table, b->u.create_index.table);
            IE(a->u.create_index.if_not_exists, b->u.create_index.if_not_exists);
            IE(a->u.create_index.is_unique, b->u.create_index.is_unique);
            LE(a->u.create_index.columns, b->u.create_index.columns);
            NE(a->u.create_index.where, b->u.create_index.where);
            break;

        case LP_STMT_CREATE_VIEW:
            SE(a->u.create_view.name, b->u.create_view.name);
            SE(a->u.create_view.schema, b->u.create_view.schema);
            IE(a->u.create_view.if_not_exists, b->u.create_view.if_not_exists);
            IE(a->u.create_view.temp, b->u.create_view.temp);
            LE(a->u.create_view.col_names, b->u.create_view.col_names);
            NE(a->u.create_view.select, b->u.create_view.select);
            break;

        case LP_STMT_CREATE_TRIGGER:
            SE(a->u.create_trigger.name, b->u.create_trigger.name);
            SE(a->u.create_trigger.schema, b->u.create_trigger.schema);
            IE(a->u.create_trigger.if_not_exists, b->u.create_trigger.if_not_exists);
            IE(a->u.create_trigger.temp, b->u.create_trigger.temp);
            IE(a->u.create_trigger.time, b->u.create_trigger.time);
            IE(a->u.create_trigger.event, b->u.create_trigger.event);
            SE(a->u.create_trigger.table_name, b->u.create_trigger.table_name);
            LE(a->u.create_trigger.update_columns, b->u.create_trigger.update_columns);
            NE(a->u.create_trigger.when, b->u.create_trigger.when);
            LE(a->u.create_trigger.body, b->u.create_trigger.body);
            break;

        case LP_STMT_CREATE_VTABLE:
            SE(a->u.create_vtable.name, b->u.create_vtable.name);
            SE(a->u.create_vtable.schema, b->u.create_vtable.schema);
            IE(a->u.create_vtable.if_not_exists, b->u.create_vtable.if_not_exists);
            SE(a->u.create_vtable.module, b->u.create_vtable.module);
            SE(a->u.create_vtable.module_args, b->u.create_vtable.module_args);
            break;

        case LP_STMT_DROP:
            IE(a->u.drop.target, b->u.drop.target);
            SE(a->u.drop.name, b->u.drop.name);
            SE(a->u.drop.schema, b->u.drop.schema);
            IE(a->u.drop.if_exists, b->u.drop.if_exists);
            break;

        case LP_STMT_BEGIN:
            IE(a->u.begin.trans_type, b->u.begin.trans_type);
            break;

        case LP_STMT_COMMIT:
        case LP_STMT_ROLLBACK:
            break;

        case LP_STMT_SAVEPOINT:
        case LP_STMT_RELEASE:
        case LP_STMT_ROLLBACK_TO:
            SE(a->u.savepoint.name, b->u.savepoint.name);
            break;

        case LP_STMT_PRAGMA:
            SE(a->u.pragma.name, b->u.pragma.name);
            SE(a->u.pragma.schema, b->u.pragma.schema);
            SE(a->u.pragma.value, b->u.pragma.value);
            IE(a->u.pragma.is_neg, b->u.pragma.is_neg);
            break;

        case LP_STMT_VACUUM:
            SE(a->u.vacuum.schema, b->u.vacuum.schema);
            NE(a->u.vacuum.into, b->u.vacuum.into);
            break;

        case LP_STMT_REINDEX:
        case LP_STMT_ANALYZE:
            SE(a->u.reindex.name, b->u.reindex.name);
            SE(a->u.reindex.schema, b->u.reindex.schema);
            break;

        case LP_STMT_ATTACH:
            NE(a->u.attach.filename, b->u.attach.filename);
            NE(a->u.attach.dbname, b->u.attach.dbname);
            NE(a->u.attach.key, b->u.attach.key);
            break;

        case LP_STMT_DETACH:
            NE(a->u.detach.dbname, b->u.detach.dbname);
            break;

        case LP_STMT_ALTER:
            SE(a->u.alter.table_name, b->u.alter.table_name);
            SE(a->u.alter.schema, b->u.alter.schema);
            IE(a->u.alter.alter_type, b->u.alter.alter_type);
            SE(a->u.alter.column_name, b->u.alter.column_name);
            SE(a->u.alter.new_name, b->u.alter.new_name);
            NE(a->u.alter.column_def, b->u.alter.column_def);
            break;

        case LP_STMT_EXPLAIN:
            IE(a->u.explain.is_query_plan, b->u.explain.is_query_plan);
            NE(a->u.explain.stmt, b->u.explain.stmt);
            break;

        /* Expressions */
        case LP_EXPR_LITERAL_INT:
        case LP_EXPR_LITERAL_FLOAT:
        case LP_EXPR_LITERAL_STRING:
        case LP_EXPR_LITERAL_BLOB:
        case LP_EXPR_LITERAL_BOOL:
            SE(a->u.literal.value, b->u.literal.value);
            break;

        case LP_EXPR_LITERAL_NULL:
            break;

        case LP_EXPR_COLUMN_REF:
            SE(a->u.column_ref.schema, b->u.column_ref.schema);
            SE(a->u.column_ref.table, b->u.column_ref.table);
            SE(a->u.column_ref.column, b->u.column_ref.column);
            break;

        case LP_EXPR_BINARY_OP:
            IE(a->u.binary.op, b->u.binary.op);
            NE(a->u.binary.left, b->u.binary.left);
            NE(a->u.binary.right, b->u.binary.right);
            NE(a->u.binary.escape, b->u.binary.escape);
            break;

        case LP_EXPR_UNARY_OP:
            IE(a->u.unary.op, b->u.unary.op);
            NE(a->u.unary.operand, b->u.unary.operand);
            break;

        case LP_EXPR_FUNCTION:
            SE(a->u.function.name, b->u.function.name);
            LE(a->u.function.args, b->u.function.args);
            IE(a->u.function.distinct, b->u.function.distinct);
            IE(a->u.function.is_ctime_kw, b->u.function.is_ctime_kw);
            LE(a->u.function.order_by, b->u.function.order_by);
            NE(a->u.function.filter, b->u.function.filter);
            NE(a->u.function.over, b->u.function.over);
            break;

        case LP_EXPR_CAST:
            NE(a->u.cast.expr, b->u.cast.expr);
            SE(a->u.cast.type_name, b->u.cast.type_name);
            break;

        case LP_EXPR_COLLATE:
            NE(a->u.collate.expr, b->u.collate.expr);
            SE(a->u.collate.collation, b->u.collate.collation);
            break;

        case LP_EXPR_BETWEEN:
            NE(a->u.between.expr, b->u.between.expr);
            NE(a->u.between.low, b->u.between.low);
            NE(a->u.between.high, b->u.between.high);
            IE(a->u.between.is_not, b->u.between.is_not);
            break;

        case LP_EXPR_IN:
            NE(a->u.in.expr, b->u.in.expr);
            LE(a->u.in.values, b->u.in.values);
            NE(a->u.in.select, b->u.in.select);
            IE(a->u.in.is_not, b->u.in.is_not);
            break;

        case LP_EXPR_EXISTS:
            NE(a->u.exists.select, b->u.exists.select);
            break;

        case LP_EXPR_SUBQUERY:
            NE(a->u.subquery.select, b->u.subquery.select);
            break;

        case LP_EXPR_CASE:
            NE(a->u.case_.operand, b->u.case_.operand);
            LE(a->u.case_.when_exprs, b->u.case_.when_exprs);
            NE(a->u.case_.else_expr, b->u.case_.else_expr);
            break;

        case LP_EXPR_RAISE:
            IE(a->u.raise.type, b->u.raise.type);
            NE(a->u.raise.message, b->u.raise.message);
            break;

        case LP_EXPR_VARIABLE:
            SE(a->u.variable.name, b->u.variable.name);
            break;

        case LP_EXPR_STAR:
            SE(a->u.star.table, b->u.star.table);
            break;

        case LP_EXPR_VECTOR:
            LE(a->u.vector.values, b->u.vector.values);
            break;

        /* Clauses / sub-structures */
        case LP_RESULT_COLUMN:
            NE(a->u.result_column.expr, b->u.result_column.expr);
            SE(a->u.result_column.alias, b->u.result_column.alias);
            break;

        case LP_FROM_TABLE:
            SE(a->u.from_table.name, b->u.from_table.name);
            SE(a->u.from_table.schema, b->u.from_table.schema);
            SE(a->u.from_table.alias, b->u.from_table.alias);
            SE(a->u.from_table.indexed_by, b->u.from_table.indexed_by);
            IE(a->u.from_table.not_indexed, b->u.from_table.not_indexed);
            LE(a->u.from_table.func_args, b->u.from_table.func_args);
            break;

        case LP_FROM_SUBQUERY:
            NE(a->u.from_subquery.select, b->u.from_subquery.select);
            SE(a->u.from_subquery.alias, b->u.from_subquery.alias);
            break;

        case LP_JOIN_CLAUSE:
            NE(a->u.join.left, b->u.join.left);
            NE(a->u.join.right, b->u.join.right);
            IE(a->u.join.join_type, b->u.join.join_type);
            NE(a->u.join.on_expr, b->u.join.on_expr);
            LE(a->u.join.using_columns, b->u.join.using_columns);
            break;

        case LP_ORDER_TERM:
            NE(a->u.order_term.expr, b->u.order_term.expr);
            IE(a->u.order_term.direction, b->u.order_term.direction);
            IE(a->u.order_term.nulls, b->u.order_term.nulls);
            break;

        case LP_LIMIT:
            NE(a->u.limit.count, b->u.limit.count);
            NE(a->u.limit.offset, b->u.limit.offset);
            break;

        case LP_COLUMN_DEF:
            SE(a->u.column_def.name, b->u.column_def.name);
            SE(a->u.column_def.type_name, b->u.column_def.type_name);
            LE(a->u.column_def.constraints, b->u.column_def.constraints);
            break;

        case LP_COLUMN_CONSTRAINT:
            IE(a->u.column_constraint.type, b->u.column_constraint.type);
            SE(a->u.column_constraint.name, b->u.column_constraint.name);
            NE(a->u.column_constraint.expr, b->u.column_constraint.expr);
            NE(a->u.column_constraint.fk, b->u.column_constraint.fk);
            SE(a->u.column_constraint.collation, b->u.column_constraint.collation);
            IE(a->u.column_constraint.sort_order, b->u.column_constraint.sort_order);
            IE(a->u.column_constraint.conflict_action, b->u.column_constraint.conflict_action);
            IE(a->u.column_constraint.is_autoinc, b->u.column_constraint.is_autoinc);
            IE(a->u.column_constraint.generated_type, b->u.column_constraint.generated_type);
            break;

        case LP_TABLE_CONSTRAINT:
            IE(a->u.table_constraint.type, b->u.table_constraint.type);
            SE(a->u.table_constraint.name, b->u.table_constraint.name);
            LE(a->u.table_constraint.columns, b->u.table_constraint.columns);
            NE(a->u.table_constraint.expr, b->u.table_constraint.expr);
            NE(a->u.table_constraint.fk, b->u.table_constraint.fk);
            IE(a->u.table_constraint.conflict_action, b->u.table_constraint.conflict_action);
            IE(a->u.table_constraint.is_autoinc, b->u.table_constraint.is_autoinc);
            break;

        case LP_FOREIGN_KEY:
            SE(a->u.foreign_key.table, b->u.foreign_key.table);
            LE(a->u.foreign_key.columns, b->u.foreign_key.columns);
            IE(a->u.foreign_key.on_delete, b->u.foreign_key.on_delete);
            IE(a->u.foreign_key.on_update, b->u.foreign_key.on_update);
            IE(a->u.foreign_key.deferrable, b->u.foreign_key.deferrable);
            break;

        case LP_CTE:
            SE(a->u.cte.name, b->u.cte.name);
            LE(a->u.cte.columns, b->u.cte.columns);
            NE(a->u.cte.select, b->u.cte.select);
            IE(a->u.cte.materialized, b->u.cte.materialized);
            break;

        case LP_WITH:
            IE(a->u.with.recursive, b->u.with.recursive);
            LE(a->u.with.ctes, b->u.with.ctes);
            break;

        case LP_UPSERT:
            LE(a->u.upsert.conflict_target, b->u.upsert.conflict_target);
            NE(a->u.upsert.conflict_where, b->u.upsert.conflict_where);
            LE(a->u.upsert.set_clauses, b->u.upsert.set_clauses);
            NE(a->u.upsert.where, b->u.upsert.where);
            NE(a->u.upsert.next, b->u.upsert.next);
            break;

        case LP_RETURNING:
            LE(a->u.returning.columns, b->u.returning.columns);
            break;

        case LP_WINDOW_DEF:
            SE(a->u.window_def.name, b->u.window_def.name);
            SE(a->u.window_def.base_name, b->u.window_def.base_name);
            LE(a->u.window_def.partition_by, b->u.window_def.partition_by);
            LE(a->u.window_def.order_by, b->u.window_def.order_by);
            NE(a->u.window_def.frame, b->u.window_def.frame);
            break;

        case LP_WINDOW_FRAME:
            IE(a->u.window_frame.type, b->u.window_frame.type);
            NE(a->u.window_frame.start, b->u.window_frame.start);
            NE(a->u.window_frame.end, b->u.window_frame.end);
            IE(a->u.window_frame.exclude, b->u.window_frame.exclude);
            break;

        case LP_FRAME_BOUND:
            IE(a->u.frame_bound.type, b->u.frame_bound.type);
            NE(a->u.frame_bound.expr, b->u.frame_bound.expr);
            break;

        case LP_SET_CLAUSE:
            SE(a->u.set_clause.column, b->u.set_clause.column);
            LE(a->u.set_clause.columns, b->u.set_clause.columns);
            NE(a->u.set_clause.expr, b->u.set_clause.expr);
            break;

        case LP_INDEX_COLUMN:
            NE(a->u.index_column.expr, b->u.index_column.expr);
            SE(a->u.index_column.collation, b->u.index_column.collation);
            IE(a->u.index_column.sort_order, b->u.index_column.sort_order);
            break;

        case LP_VALUES_ROW:
            LE(a->u.values_row.values, b->u.values_row.values);
            break;

        case LP_TRIGGER_CMD:
            NE(a->u.trigger_cmd.stmt, b->u.trigger_cmd.stmt);
            break;

        case LP_NODE_KIND_COUNT:
            break;
    }

    #undef SE
    #undef NE
    #undef LE
    #undef IE

    return 1;
}

/* ================================================================== */
/*  lp_fix_parents                                                     */
/* ================================================================== */

static void fix_node(LpNode *node);

static void fix_list(LpNode *parent, LpNodeList *list) {
    if (!list || !list->items) return;
    for (int i = 0; i < list->count; i++) {
        if (list->items[i]) {
            list->items[i]->parent = parent;
            fix_node(list->items[i]);
        }
    }
}

#define FIX_NODE(p, n) do { if (n) { (n)->parent = (p); fix_node(n); } } while(0)
#define FIX_LIST(p, l) fix_list((p), &(l))

static void fix_node(LpNode *node) {
    switch (node->kind) {
        case LP_STMT_SELECT:
            FIX_LIST(node, node->u.select.result_columns);
            FIX_NODE(node, node->u.select.from);
            FIX_NODE(node, node->u.select.where);
            FIX_LIST(node, node->u.select.group_by);
            FIX_NODE(node, node->u.select.having);
            FIX_LIST(node, node->u.select.window_defs);
            FIX_LIST(node, node->u.select.order_by);
            FIX_NODE(node, node->u.select.limit);
            FIX_NODE(node, node->u.select.with);
            break;

        case LP_COMPOUND_SELECT:
            FIX_NODE(node, node->u.compound.left);
            FIX_NODE(node, node->u.compound.right);
            break;

        case LP_STMT_INSERT:
            FIX_LIST(node, node->u.insert.columns);
            FIX_NODE(node, node->u.insert.source);
            FIX_NODE(node, node->u.insert.upsert);
            FIX_LIST(node, node->u.insert.returning);
            break;

        case LP_STMT_UPDATE:
            FIX_LIST(node, node->u.update.set_clauses);
            FIX_NODE(node, node->u.update.from);
            FIX_NODE(node, node->u.update.where);
            FIX_LIST(node, node->u.update.order_by);
            FIX_NODE(node, node->u.update.limit);
            FIX_LIST(node, node->u.update.returning);
            break;

        case LP_STMT_DELETE:
            FIX_NODE(node, node->u.del.where);
            FIX_LIST(node, node->u.del.order_by);
            FIX_NODE(node, node->u.del.limit);
            FIX_LIST(node, node->u.del.returning);
            break;

        case LP_STMT_CREATE_TABLE:
            FIX_LIST(node, node->u.create_table.columns);
            FIX_LIST(node, node->u.create_table.constraints);
            FIX_NODE(node, node->u.create_table.as_select);
            break;

        case LP_STMT_CREATE_INDEX:
            FIX_LIST(node, node->u.create_index.columns);
            FIX_NODE(node, node->u.create_index.where);
            break;

        case LP_STMT_CREATE_VIEW:
            FIX_LIST(node, node->u.create_view.col_names);
            FIX_NODE(node, node->u.create_view.select);
            break;

        case LP_STMT_CREATE_TRIGGER:
            FIX_LIST(node, node->u.create_trigger.update_columns);
            FIX_NODE(node, node->u.create_trigger.when);
            FIX_LIST(node, node->u.create_trigger.body);
            break;

        case LP_STMT_CREATE_VTABLE:
        case LP_STMT_DROP:
        case LP_STMT_BEGIN:
        case LP_STMT_COMMIT:
        case LP_STMT_ROLLBACK:
        case LP_STMT_SAVEPOINT:
        case LP_STMT_RELEASE:
        case LP_STMT_ROLLBACK_TO:
        case LP_STMT_PRAGMA:
            break;

        case LP_STMT_VACUUM:
            FIX_NODE(node, node->u.vacuum.into);
            break;

        case LP_STMT_REINDEX:
        case LP_STMT_ANALYZE:
            break;

        case LP_STMT_ATTACH:
            FIX_NODE(node, node->u.attach.filename);
            FIX_NODE(node, node->u.attach.dbname);
            FIX_NODE(node, node->u.attach.key);
            break;

        case LP_STMT_DETACH:
            FIX_NODE(node, node->u.detach.dbname);
            break;

        case LP_STMT_ALTER:
            FIX_NODE(node, node->u.alter.column_def);
            break;

        case LP_STMT_EXPLAIN:
            FIX_NODE(node, node->u.explain.stmt);
            break;

        case LP_EXPR_LITERAL_INT:
        case LP_EXPR_LITERAL_FLOAT:
        case LP_EXPR_LITERAL_STRING:
        case LP_EXPR_LITERAL_BLOB:
        case LP_EXPR_LITERAL_NULL:
        case LP_EXPR_LITERAL_BOOL:
        case LP_EXPR_COLUMN_REF:
            break;

        case LP_EXPR_BINARY_OP:
            FIX_NODE(node, node->u.binary.left);
            FIX_NODE(node, node->u.binary.right);
            FIX_NODE(node, node->u.binary.escape);
            break;

        case LP_EXPR_UNARY_OP:
            FIX_NODE(node, node->u.unary.operand);
            break;

        case LP_EXPR_FUNCTION:
            FIX_LIST(node, node->u.function.args);
            FIX_LIST(node, node->u.function.order_by);
            FIX_NODE(node, node->u.function.filter);
            FIX_NODE(node, node->u.function.over);
            break;

        case LP_EXPR_CAST:
            FIX_NODE(node, node->u.cast.expr);
            break;

        case LP_EXPR_COLLATE:
            FIX_NODE(node, node->u.collate.expr);
            break;

        case LP_EXPR_BETWEEN:
            FIX_NODE(node, node->u.between.expr);
            FIX_NODE(node, node->u.between.low);
            FIX_NODE(node, node->u.between.high);
            break;

        case LP_EXPR_IN:
            FIX_NODE(node, node->u.in.expr);
            FIX_LIST(node, node->u.in.values);
            FIX_NODE(node, node->u.in.select);
            break;

        case LP_EXPR_EXISTS:
            FIX_NODE(node, node->u.exists.select);
            break;

        case LP_EXPR_SUBQUERY:
            FIX_NODE(node, node->u.subquery.select);
            break;

        case LP_EXPR_CASE:
            FIX_NODE(node, node->u.case_.operand);
            FIX_LIST(node, node->u.case_.when_exprs);
            FIX_NODE(node, node->u.case_.else_expr);
            break;

        case LP_EXPR_RAISE:
            FIX_NODE(node, node->u.raise.message);
            break;

        case LP_EXPR_VARIABLE:
        case LP_EXPR_STAR:
            break;

        case LP_EXPR_VECTOR:
            FIX_LIST(node, node->u.vector.values);
            break;

        case LP_RESULT_COLUMN:
            FIX_NODE(node, node->u.result_column.expr);
            break;

        case LP_FROM_TABLE:
            FIX_LIST(node, node->u.from_table.func_args);
            break;

        case LP_FROM_SUBQUERY:
            FIX_NODE(node, node->u.from_subquery.select);
            break;

        case LP_JOIN_CLAUSE:
            FIX_NODE(node, node->u.join.left);
            FIX_NODE(node, node->u.join.right);
            FIX_NODE(node, node->u.join.on_expr);
            FIX_LIST(node, node->u.join.using_columns);
            break;

        case LP_ORDER_TERM:
            FIX_NODE(node, node->u.order_term.expr);
            break;

        case LP_LIMIT:
            FIX_NODE(node, node->u.limit.count);
            FIX_NODE(node, node->u.limit.offset);
            break;

        case LP_COLUMN_DEF:
            FIX_LIST(node, node->u.column_def.constraints);
            break;

        case LP_COLUMN_CONSTRAINT:
            FIX_NODE(node, node->u.column_constraint.expr);
            FIX_NODE(node, node->u.column_constraint.fk);
            break;

        case LP_TABLE_CONSTRAINT:
            FIX_LIST(node, node->u.table_constraint.columns);
            FIX_NODE(node, node->u.table_constraint.expr);
            FIX_NODE(node, node->u.table_constraint.fk);
            break;

        case LP_FOREIGN_KEY:
            FIX_LIST(node, node->u.foreign_key.columns);
            break;

        case LP_CTE:
            FIX_LIST(node, node->u.cte.columns);
            FIX_NODE(node, node->u.cte.select);
            break;

        case LP_WITH:
            FIX_LIST(node, node->u.with.ctes);
            break;

        case LP_UPSERT:
            FIX_LIST(node, node->u.upsert.conflict_target);
            FIX_NODE(node, node->u.upsert.conflict_where);
            FIX_LIST(node, node->u.upsert.set_clauses);
            FIX_NODE(node, node->u.upsert.where);
            FIX_NODE(node, node->u.upsert.next);
            break;

        case LP_RETURNING:
            FIX_LIST(node, node->u.returning.columns);
            break;

        case LP_WINDOW_DEF:
            FIX_LIST(node, node->u.window_def.partition_by);
            FIX_LIST(node, node->u.window_def.order_by);
            FIX_NODE(node, node->u.window_def.frame);
            break;

        case LP_WINDOW_FRAME:
            FIX_NODE(node, node->u.window_frame.start);
            FIX_NODE(node, node->u.window_frame.end);
            break;

        case LP_FRAME_BOUND:
            FIX_NODE(node, node->u.frame_bound.expr);
            break;

        case LP_SET_CLAUSE:
            FIX_LIST(node, node->u.set_clause.columns);
            FIX_NODE(node, node->u.set_clause.expr);
            break;

        case LP_INDEX_COLUMN:
            FIX_NODE(node, node->u.index_column.expr);
            break;

        case LP_VALUES_ROW:
            FIX_LIST(node, node->u.values_row.values);
            break;

        case LP_TRIGGER_CMD:
            FIX_NODE(node, node->u.trigger_cmd.stmt);
            break;

        case LP_NODE_KIND_COUNT:
            break;
    }
}

#undef FIX_NODE
#undef FIX_LIST

void lp_fix_parents(LpNode *root) {
    if (!root) return;
    root->parent = NULL;
    fix_node(root);
}

/* ================================================================== */
/*  AST Mutation API                                                   */
/* ================================================================== */

LpNode *lp_node_alloc(arena_t *arena, LpNodeKind kind) {
    LpNode *n = (LpNode *)arena_zeroalloc(arena, sizeof(LpNode));
    if (n) n->kind = kind;
    return n;
}

char *lp_strdup(arena_t *arena, const char *s) {
    if (!s) return NULL;
    return arena_strdup(arena, s);
}

/* Internal: grow a list to hold at least one more item */
static void list_grow(arena_t *arena, LpNodeList *list) {
    int new_cap = list->capacity < 4 ? 4 : list->capacity * 2;
    LpNode **new_items = (LpNode **)arena_alloc(arena, sizeof(LpNode *) * new_cap);
    if (!new_items) return;
    if (list->items && list->count > 0) {
        memcpy(new_items, list->items, sizeof(LpNode *) * list->count);
    }
    list->items = new_items;
    list->capacity = new_cap;
}

void lp_list_push(arena_t *arena, LpNodeList *list, LpNode *item) {
    if (!list || !item) return;
    if (list->count >= list->capacity) list_grow(arena, list);
    list->items[list->count++] = item;
}

void lp_list_insert(arena_t *arena, LpNodeList *list, int index, LpNode *item) {
    if (!list || !item) return;
    if (index < 0) index = 0;
    if (index > list->count) index = list->count;
    if (list->count >= list->capacity) list_grow(arena, list);
    /* Shift items right */
    for (int i = list->count; i > index; i--) {
        list->items[i] = list->items[i - 1];
    }
    list->items[index] = item;
    list->count++;
}

LpNode *lp_list_replace(LpNodeList *list, int index, LpNode *new_item) {
    if (!list || index < 0 || index >= list->count) return NULL;
    LpNode *old = list->items[index];
    list->items[index] = new_item;
    return old;
}

LpNode *lp_list_remove(LpNodeList *list, int index) {
    if (!list || index < 0 || index >= list->count) return NULL;
    LpNode *old = list->items[index];
    /* Shift items left */
    for (int i = index; i < list->count - 1; i++) {
        list->items[i] = list->items[i + 1];
    }
    list->count--;
    return old;
}

/* Deep clone helpers */
static LpNodeList clone_list(arena_t *arena, const LpNodeList *src);

LpNode *lp_node_clone(arena_t *arena, const LpNode *node) {
    if (!node) return NULL;
    LpNode *n = (LpNode *)arena_zeroalloc(arena, sizeof(LpNode));
    if (!n) return NULL;
    n->kind = node->kind;
    n->pos = node->pos;

    #define CS(s) arena_strdup(arena, (s))  /* clone string */
    #define CN(p) lp_node_clone(arena, (p)) /* clone node */
    #define CL(l) clone_list(arena, &(l))   /* clone list */

    switch (node->kind) {
        case LP_STMT_SELECT:
            n->u.select.distinct = node->u.select.distinct;
            n->u.select.result_columns = CL(node->u.select.result_columns);
            n->u.select.from = CN(node->u.select.from);
            n->u.select.where = CN(node->u.select.where);
            n->u.select.group_by = CL(node->u.select.group_by);
            n->u.select.having = CN(node->u.select.having);
            n->u.select.order_by = CL(node->u.select.order_by);
            n->u.select.limit = CN(node->u.select.limit);
            n->u.select.window_defs = CL(node->u.select.window_defs);
            n->u.select.with = CN(node->u.select.with);
            break;

        case LP_COMPOUND_SELECT:
            n->u.compound.op = node->u.compound.op;
            n->u.compound.left = CN(node->u.compound.left);
            n->u.compound.right = CN(node->u.compound.right);
            break;

        case LP_STMT_INSERT:
            n->u.insert.table = CS(node->u.insert.table);
            n->u.insert.schema = CS(node->u.insert.schema);
            n->u.insert.alias = CS(node->u.insert.alias);
            n->u.insert.or_conflict = node->u.insert.or_conflict;
            n->u.insert.columns = CL(node->u.insert.columns);
            n->u.insert.source = CN(node->u.insert.source);
            n->u.insert.upsert = CN(node->u.insert.upsert);
            n->u.insert.returning = CL(node->u.insert.returning);
            break;

        case LP_STMT_UPDATE:
            n->u.update.table = CS(node->u.update.table);
            n->u.update.schema = CS(node->u.update.schema);
            n->u.update.alias = CS(node->u.update.alias);
            n->u.update.or_conflict = node->u.update.or_conflict;
            n->u.update.set_clauses = CL(node->u.update.set_clauses);
            n->u.update.from = CN(node->u.update.from);
            n->u.update.where = CN(node->u.update.where);
            n->u.update.order_by = CL(node->u.update.order_by);
            n->u.update.limit = CN(node->u.update.limit);
            n->u.update.returning = CL(node->u.update.returning);
            break;

        case LP_STMT_DELETE:
            n->u.del.table = CS(node->u.del.table);
            n->u.del.schema = CS(node->u.del.schema);
            n->u.del.alias = CS(node->u.del.alias);
            n->u.del.where = CN(node->u.del.where);
            n->u.del.order_by = CL(node->u.del.order_by);
            n->u.del.limit = CN(node->u.del.limit);
            n->u.del.returning = CL(node->u.del.returning);
            break;

        case LP_STMT_CREATE_TABLE:
            n->u.create_table.name = CS(node->u.create_table.name);
            n->u.create_table.schema = CS(node->u.create_table.schema);
            n->u.create_table.if_not_exists = node->u.create_table.if_not_exists;
            n->u.create_table.temp = node->u.create_table.temp;
            n->u.create_table.options = node->u.create_table.options;
            n->u.create_table.columns = CL(node->u.create_table.columns);
            n->u.create_table.constraints = CL(node->u.create_table.constraints);
            n->u.create_table.as_select = CN(node->u.create_table.as_select);
            break;

        case LP_STMT_CREATE_INDEX:
            n->u.create_index.name = CS(node->u.create_index.name);
            n->u.create_index.schema = CS(node->u.create_index.schema);
            n->u.create_index.table = CS(node->u.create_index.table);
            n->u.create_index.if_not_exists = node->u.create_index.if_not_exists;
            n->u.create_index.is_unique = node->u.create_index.is_unique;
            n->u.create_index.columns = CL(node->u.create_index.columns);
            n->u.create_index.where = CN(node->u.create_index.where);
            break;

        case LP_STMT_CREATE_VIEW:
            n->u.create_view.name = CS(node->u.create_view.name);
            n->u.create_view.schema = CS(node->u.create_view.schema);
            n->u.create_view.if_not_exists = node->u.create_view.if_not_exists;
            n->u.create_view.temp = node->u.create_view.temp;
            n->u.create_view.col_names = CL(node->u.create_view.col_names);
            n->u.create_view.select = CN(node->u.create_view.select);
            break;

        case LP_STMT_CREATE_TRIGGER:
            n->u.create_trigger.name = CS(node->u.create_trigger.name);
            n->u.create_trigger.schema = CS(node->u.create_trigger.schema);
            n->u.create_trigger.if_not_exists = node->u.create_trigger.if_not_exists;
            n->u.create_trigger.temp = node->u.create_trigger.temp;
            n->u.create_trigger.time = node->u.create_trigger.time;
            n->u.create_trigger.event = node->u.create_trigger.event;
            n->u.create_trigger.table_name = CS(node->u.create_trigger.table_name);
            n->u.create_trigger.update_columns = CL(node->u.create_trigger.update_columns);
            n->u.create_trigger.when = CN(node->u.create_trigger.when);
            n->u.create_trigger.body = CL(node->u.create_trigger.body);
            break;

        case LP_STMT_CREATE_VTABLE:
            n->u.create_vtable.name = CS(node->u.create_vtable.name);
            n->u.create_vtable.schema = CS(node->u.create_vtable.schema);
            n->u.create_vtable.if_not_exists = node->u.create_vtable.if_not_exists;
            n->u.create_vtable.module = CS(node->u.create_vtable.module);
            n->u.create_vtable.module_args = CS(node->u.create_vtable.module_args);
            break;

        case LP_STMT_DROP:
            n->u.drop.target = node->u.drop.target;
            n->u.drop.name = CS(node->u.drop.name);
            n->u.drop.schema = CS(node->u.drop.schema);
            n->u.drop.if_exists = node->u.drop.if_exists;
            break;

        case LP_STMT_BEGIN:
            n->u.begin.trans_type = node->u.begin.trans_type;
            break;

        case LP_STMT_COMMIT:
            break;

        case LP_STMT_ROLLBACK:
            break;

        case LP_STMT_SAVEPOINT:
        case LP_STMT_RELEASE:
        case LP_STMT_ROLLBACK_TO:
            n->u.savepoint.name = CS(node->u.savepoint.name);
            break;

        case LP_STMT_PRAGMA:
            n->u.pragma.name = CS(node->u.pragma.name);
            n->u.pragma.schema = CS(node->u.pragma.schema);
            n->u.pragma.value = CS(node->u.pragma.value);
            n->u.pragma.is_neg = node->u.pragma.is_neg;
            break;

        case LP_STMT_VACUUM:
            n->u.vacuum.schema = CS(node->u.vacuum.schema);
            n->u.vacuum.into = CN(node->u.vacuum.into);
            break;

        case LP_STMT_REINDEX:
        case LP_STMT_ANALYZE:
            n->u.reindex.name = CS(node->u.reindex.name);
            n->u.reindex.schema = CS(node->u.reindex.schema);
            break;

        case LP_STMT_ATTACH:
            n->u.attach.filename = CN(node->u.attach.filename);
            n->u.attach.dbname = CN(node->u.attach.dbname);
            n->u.attach.key = CN(node->u.attach.key);
            break;

        case LP_STMT_DETACH:
            n->u.detach.dbname = CN(node->u.detach.dbname);
            break;

        case LP_STMT_ALTER:
            n->u.alter.table_name = CS(node->u.alter.table_name);
            n->u.alter.schema = CS(node->u.alter.schema);
            n->u.alter.alter_type = node->u.alter.alter_type;
            n->u.alter.column_name = CS(node->u.alter.column_name);
            n->u.alter.new_name = CS(node->u.alter.new_name);
            n->u.alter.column_def = CN(node->u.alter.column_def);
            break;

        case LP_STMT_EXPLAIN:
            n->u.explain.is_query_plan = node->u.explain.is_query_plan;
            n->u.explain.stmt = CN(node->u.explain.stmt);
            break;

        /* Expressions */
        case LP_EXPR_LITERAL_INT:
        case LP_EXPR_LITERAL_FLOAT:
        case LP_EXPR_LITERAL_STRING:
        case LP_EXPR_LITERAL_BLOB:
        case LP_EXPR_LITERAL_BOOL:
            n->u.literal.value = CS(node->u.literal.value);
            break;

        case LP_EXPR_LITERAL_NULL:
            break;

        case LP_EXPR_COLUMN_REF:
            n->u.column_ref.schema = CS(node->u.column_ref.schema);
            n->u.column_ref.table = CS(node->u.column_ref.table);
            n->u.column_ref.column = CS(node->u.column_ref.column);
            break;

        case LP_EXPR_BINARY_OP:
            n->u.binary.op = node->u.binary.op;
            n->u.binary.left = CN(node->u.binary.left);
            n->u.binary.right = CN(node->u.binary.right);
            n->u.binary.escape = CN(node->u.binary.escape);
            break;

        case LP_EXPR_UNARY_OP:
            n->u.unary.op = node->u.unary.op;
            n->u.unary.operand = CN(node->u.unary.operand);
            break;

        case LP_EXPR_FUNCTION:
            n->u.function.name = CS(node->u.function.name);
            n->u.function.args = CL(node->u.function.args);
            n->u.function.distinct = node->u.function.distinct;
            n->u.function.is_ctime_kw = node->u.function.is_ctime_kw;
            n->u.function.order_by = CL(node->u.function.order_by);
            n->u.function.filter = CN(node->u.function.filter);
            n->u.function.over = CN(node->u.function.over);
            break;

        case LP_EXPR_CAST:
            n->u.cast.expr = CN(node->u.cast.expr);
            n->u.cast.type_name = CS(node->u.cast.type_name);
            break;

        case LP_EXPR_COLLATE:
            n->u.collate.expr = CN(node->u.collate.expr);
            n->u.collate.collation = CS(node->u.collate.collation);
            break;

        case LP_EXPR_BETWEEN:
            n->u.between.expr = CN(node->u.between.expr);
            n->u.between.low = CN(node->u.between.low);
            n->u.between.high = CN(node->u.between.high);
            n->u.between.is_not = node->u.between.is_not;
            break;

        case LP_EXPR_IN:
            n->u.in.expr = CN(node->u.in.expr);
            n->u.in.values = CL(node->u.in.values);
            n->u.in.select = CN(node->u.in.select);
            n->u.in.is_not = node->u.in.is_not;
            break;

        case LP_EXPR_EXISTS:
            n->u.exists.select = CN(node->u.exists.select);
            break;

        case LP_EXPR_SUBQUERY:
            n->u.subquery.select = CN(node->u.subquery.select);
            break;

        case LP_EXPR_CASE:
            n->u.case_.operand = CN(node->u.case_.operand);
            n->u.case_.when_exprs = CL(node->u.case_.when_exprs);
            n->u.case_.else_expr = CN(node->u.case_.else_expr);
            break;

        case LP_EXPR_RAISE:
            n->u.raise.type = node->u.raise.type;
            n->u.raise.message = CN(node->u.raise.message);
            break;

        case LP_EXPR_VARIABLE:
            n->u.variable.name = CS(node->u.variable.name);
            break;

        case LP_EXPR_STAR:
            n->u.star.table = CS(node->u.star.table);
            break;

        case LP_EXPR_VECTOR:
            n->u.vector.values = CL(node->u.vector.values);
            break;

        /* Clauses / sub-structures */
        case LP_RESULT_COLUMN:
            n->u.result_column.expr = CN(node->u.result_column.expr);
            n->u.result_column.alias = CS(node->u.result_column.alias);
            break;

        case LP_FROM_TABLE:
            n->u.from_table.name = CS(node->u.from_table.name);
            n->u.from_table.schema = CS(node->u.from_table.schema);
            n->u.from_table.alias = CS(node->u.from_table.alias);
            n->u.from_table.indexed_by = CS(node->u.from_table.indexed_by);
            n->u.from_table.not_indexed = node->u.from_table.not_indexed;
            n->u.from_table.func_args = CL(node->u.from_table.func_args);
            break;

        case LP_FROM_SUBQUERY:
            n->u.from_subquery.select = CN(node->u.from_subquery.select);
            n->u.from_subquery.alias = CS(node->u.from_subquery.alias);
            break;

        case LP_JOIN_CLAUSE:
            n->u.join.left = CN(node->u.join.left);
            n->u.join.right = CN(node->u.join.right);
            n->u.join.join_type = node->u.join.join_type;
            n->u.join.on_expr = CN(node->u.join.on_expr);
            n->u.join.using_columns = CL(node->u.join.using_columns);
            break;

        case LP_ORDER_TERM:
            n->u.order_term.expr = CN(node->u.order_term.expr);
            n->u.order_term.direction = node->u.order_term.direction;
            n->u.order_term.nulls = node->u.order_term.nulls;
            break;

        case LP_LIMIT:
            n->u.limit.count = CN(node->u.limit.count);
            n->u.limit.offset = CN(node->u.limit.offset);
            break;

        case LP_COLUMN_DEF:
            n->u.column_def.name = CS(node->u.column_def.name);
            n->u.column_def.type_name = CS(node->u.column_def.type_name);
            n->u.column_def.constraints = CL(node->u.column_def.constraints);
            break;

        case LP_COLUMN_CONSTRAINT:
            n->u.column_constraint.type = node->u.column_constraint.type;
            n->u.column_constraint.name = CS(node->u.column_constraint.name);
            n->u.column_constraint.expr = CN(node->u.column_constraint.expr);
            n->u.column_constraint.fk = CN(node->u.column_constraint.fk);
            n->u.column_constraint.collation = CS(node->u.column_constraint.collation);
            n->u.column_constraint.sort_order = node->u.column_constraint.sort_order;
            n->u.column_constraint.conflict_action = node->u.column_constraint.conflict_action;
            n->u.column_constraint.is_autoinc = node->u.column_constraint.is_autoinc;
            n->u.column_constraint.generated_type = node->u.column_constraint.generated_type;
            break;

        case LP_TABLE_CONSTRAINT:
            n->u.table_constraint.type = node->u.table_constraint.type;
            n->u.table_constraint.name = CS(node->u.table_constraint.name);
            n->u.table_constraint.columns = CL(node->u.table_constraint.columns);
            n->u.table_constraint.expr = CN(node->u.table_constraint.expr);
            n->u.table_constraint.fk = CN(node->u.table_constraint.fk);
            n->u.table_constraint.conflict_action = node->u.table_constraint.conflict_action;
            n->u.table_constraint.is_autoinc = node->u.table_constraint.is_autoinc;
            break;

        case LP_FOREIGN_KEY:
            n->u.foreign_key.table = CS(node->u.foreign_key.table);
            n->u.foreign_key.columns = CL(node->u.foreign_key.columns);
            n->u.foreign_key.on_delete = node->u.foreign_key.on_delete;
            n->u.foreign_key.on_update = node->u.foreign_key.on_update;
            n->u.foreign_key.deferrable = node->u.foreign_key.deferrable;
            break;

        case LP_CTE:
            n->u.cte.name = CS(node->u.cte.name);
            n->u.cte.columns = CL(node->u.cte.columns);
            n->u.cte.select = CN(node->u.cte.select);
            n->u.cte.materialized = node->u.cte.materialized;
            break;

        case LP_WITH:
            n->u.with.recursive = node->u.with.recursive;
            n->u.with.ctes = CL(node->u.with.ctes);
            break;

        case LP_UPSERT:
            n->u.upsert.conflict_target = CL(node->u.upsert.conflict_target);
            n->u.upsert.conflict_where = CN(node->u.upsert.conflict_where);
            n->u.upsert.set_clauses = CL(node->u.upsert.set_clauses);
            n->u.upsert.where = CN(node->u.upsert.where);
            n->u.upsert.next = CN(node->u.upsert.next);
            break;

        case LP_RETURNING:
            n->u.returning.columns = CL(node->u.returning.columns);
            break;

        case LP_WINDOW_DEF:
            n->u.window_def.name = CS(node->u.window_def.name);
            n->u.window_def.base_name = CS(node->u.window_def.base_name);
            n->u.window_def.partition_by = CL(node->u.window_def.partition_by);
            n->u.window_def.order_by = CL(node->u.window_def.order_by);
            n->u.window_def.frame = CN(node->u.window_def.frame);
            break;

        case LP_WINDOW_FRAME:
            n->u.window_frame.type = node->u.window_frame.type;
            n->u.window_frame.start = CN(node->u.window_frame.start);
            n->u.window_frame.end = CN(node->u.window_frame.end);
            n->u.window_frame.exclude = node->u.window_frame.exclude;
            break;

        case LP_FRAME_BOUND:
            n->u.frame_bound.type = node->u.frame_bound.type;
            n->u.frame_bound.expr = CN(node->u.frame_bound.expr);
            break;

        case LP_SET_CLAUSE:
            n->u.set_clause.column = CS(node->u.set_clause.column);
            n->u.set_clause.columns = CL(node->u.set_clause.columns);
            n->u.set_clause.expr = CN(node->u.set_clause.expr);
            break;

        case LP_INDEX_COLUMN:
            n->u.index_column.expr = CN(node->u.index_column.expr);
            n->u.index_column.collation = CS(node->u.index_column.collation);
            n->u.index_column.sort_order = node->u.index_column.sort_order;
            break;

        case LP_VALUES_ROW:
            n->u.values_row.values = CL(node->u.values_row.values);
            break;

        case LP_TRIGGER_CMD:
            n->u.trigger_cmd.stmt = CN(node->u.trigger_cmd.stmt);
            break;

        case LP_NODE_KIND_COUNT:
            break;
    }

    #undef CS
    #undef CN
    #undef CL

    lp_fix_parents(n);
    return n;
}

static LpNodeList clone_list(arena_t *arena, const LpNodeList *src) {
    LpNodeList dst;
    dst.count = 0;
    dst.capacity = 0;
    dst.items = NULL;
    if (!src || src->count <= 0) return dst;
    dst.capacity = src->count;
    dst.items = (LpNode **)arena_alloc(arena, sizeof(LpNode *) * dst.capacity);
    if (!dst.items) { dst.capacity = 0; return dst; }
    for (int i = 0; i < src->count; i++) {
        dst.items[i] = lp_node_clone(arena, src->items[i]);
    }
    dst.count = src->count;
    return dst;
}

/* ================================================================== */
/*  AST Visitor (depth-first traversal)                                */
/* ================================================================== */

static int walk_node(LpNode *node, LpVisitor *v);
static int walk_list(LpNodeList *list, LpVisitor *v);

static int walk_list(LpNodeList *list, LpVisitor *v) {
    if (!list || !list->items) return 0;
    for (int i = 0; i < list->count; i++) {
        if (list->items[i]) {
            int rc = walk_node(list->items[i], v);
            if (rc == 2) return 2;
        }
    }
    return 0;
}

#define WALK_NODE(n) do { if (n) { int _rc = walk_node(n, v); if (_rc == 2) return 2; } } while(0)
#define WALK_LIST(l) do { int _rc = walk_list(&(l), v); if (_rc == 2) return 2; } while(0)

static int walk_children(LpNode *node, LpVisitor *v) {
    switch (node->kind) {
        case LP_STMT_SELECT:
            WALK_LIST(node->u.select.result_columns);
            WALK_NODE(node->u.select.from);
            WALK_NODE(node->u.select.where);
            WALK_LIST(node->u.select.group_by);
            WALK_NODE(node->u.select.having);
            WALK_LIST(node->u.select.window_defs);
            WALK_LIST(node->u.select.order_by);
            WALK_NODE(node->u.select.limit);
            WALK_NODE(node->u.select.with);
            break;

        case LP_COMPOUND_SELECT:
            WALK_NODE(node->u.compound.left);
            WALK_NODE(node->u.compound.right);
            break;

        case LP_STMT_INSERT:
            WALK_LIST(node->u.insert.columns);
            WALK_NODE(node->u.insert.source);
            WALK_NODE(node->u.insert.upsert);
            WALK_LIST(node->u.insert.returning);
            break;

        case LP_STMT_UPDATE:
            WALK_LIST(node->u.update.set_clauses);
            WALK_NODE(node->u.update.from);
            WALK_NODE(node->u.update.where);
            WALK_LIST(node->u.update.order_by);
            WALK_NODE(node->u.update.limit);
            WALK_LIST(node->u.update.returning);
            break;

        case LP_STMT_DELETE:
            WALK_NODE(node->u.del.where);
            WALK_LIST(node->u.del.order_by);
            WALK_NODE(node->u.del.limit);
            WALK_LIST(node->u.del.returning);
            break;

        case LP_STMT_CREATE_TABLE:
            WALK_LIST(node->u.create_table.columns);
            WALK_LIST(node->u.create_table.constraints);
            WALK_NODE(node->u.create_table.as_select);
            break;

        case LP_STMT_CREATE_INDEX:
            WALK_LIST(node->u.create_index.columns);
            WALK_NODE(node->u.create_index.where);
            break;

        case LP_STMT_CREATE_VIEW:
            WALK_LIST(node->u.create_view.col_names);
            WALK_NODE(node->u.create_view.select);
            break;

        case LP_STMT_CREATE_TRIGGER:
            WALK_LIST(node->u.create_trigger.update_columns);
            WALK_NODE(node->u.create_trigger.when);
            WALK_LIST(node->u.create_trigger.body);
            break;

        case LP_STMT_CREATE_VTABLE:
            break;

        case LP_STMT_DROP:
            break;

        case LP_STMT_BEGIN:
        case LP_STMT_COMMIT:
        case LP_STMT_ROLLBACK:
        case LP_STMT_SAVEPOINT:
        case LP_STMT_RELEASE:
        case LP_STMT_ROLLBACK_TO:
            break;

        case LP_STMT_PRAGMA:
            break;

        case LP_STMT_VACUUM:
            WALK_NODE(node->u.vacuum.into);
            break;

        case LP_STMT_REINDEX:
        case LP_STMT_ANALYZE:
            break;

        case LP_STMT_ATTACH:
            WALK_NODE(node->u.attach.filename);
            WALK_NODE(node->u.attach.dbname);
            WALK_NODE(node->u.attach.key);
            break;

        case LP_STMT_DETACH:
            WALK_NODE(node->u.detach.dbname);
            break;

        case LP_STMT_ALTER:
            WALK_NODE(node->u.alter.column_def);
            break;

        case LP_STMT_EXPLAIN:
            WALK_NODE(node->u.explain.stmt);
            break;

        case LP_EXPR_LITERAL_INT:
        case LP_EXPR_LITERAL_FLOAT:
        case LP_EXPR_LITERAL_STRING:
        case LP_EXPR_LITERAL_BLOB:
        case LP_EXPR_LITERAL_NULL:
        case LP_EXPR_LITERAL_BOOL:
            break;

        case LP_EXPR_COLUMN_REF:
            break;

        case LP_EXPR_BINARY_OP:
            WALK_NODE(node->u.binary.left);
            WALK_NODE(node->u.binary.right);
            WALK_NODE(node->u.binary.escape);
            break;

        case LP_EXPR_UNARY_OP:
            WALK_NODE(node->u.unary.operand);
            break;

        case LP_EXPR_FUNCTION:
            WALK_LIST(node->u.function.args);
            WALK_LIST(node->u.function.order_by);
            WALK_NODE(node->u.function.filter);
            WALK_NODE(node->u.function.over);
            break;

        case LP_EXPR_CAST:
            WALK_NODE(node->u.cast.expr);
            break;

        case LP_EXPR_COLLATE:
            WALK_NODE(node->u.collate.expr);
            break;

        case LP_EXPR_BETWEEN:
            WALK_NODE(node->u.between.expr);
            WALK_NODE(node->u.between.low);
            WALK_NODE(node->u.between.high);
            break;

        case LP_EXPR_IN:
            WALK_NODE(node->u.in.expr);
            WALK_LIST(node->u.in.values);
            WALK_NODE(node->u.in.select);
            break;

        case LP_EXPR_EXISTS:
            WALK_NODE(node->u.exists.select);
            break;

        case LP_EXPR_SUBQUERY:
            WALK_NODE(node->u.subquery.select);
            break;

        case LP_EXPR_CASE:
            WALK_NODE(node->u.case_.operand);
            WALK_LIST(node->u.case_.when_exprs);
            WALK_NODE(node->u.case_.else_expr);
            break;

        case LP_EXPR_RAISE:
            WALK_NODE(node->u.raise.message);
            break;

        case LP_EXPR_VARIABLE:
            break;

        case LP_EXPR_STAR:
            break;

        case LP_EXPR_VECTOR:
            WALK_LIST(node->u.vector.values);
            break;

        case LP_RESULT_COLUMN:
            WALK_NODE(node->u.result_column.expr);
            break;

        case LP_FROM_TABLE:
            WALK_LIST(node->u.from_table.func_args);
            break;

        case LP_FROM_SUBQUERY:
            WALK_NODE(node->u.from_subquery.select);
            break;

        case LP_JOIN_CLAUSE:
            WALK_NODE(node->u.join.left);
            WALK_NODE(node->u.join.right);
            WALK_NODE(node->u.join.on_expr);
            WALK_LIST(node->u.join.using_columns);
            break;

        case LP_ORDER_TERM:
            WALK_NODE(node->u.order_term.expr);
            break;

        case LP_LIMIT:
            WALK_NODE(node->u.limit.count);
            WALK_NODE(node->u.limit.offset);
            break;

        case LP_COLUMN_DEF:
            WALK_LIST(node->u.column_def.constraints);
            break;

        case LP_COLUMN_CONSTRAINT:
            WALK_NODE(node->u.column_constraint.expr);
            WALK_NODE(node->u.column_constraint.fk);
            break;

        case LP_TABLE_CONSTRAINT:
            WALK_LIST(node->u.table_constraint.columns);
            WALK_NODE(node->u.table_constraint.expr);
            WALK_NODE(node->u.table_constraint.fk);
            break;

        case LP_FOREIGN_KEY:
            WALK_LIST(node->u.foreign_key.columns);
            break;

        case LP_CTE:
            WALK_LIST(node->u.cte.columns);
            WALK_NODE(node->u.cte.select);
            break;

        case LP_WITH:
            WALK_LIST(node->u.with.ctes);
            break;

        case LP_UPSERT:
            WALK_LIST(node->u.upsert.conflict_target);
            WALK_NODE(node->u.upsert.conflict_where);
            WALK_LIST(node->u.upsert.set_clauses);
            WALK_NODE(node->u.upsert.where);
            WALK_NODE(node->u.upsert.next);
            break;

        case LP_RETURNING:
            WALK_LIST(node->u.returning.columns);
            break;

        case LP_WINDOW_DEF:
            WALK_LIST(node->u.window_def.partition_by);
            WALK_LIST(node->u.window_def.order_by);
            WALK_NODE(node->u.window_def.frame);
            break;

        case LP_WINDOW_FRAME:
            WALK_NODE(node->u.window_frame.start);
            WALK_NODE(node->u.window_frame.end);
            break;

        case LP_FRAME_BOUND:
            WALK_NODE(node->u.frame_bound.expr);
            break;

        case LP_SET_CLAUSE:
            WALK_LIST(node->u.set_clause.columns);
            WALK_NODE(node->u.set_clause.expr);
            break;

        case LP_INDEX_COLUMN:
            WALK_NODE(node->u.index_column.expr);
            break;

        case LP_VALUES_ROW:
            WALK_LIST(node->u.values_row.values);
            break;

        case LP_TRIGGER_CMD:
            WALK_NODE(node->u.trigger_cmd.stmt);
            break;

        case LP_NODE_KIND_COUNT:
            break;
    }
    return 0;
}

static int walk_node(LpNode *node, LpVisitor *v) {
    if (!node) return 0;

    int rc = 0;
    if (v->enter) {
        rc = v->enter(v, node);
        if (rc == 2) return 2;
    }

    if (rc == 0) {
        int crc = walk_children(node, v);
        if (crc == 2) return 2;
    }

    if (v->leave) {
        rc = v->leave(v, node);
        if (rc == 2) return 2;
    }

    return 0;
}

void lp_ast_walk(LpNode *root, LpVisitor *visitor) {
    if (!root || !visitor) return;
    walk_node(root, visitor);
}

/* ================================================================== */
/*  JSON Serializer                                                    */
/* ================================================================== */

static void json_indent(LpBuf *out, int depth, int pretty) {
    if (!pretty) return;
    lp_buf_putc(out, '\n');
    for (int i = 0; i < depth * 2; i++) lp_buf_putc(out, ' ');
}

static void json_string(LpBuf *out, const char *s) {
    if (!s) {
        lp_buf_puts(out, "null");
        return;
    }
    lp_buf_putc(out, '"');
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  lp_buf_puts(out, "\\\""); break;
            case '\\': lp_buf_puts(out, "\\\\"); break;
            case '\b': lp_buf_puts(out, "\\b"); break;
            case '\f': lp_buf_puts(out, "\\f"); break;
            case '\n': lp_buf_puts(out, "\\n"); break;
            case '\r': lp_buf_puts(out, "\\r"); break;
            case '\t': lp_buf_puts(out, "\\t"); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    lp_buf_printf(out, "\\u%04x", (unsigned char)*p);
                } else {
                    lp_buf_putc(out, *p);
                }
                break;
        }
    }
    lp_buf_putc(out, '"');
}

static void json_node(LpNode *node, LpBuf *out, int depth, int pretty);

static void json_list(LpNodeList *list, LpBuf *out, int depth, int pretty) {
    lp_buf_putc(out, '[');
    if (list && list->items && list->count > 0) {
        for (int i = 0; i < list->count; i++) {
            if (i > 0) lp_buf_putc(out, ',');
            json_indent(out, depth + 1, pretty);
            json_node(list->items[i], out, depth + 1, pretty);
        }
        json_indent(out, depth, pretty);
    }
    lp_buf_putc(out, ']');
}

/* Helper macros for JSON output */
#define J_SEP() do { lp_buf_putc(out, ','); json_indent(out, depth+1, pretty); } while(0)
#define J_KEY(k) do { json_string(out, k); lp_buf_putc(out, ':'); if (pretty) lp_buf_putc(out, ' '); } while(0)
#define J_STR(k, v) do { if (v) { J_SEP(); J_KEY(k); json_string(out, v); } } while(0)
#define J_INT(k, v) do { J_SEP(); J_KEY(k); lp_buf_printf(out, "%d", v); } while(0)
#define J_BOOL(k, v) do { J_SEP(); J_KEY(k); lp_buf_puts(out, (v) ? "true" : "false"); } while(0)
#define J_NODE(k, n) do { if (n) { J_SEP(); J_KEY(k); json_node(n, out, depth+1, pretty); } } while(0)
#define J_LIST(k, l) do { if ((l).count > 0) { J_SEP(); J_KEY(k); json_list(&(l), out, depth+1, pretty); } } while(0)

static const char *drop_type_name(LpDropType t) {
    switch (t) {
        case LP_DROP_TABLE:   return "TABLE";
        case LP_DROP_INDEX:   return "INDEX";
        case LP_DROP_VIEW:    return "VIEW";
        case LP_DROP_TRIGGER: return "TRIGGER";
    }
    return "UNKNOWN";
}

static const char *alter_type_name(LpAlterType t) {
    switch (t) {
        case LP_ALTER_RENAME_TABLE:   return "RENAME_TABLE";
        case LP_ALTER_ADD_COLUMN:     return "ADD_COLUMN";
        case LP_ALTER_DROP_COLUMN:    return "DROP_COLUMN";
        case LP_ALTER_RENAME_COLUMN:  return "RENAME_COLUMN";
    }
    return "UNKNOWN";
}

static const char *col_cons_type_name(LpColConsType t) {
    switch (t) {
        case LP_CCONS_PRIMARY_KEY: return "PRIMARY_KEY";
        case LP_CCONS_NOT_NULL:    return "NOT_NULL";
        case LP_CCONS_UNIQUE:      return "UNIQUE";
        case LP_CCONS_CHECK:       return "CHECK";
        case LP_CCONS_DEFAULT:     return "DEFAULT";
        case LP_CCONS_REFERENCES:  return "REFERENCES";
        case LP_CCONS_COLLATE:     return "COLLATE";
        case LP_CCONS_GENERATED:   return "GENERATED";
        case LP_CCONS_NULL:        return "NULL";
    }
    return "UNKNOWN";
}

static const char *table_cons_type_name(LpTableConsType t) {
    switch (t) {
        case LP_TCONS_PRIMARY_KEY:  return "PRIMARY_KEY";
        case LP_TCONS_UNIQUE:       return "UNIQUE";
        case LP_TCONS_CHECK:        return "CHECK";
        case LP_TCONS_FOREIGN_KEY:  return "FOREIGN_KEY";
    }
    return "UNKNOWN";
}

static const char *fk_action_name(LpFKAction a) {
    switch (a) {
        case LP_FK_NO_ACTION:   return "NO_ACTION";
        case LP_FK_SET_NULL:    return "SET_NULL";
        case LP_FK_SET_DEFAULT: return "SET_DEFAULT";
        case LP_FK_CASCADE:     return "CASCADE";
        case LP_FK_RESTRICT:    return "RESTRICT";
    }
    return "UNKNOWN";
}

static const char *compound_op_name(LpCompoundOp op) {
    switch (op) {
        case LP_COMPOUND_UNION:     return "UNION";
        case LP_COMPOUND_UNION_ALL: return "UNION_ALL";
        case LP_COMPOUND_INTERSECT: return "INTERSECT";
        case LP_COMPOUND_EXCEPT:    return "EXCEPT";
    }
    return "UNKNOWN";
}

static const char *trigger_time_name(LpTriggerTime t) {
    switch (t) {
        case LP_TRIGGER_BEFORE:     return "BEFORE";
        case LP_TRIGGER_AFTER:      return "AFTER";
        case LP_TRIGGER_INSTEAD_OF: return "INSTEAD_OF";
    }
    return "UNKNOWN";
}

static const char *trigger_event_name(LpTriggerEvent e) {
    switch (e) {
        case LP_TRIGGER_INSERT: return "INSERT";
        case LP_TRIGGER_UPDATE: return "UPDATE";
        case LP_TRIGGER_DELETE: return "DELETE";
    }
    return "UNKNOWN";
}

static const char *frame_type_name(LpFrameType t) {
    switch (t) {
        case LP_FRAME_ROWS:   return "ROWS";
        case LP_FRAME_RANGE:  return "RANGE";
        case LP_FRAME_GROUPS: return "GROUPS";
    }
    return "UNKNOWN";
}

static const char *bound_type_name(LpBoundType t) {
    switch (t) {
        case LP_BOUND_CURRENT_ROW:          return "CURRENT_ROW";
        case LP_BOUND_UNBOUNDED_PRECEDING:  return "UNBOUNDED_PRECEDING";
        case LP_BOUND_UNBOUNDED_FOLLOWING:  return "UNBOUNDED_FOLLOWING";
        case LP_BOUND_PRECEDING:            return "PRECEDING";
        case LP_BOUND_FOLLOWING:            return "FOLLOWING";
    }
    return "UNKNOWN";
}

static const char *exclude_type_name(LpExcludeType t) {
    switch (t) {
        case LP_EXCLUDE_NONE:        return "NONE";
        case LP_EXCLUDE_NO_OTHERS:   return "NO_OTHERS";
        case LP_EXCLUDE_CURRENT_ROW: return "CURRENT_ROW";
        case LP_EXCLUDE_GROUP:       return "GROUP";
        case LP_EXCLUDE_TIES:        return "TIES";
    }
    return "UNKNOWN";
}

static const char *raise_type_name(LpRaiseType t) {
    switch (t) {
        case LP_RAISE_IGNORE:   return "IGNORE";
        case LP_RAISE_ROLLBACK: return "ROLLBACK";
        case LP_RAISE_ABORT:    return "ABORT";
        case LP_RAISE_FAIL:     return "FAIL";
    }
    return "UNKNOWN";
}

static const char *materialized_name(LpMaterialized m) {
    switch (m) {
        case LP_MATERIALIZE_ANY: return "ANY";
        case LP_MATERIALIZE_YES: return "YES";
        case LP_MATERIALIZE_NO:  return "NO";
    }
    return "UNKNOWN";
}

static const char *conflict_name(int c) {
    switch (c) {
        case LP_CONFLICT_NONE:     return "NONE";
        case LP_CONFLICT_ROLLBACK: return "ROLLBACK";
        case LP_CONFLICT_ABORT:    return "ABORT";
        case LP_CONFLICT_FAIL:     return "FAIL";
        case LP_CONFLICT_IGNORE:   return "IGNORE";
        case LP_CONFLICT_REPLACE:  return "REPLACE";
    }
    return "UNKNOWN";
}

static const char *sort_order_name(int s) {
    switch (s) {
        case LP_SORT_ASC:       return "ASC";
        case LP_SORT_DESC:      return "DESC";
        case LP_SORT_UNDEFINED: return "UNDEFINED";
    }
    return "UNKNOWN";
}

static void json_node(LpNode *node, LpBuf *out, int depth, int pretty) {
    if (!node) {
        lp_buf_puts(out, "null");
        return;
    }

    lp_buf_putc(out, '{');
    json_indent(out, depth + 1, pretty);
    J_KEY("kind");
    json_string(out, lp_node_kind_name(node->kind));

    /* Source position */
    if (node->pos.line > 0) {
        J_SEP(); J_KEY("pos");
        lp_buf_printf(out, "{\"line\": %u, \"col\": %u, \"offset\": %u}",
                node->pos.line, node->pos.col, node->pos.offset);
    }

    switch (node->kind) {
        case LP_STMT_SELECT:
            J_BOOL("distinct", node->u.select.distinct);
            J_LIST("result_columns", node->u.select.result_columns);
            J_NODE("from", node->u.select.from);
            J_NODE("where", node->u.select.where);
            J_LIST("group_by", node->u.select.group_by);
            J_NODE("having", node->u.select.having);
            J_LIST("window_defs", node->u.select.window_defs);
            J_LIST("order_by", node->u.select.order_by);
            J_NODE("limit", node->u.select.limit);
            J_NODE("with", node->u.select.with);
            break;

        case LP_COMPOUND_SELECT:
            J_SEP(); J_KEY("op"); json_string(out, compound_op_name(node->u.compound.op));
            J_NODE("left", node->u.compound.left);
            J_NODE("right", node->u.compound.right);
            break;

        case LP_STMT_INSERT:
            J_STR("table", node->u.insert.table);
            J_STR("schema", node->u.insert.schema);
            J_STR("alias", node->u.insert.alias);
            if (node->u.insert.or_conflict != LP_CONFLICT_NONE) {
                J_SEP(); J_KEY("or_conflict"); json_string(out, conflict_name(node->u.insert.or_conflict));
            }
            J_LIST("columns", node->u.insert.columns);
            J_NODE("source", node->u.insert.source);
            J_NODE("upsert", node->u.insert.upsert);
            J_LIST("returning", node->u.insert.returning);
            break;

        case LP_STMT_UPDATE:
            J_STR("table", node->u.update.table);
            J_STR("schema", node->u.update.schema);
            J_STR("alias", node->u.update.alias);
            if (node->u.update.or_conflict != LP_CONFLICT_NONE) {
                J_SEP(); J_KEY("or_conflict"); json_string(out, conflict_name(node->u.update.or_conflict));
            }
            J_LIST("set_clauses", node->u.update.set_clauses);
            J_NODE("from", node->u.update.from);
            J_NODE("where", node->u.update.where);
            J_LIST("order_by", node->u.update.order_by);
            J_NODE("limit", node->u.update.limit);
            J_LIST("returning", node->u.update.returning);
            break;

        case LP_STMT_DELETE:
            J_STR("table", node->u.del.table);
            J_STR("schema", node->u.del.schema);
            J_STR("alias", node->u.del.alias);
            J_NODE("where", node->u.del.where);
            J_LIST("order_by", node->u.del.order_by);
            J_NODE("limit", node->u.del.limit);
            J_LIST("returning", node->u.del.returning);
            break;

        case LP_STMT_CREATE_TABLE:
            J_STR("name", node->u.create_table.name);
            J_STR("schema", node->u.create_table.schema);
            J_BOOL("if_not_exists", node->u.create_table.if_not_exists);
            J_BOOL("temp", node->u.create_table.temp);
            J_INT("options", node->u.create_table.options);
            J_LIST("columns", node->u.create_table.columns);
            J_LIST("constraints", node->u.create_table.constraints);
            J_NODE("as_select", node->u.create_table.as_select);
            break;

        case LP_STMT_CREATE_INDEX:
            J_STR("name", node->u.create_index.name);
            J_STR("schema", node->u.create_index.schema);
            J_STR("table", node->u.create_index.table);
            J_BOOL("is_unique", node->u.create_index.is_unique);
            J_BOOL("if_not_exists", node->u.create_index.if_not_exists);
            J_LIST("columns", node->u.create_index.columns);
            J_NODE("where", node->u.create_index.where);
            break;

        case LP_STMT_CREATE_VIEW:
            J_STR("name", node->u.create_view.name);
            J_STR("schema", node->u.create_view.schema);
            J_BOOL("if_not_exists", node->u.create_view.if_not_exists);
            J_BOOL("temp", node->u.create_view.temp);
            J_LIST("col_names", node->u.create_view.col_names);
            J_NODE("select", node->u.create_view.select);
            break;

        case LP_STMT_CREATE_TRIGGER:
            J_STR("name", node->u.create_trigger.name);
            J_STR("schema", node->u.create_trigger.schema);
            J_BOOL("if_not_exists", node->u.create_trigger.if_not_exists);
            J_BOOL("temp", node->u.create_trigger.temp);
            J_SEP(); J_KEY("time"); json_string(out, trigger_time_name(node->u.create_trigger.time));
            J_SEP(); J_KEY("event"); json_string(out, trigger_event_name(node->u.create_trigger.event));
            J_STR("table_name", node->u.create_trigger.table_name);
            J_LIST("update_columns", node->u.create_trigger.update_columns);
            J_NODE("when", node->u.create_trigger.when);
            J_LIST("body", node->u.create_trigger.body);
            break;

        case LP_STMT_CREATE_VTABLE:
            J_STR("name", node->u.create_vtable.name);
            J_STR("schema", node->u.create_vtable.schema);
            J_BOOL("if_not_exists", node->u.create_vtable.if_not_exists);
            J_STR("module", node->u.create_vtable.module);
            J_STR("module_args", node->u.create_vtable.module_args);
            break;

        case LP_STMT_DROP:
            J_SEP(); J_KEY("target"); json_string(out, drop_type_name(node->u.drop.target));
            J_STR("name", node->u.drop.name);
            J_STR("schema", node->u.drop.schema);
            J_BOOL("if_exists", node->u.drop.if_exists);
            break;

        case LP_STMT_BEGIN:
            J_INT("trans_type", node->u.begin.trans_type);
            break;

        case LP_STMT_COMMIT:
        case LP_STMT_ROLLBACK:
            break;

        case LP_STMT_SAVEPOINT:
        case LP_STMT_RELEASE:
        case LP_STMT_ROLLBACK_TO:
            J_STR("name", node->u.savepoint.name);
            break;

        case LP_STMT_PRAGMA:
            J_STR("name", node->u.pragma.name);
            J_STR("schema", node->u.pragma.schema);
            J_STR("value", node->u.pragma.value);
            if (node->u.pragma.is_neg) J_BOOL("is_neg", node->u.pragma.is_neg);
            break;

        case LP_STMT_VACUUM:
            J_STR("schema", node->u.vacuum.schema);
            J_NODE("into", node->u.vacuum.into);
            break;

        case LP_STMT_REINDEX:
        case LP_STMT_ANALYZE:
            J_STR("name", node->u.reindex.name);
            J_STR("schema", node->u.reindex.schema);
            break;

        case LP_STMT_ATTACH:
            J_NODE("filename", node->u.attach.filename);
            J_NODE("dbname", node->u.attach.dbname);
            J_NODE("key", node->u.attach.key);
            break;

        case LP_STMT_DETACH:
            J_NODE("dbname", node->u.detach.dbname);
            break;

        case LP_STMT_ALTER:
            J_SEP(); J_KEY("alter_type"); json_string(out, alter_type_name(node->u.alter.alter_type));
            J_STR("table_name", node->u.alter.table_name);
            J_STR("schema", node->u.alter.schema);
            J_STR("column_name", node->u.alter.column_name);
            J_STR("new_name", node->u.alter.new_name);
            J_NODE("column_def", node->u.alter.column_def);
            break;

        case LP_STMT_EXPLAIN:
            J_BOOL("is_query_plan", node->u.explain.is_query_plan);
            J_NODE("stmt", node->u.explain.stmt);
            break;

        case LP_EXPR_LITERAL_INT:
        case LP_EXPR_LITERAL_FLOAT:
        case LP_EXPR_LITERAL_STRING:
        case LP_EXPR_LITERAL_BLOB:
        case LP_EXPR_LITERAL_BOOL:
            J_STR("value", node->u.literal.value);
            break;

        case LP_EXPR_LITERAL_NULL:
            break;

        case LP_EXPR_COLUMN_REF:
            J_STR("schema", node->u.column_ref.schema);
            J_STR("table", node->u.column_ref.table);
            J_STR("column", node->u.column_ref.column);
            break;

        case LP_EXPR_BINARY_OP:
            J_SEP(); J_KEY("op"); json_string(out, lp_binop_name(node->u.binary.op));
            J_NODE("left", node->u.binary.left);
            J_NODE("right", node->u.binary.right);
            J_NODE("escape", node->u.binary.escape);
            break;

        case LP_EXPR_UNARY_OP:
            J_SEP(); J_KEY("op"); json_string(out, lp_unaryop_name(node->u.unary.op));
            J_NODE("operand", node->u.unary.operand);
            break;

        case LP_EXPR_FUNCTION:
            J_STR("name", node->u.function.name);
            J_BOOL("distinct", node->u.function.distinct);
            if (node->u.function.is_ctime_kw) J_BOOL("is_ctime_kw", node->u.function.is_ctime_kw);
            J_LIST("args", node->u.function.args);
            J_LIST("order_by", node->u.function.order_by);
            J_NODE("filter", node->u.function.filter);
            J_NODE("over", node->u.function.over);
            break;

        case LP_EXPR_CAST:
            J_NODE("expr", node->u.cast.expr);
            J_STR("type_name", node->u.cast.type_name);
            break;

        case LP_EXPR_COLLATE:
            J_NODE("expr", node->u.collate.expr);
            J_STR("collation", node->u.collate.collation);
            break;

        case LP_EXPR_BETWEEN:
            J_BOOL("is_not", node->u.between.is_not);
            J_NODE("expr", node->u.between.expr);
            J_NODE("low", node->u.between.low);
            J_NODE("high", node->u.between.high);
            break;

        case LP_EXPR_IN:
            J_BOOL("is_not", node->u.in.is_not);
            J_NODE("expr", node->u.in.expr);
            J_LIST("values", node->u.in.values);
            J_NODE("select", node->u.in.select);
            break;

        case LP_EXPR_EXISTS:
            J_NODE("select", node->u.exists.select);
            break;

        case LP_EXPR_SUBQUERY:
            J_NODE("select", node->u.subquery.select);
            break;

        case LP_EXPR_CASE:
            J_NODE("operand", node->u.case_.operand);
            J_LIST("when_exprs", node->u.case_.when_exprs);
            J_NODE("else_expr", node->u.case_.else_expr);
            break;

        case LP_EXPR_RAISE:
            J_SEP(); J_KEY("type"); json_string(out, raise_type_name(node->u.raise.type));
            J_NODE("message", node->u.raise.message);
            break;

        case LP_EXPR_VARIABLE:
            J_STR("name", node->u.variable.name);
            break;

        case LP_EXPR_STAR:
            J_STR("table", node->u.star.table);
            break;

        case LP_EXPR_VECTOR:
            J_LIST("values", node->u.vector.values);
            break;

        case LP_RESULT_COLUMN:
            J_NODE("expr", node->u.result_column.expr);
            J_STR("alias", node->u.result_column.alias);
            break;

        case LP_FROM_TABLE:
            J_STR("name", node->u.from_table.name);
            J_STR("schema", node->u.from_table.schema);
            J_STR("alias", node->u.from_table.alias);
            J_STR("indexed_by", node->u.from_table.indexed_by);
            if (node->u.from_table.not_indexed) J_BOOL("not_indexed", node->u.from_table.not_indexed);
            J_LIST("func_args", node->u.from_table.func_args);
            break;

        case LP_FROM_SUBQUERY:
            J_NODE("select", node->u.from_subquery.select);
            J_STR("alias", node->u.from_subquery.alias);
            break;

        case LP_JOIN_CLAUSE:
            J_INT("join_type", node->u.join.join_type);
            J_NODE("left", node->u.join.left);
            J_NODE("right", node->u.join.right);
            J_NODE("on_expr", node->u.join.on_expr);
            J_LIST("using_columns", node->u.join.using_columns);
            break;

        case LP_ORDER_TERM:
            J_NODE("expr", node->u.order_term.expr);
            J_SEP(); J_KEY("direction"); json_string(out, sort_order_name(node->u.order_term.direction));
            J_INT("nulls", node->u.order_term.nulls);
            break;

        case LP_LIMIT:
            J_NODE("count", node->u.limit.count);
            J_NODE("offset", node->u.limit.offset);
            break;

        case LP_COLUMN_DEF:
            J_STR("name", node->u.column_def.name);
            J_STR("type_name", node->u.column_def.type_name);
            J_LIST("constraints", node->u.column_def.constraints);
            break;

        case LP_COLUMN_CONSTRAINT:
            J_SEP(); J_KEY("constraint_type"); json_string(out, col_cons_type_name(node->u.column_constraint.type));
            J_STR("name", node->u.column_constraint.name);
            J_NODE("expr", node->u.column_constraint.expr);
            J_NODE("fk", node->u.column_constraint.fk);
            J_STR("collation", node->u.column_constraint.collation);
            if (node->u.column_constraint.sort_order != 0) {
                J_SEP(); J_KEY("sort_order"); json_string(out, sort_order_name(node->u.column_constraint.sort_order));
            }
            if (node->u.column_constraint.conflict_action != LP_CONFLICT_NONE) {
                J_SEP(); J_KEY("conflict_action"); json_string(out, conflict_name(node->u.column_constraint.conflict_action));
            }
            if (node->u.column_constraint.is_autoinc) J_BOOL("is_autoinc", node->u.column_constraint.is_autoinc);
            if (node->u.column_constraint.generated_type) J_INT("generated_type", node->u.column_constraint.generated_type);
            break;

        case LP_TABLE_CONSTRAINT:
            J_SEP(); J_KEY("constraint_type"); json_string(out, table_cons_type_name(node->u.table_constraint.type));
            J_STR("name", node->u.table_constraint.name);
            J_LIST("columns", node->u.table_constraint.columns);
            J_NODE("expr", node->u.table_constraint.expr);
            J_NODE("fk", node->u.table_constraint.fk);
            if (node->u.table_constraint.conflict_action != LP_CONFLICT_NONE) {
                J_SEP(); J_KEY("conflict_action"); json_string(out, conflict_name(node->u.table_constraint.conflict_action));
            }
            if (node->u.table_constraint.is_autoinc) J_BOOL("is_autoinc", node->u.table_constraint.is_autoinc);
            break;

        case LP_FOREIGN_KEY:
            J_STR("table", node->u.foreign_key.table);
            J_LIST("columns", node->u.foreign_key.columns);
            J_SEP(); J_KEY("on_delete"); json_string(out, fk_action_name(node->u.foreign_key.on_delete));
            J_SEP(); J_KEY("on_update"); json_string(out, fk_action_name(node->u.foreign_key.on_update));
            J_INT("deferrable", node->u.foreign_key.deferrable);
            break;

        case LP_CTE:
            J_STR("name", node->u.cte.name);
            J_LIST("columns", node->u.cte.columns);
            J_NODE("select", node->u.cte.select);
            J_SEP(); J_KEY("materialized"); json_string(out, materialized_name(node->u.cte.materialized));
            break;

        case LP_WITH:
            J_BOOL("recursive", node->u.with.recursive);
            J_LIST("ctes", node->u.with.ctes);
            break;

        case LP_UPSERT:
            J_LIST("conflict_target", node->u.upsert.conflict_target);
            J_NODE("conflict_where", node->u.upsert.conflict_where);
            J_LIST("set_clauses", node->u.upsert.set_clauses);
            J_NODE("where", node->u.upsert.where);
            J_NODE("next", node->u.upsert.next);
            break;

        case LP_RETURNING:
            J_LIST("columns", node->u.returning.columns);
            break;

        case LP_WINDOW_DEF:
            J_STR("name", node->u.window_def.name);
            J_STR("base_name", node->u.window_def.base_name);
            J_LIST("partition_by", node->u.window_def.partition_by);
            J_LIST("order_by", node->u.window_def.order_by);
            J_NODE("frame", node->u.window_def.frame);
            break;

        case LP_WINDOW_FRAME:
            J_SEP(); J_KEY("frame_type"); json_string(out, frame_type_name(node->u.window_frame.type));
            J_NODE("start", node->u.window_frame.start);
            J_NODE("end", node->u.window_frame.end);
            J_SEP(); J_KEY("exclude"); json_string(out, exclude_type_name(node->u.window_frame.exclude));
            break;

        case LP_FRAME_BOUND:
            J_SEP(); J_KEY("bound_type"); json_string(out, bound_type_name(node->u.frame_bound.type));
            J_NODE("expr", node->u.frame_bound.expr);
            break;

        case LP_SET_CLAUSE:
            J_STR("column", node->u.set_clause.column);
            J_LIST("columns", node->u.set_clause.columns);
            J_NODE("expr", node->u.set_clause.expr);
            break;

        case LP_INDEX_COLUMN:
            J_NODE("expr", node->u.index_column.expr);
            J_STR("collation", node->u.index_column.collation);
            J_SEP(); J_KEY("sort_order"); json_string(out, sort_order_name(node->u.index_column.sort_order));
            break;

        case LP_VALUES_ROW:
            J_LIST("values", node->u.values_row.values);
            break;

        case LP_TRIGGER_CMD:
            J_NODE("stmt", node->u.trigger_cmd.stmt);
            break;

        case LP_NODE_KIND_COUNT:
            break;
    }

    json_indent(out, depth, pretty);
    lp_buf_putc(out, '}');
}

char *lp_ast_to_json(LpNode *root, arena_t *arena, int pretty) {
    LpBuf buf;
    lp_buf_init(&buf);
    if (!root) {
        lp_buf_puts(&buf, "null");
    } else {
        json_node(root, &buf, 0, pretty);
    }
    if (pretty) lp_buf_putc(&buf, '\n');
    return lp_buf_finish(&buf, arena);
}

const char *lp_error_code_name(LpErrorCode code) {
    switch (code) {
        case LP_ERR_SYNTAX:         return "syntax";
        case LP_ERR_ILLEGAL_TOKEN:  return "illegal_token";
        case LP_ERR_INCOMPLETE:     return "incomplete";
        case LP_ERR_STACK_OVERFLOW: return "stack_overflow";
    }
    return "unknown";
}

char *lp_parse_result_to_json(LpParseResult *result, arena_t *arena, int pretty) {
    if (!result) return NULL;
    LpBuf buf;
    lp_buf_init(&buf);

    lp_buf_putc(&buf, '{');
    if (pretty) lp_buf_putc(&buf, '\n');

    /* "statements": [...] */
    json_indent(&buf, 1, pretty);
    lp_buf_puts(&buf, "\"statements\":");
    if (pretty) lp_buf_putc(&buf, ' ');
    json_list(&result->stmts, &buf, 1, pretty);
    lp_buf_putc(&buf, ',');
    if (pretty) lp_buf_putc(&buf, '\n');

    /* "errors": [...] */
    json_indent(&buf, 1, pretty);
    lp_buf_puts(&buf, "\"errors\":");
    if (pretty) lp_buf_putc(&buf, ' ');
    lp_buf_putc(&buf, '[');
    for (int i = 0; i < result->errors.count; i++) {
        if (i > 0) lp_buf_putc(&buf, ',');
        if (pretty) lp_buf_putc(&buf, '\n');
        json_indent(&buf, 2, pretty);
        LpError *e = &result->errors.items[i];
        lp_buf_putc(&buf, '{');
        if (pretty) lp_buf_putc(&buf, '\n');

        json_indent(&buf, 3, pretty);
        lp_buf_printf(&buf, "\"code\": \"%s\",", lp_error_code_name(e->code));
        if (pretty) lp_buf_putc(&buf, '\n');

        json_indent(&buf, 3, pretty);
        lp_buf_printf(&buf, "\"message\": ");
        json_string(&buf, e->message);
        lp_buf_putc(&buf, ',');
        if (pretty) lp_buf_putc(&buf, '\n');

        json_indent(&buf, 3, pretty);
        lp_buf_printf(&buf, "\"pos\": {\"line\": %u, \"col\": %u, \"offset\": %u},",
                      e->pos.line, e->pos.col, e->pos.offset);
        if (pretty) lp_buf_putc(&buf, '\n');

        json_indent(&buf, 3, pretty);
        lp_buf_printf(&buf, "\"end_pos\": {\"line\": %u, \"col\": %u, \"offset\": %u}",
                      e->end_pos.line, e->end_pos.col, e->end_pos.offset);
        if (pretty) lp_buf_putc(&buf, '\n');

        json_indent(&buf, 2, pretty);
        lp_buf_putc(&buf, '}');
    }
    if (result->errors.count > 0 && pretty) lp_buf_putc(&buf, '\n');
    if (result->errors.count > 0) json_indent(&buf, 1, pretty);
    lp_buf_putc(&buf, ']');
    if (pretty) lp_buf_putc(&buf, '\n');

    json_indent(&buf, 0, pretty);
    lp_buf_putc(&buf, '}');
    if (pretty) lp_buf_putc(&buf, '\n');

    return lp_buf_finish(&buf, arena);
}
