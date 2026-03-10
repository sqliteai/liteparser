/*
** lp_unparse.c — AST-to-SQL unparser.
**
** Converts an AST back to SQL text. Output is written to a char* via LpBuf.
*/

#include "liteparser.h"
#include "liteparser_internal.h"
#include <string.h>
#include <ctype.h>

/* ================================================================== */
/*  Identifier and literal output helpers                              */
/* ================================================================== */

/* SQL keywords that must be quoted when used as identifiers */
static int is_sql_keyword(const char *s) {
    static const char *const keywords[] = {
        "ABORT","ACTION","ADD","AFTER","ALL","ALTER","ALWAYS","ANALYZE","AND",
        "AS","ASC","ATTACH","AUTOINCREMENT","BEFORE","BEGIN","BETWEEN","BY",
        "CASCADE","CASE","CAST","CHECK","COLLATE","COLUMN","COMMIT","CONFLICT",
        "CONSTRAINT","CREATE","CROSS","CURRENT","CURRENT_DATE","CURRENT_TIME",
        "CURRENT_TIMESTAMP","DATABASE","DEFAULT","DEFERRABLE","DEFERRED",
        "DELETE","DESC","DETACH","DISTINCT","DO","DROP","EACH","ELSE","END",
        "ESCAPE","EXCEPT","EXCLUDE","EXCLUSIVE","EXISTS","EXPLAIN","FAIL",
        "FILTER","FIRST","FOLLOWING","FOR","FOREIGN","FROM","FULL","GENERATED",
        "GLOB","GROUP","GROUPS","HAVING","IF","IGNORE","IMMEDIATE","IN","INDEX",
        "INDEXED","INITIALLY","INNER","INSERT","INSTEAD","INTERSECT","INTO",
        "IS","ISNULL","JOIN","KEY","LAST","LEFT","LIKE","LIMIT","MATCH",
        "MATERIALIZED","NATURAL","NO","NOT","NOTHING","NOTNULL","NULL","NULLS",
        "OF","OFFSET","ON","OR","ORDER","OTHERS","OUTER","OVER","PARTITION",
        "PLAN","PRAGMA","PRECEDING","PRIMARY","QUERY","RAISE","RANGE",
        "RECURSIVE","REFERENCES","REGEXP","REINDEX","RELEASE","RENAME",
        "REPLACE","RESTRICT","RETURNING","RIGHT","ROLLBACK","ROW","ROWS",
        "SAVEPOINT","SELECT","SET","TABLE","TEMP","TEMPORARY","THEN","TIES",
        "TO","TRANSACTION","TRIGGER","UNBOUNDED","UNION","UNIQUE","UPDATE",
        "USING","VACUUM","VALUES","VIEW","VIRTUAL","WHEN","WHERE","WINDOW",
        "WITH","WITHIN","WITHOUT","FALSE","TRUE",
    };
    int n = sizeof(keywords)/sizeof(keywords[0]);
    for (int i = 0; i < n; i++) {
        if (strcasecmp(s, keywords[i]) == 0) return 1;
    }
    return 0;
}

static int needs_quoting(const char *s) {
    if (!s) return 0;
    if (!*s) return 1; /* empty string identifier "" must be quoted */
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 1;
    for (const char *p = s + 1; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 1;
    }
    if (is_sql_keyword(s)) return 1;
    return 0;
}

static void sql_ident(LpBuf *out, const char *s) {
    if (!s) return;
    if (needs_quoting(s)) {
        lp_buf_putc(out, '"');
        for (const char *p = s; *p; p++) {
            if (*p == '"') lp_buf_putc(out, '"'); /* escape double quotes */
            lp_buf_putc(out, *p);
        }
        lp_buf_putc(out, '"');
    } else {
        lp_buf_puts(out, s);
    }
}

/* schema.name */
static void sql_schema_name(LpBuf *out, const char *schema, const char *name) {
    if (schema) {
        sql_ident(out, schema);
        lp_buf_putc(out, '.');
    }
    sql_ident(out, name);
}

static void sql_str_lit(LpBuf *out, const char *s) {
    lp_buf_putc(out, '\'');
    if (s) {
        for (const char *p = s; *p; p++) {
            if (*p == '\'') lp_buf_putc(out, '\'');
            lp_buf_putc(out, *p);
        }
    }
    lp_buf_putc(out, '\'');
}

/* ================================================================== */
/*  Forward declarations                                               */
/* ================================================================== */

static void sql_node(LpNode *node, LpBuf *out);
static void sql_expr(LpNode *node, LpBuf *out, int parent_prec);
static void sql_select_body(LpNode *node, LpBuf *out);
static void sql_from(LpNode *node, LpBuf *out);

/* ================================================================== */
/*  Operator precedence (higher = tighter binding)                     */
/* ================================================================== */

static int binop_prec(LpBinOp op) {
    switch (op) {
        case LP_OP_OR:                          return 1;
        case LP_OP_AND:                         return 2;
        case LP_OP_IS: case LP_OP_ISNOT:
        case LP_OP_LIKE: case LP_OP_GLOB:
        case LP_OP_MATCH: case LP_OP_REGEXP:    return 3;
        case LP_OP_EQ: case LP_OP_NE:
        case LP_OP_LT: case LP_OP_LE:
        case LP_OP_GT: case LP_OP_GE:           return 4;
        case LP_OP_BITOR:                       return 5;
        case LP_OP_BITAND:                      return 6;
        case LP_OP_LSHIFT: case LP_OP_RSHIFT:   return 7;
        case LP_OP_ADD: case LP_OP_SUB:         return 8;
        case LP_OP_MUL: case LP_OP_DIV:
        case LP_OP_MOD:                         return 9;
        case LP_OP_CONCAT:                      return 10;
        case LP_OP_PTR: case LP_OP_PTR2:        return 11;
    }
    return 0;
}

static const char *binop_sql(LpBinOp op) {
    switch (op) {
        case LP_OP_ADD:     return " + ";
        case LP_OP_SUB:     return " - ";
        case LP_OP_MUL:     return " * ";
        case LP_OP_DIV:     return " / ";
        case LP_OP_MOD:     return " % ";
        case LP_OP_AND:     return " AND ";
        case LP_OP_OR:      return " OR ";
        case LP_OP_EQ:      return " = ";
        case LP_OP_NE:      return " != ";
        case LP_OP_LT:      return " < ";
        case LP_OP_LE:      return " <= ";
        case LP_OP_GT:      return " > ";
        case LP_OP_GE:      return " >= ";
        case LP_OP_BITAND:  return " & ";
        case LP_OP_BITOR:   return " | ";
        case LP_OP_LSHIFT:  return " << ";
        case LP_OP_RSHIFT:  return " >> ";
        case LP_OP_CONCAT:  return " || ";
        case LP_OP_IS:      return " IS ";
        case LP_OP_ISNOT:   return " IS NOT ";
        case LP_OP_LIKE:    return " LIKE ";
        case LP_OP_GLOB:    return " GLOB ";
        case LP_OP_MATCH:   return " MATCH ";
        case LP_OP_REGEXP:  return " REGEXP ";
        case LP_OP_PTR:     return " -> ";
        case LP_OP_PTR2:    return " ->> ";
    }
    return " ?? ";
}

/* ================================================================== */
/*  Conflict clause helper                                             */
/* ================================================================== */

static void sql_conflict(LpBuf *out, int c) {
    switch (c) {
        case LP_CONFLICT_ROLLBACK: lp_buf_puts(out, " ON CONFLICT ROLLBACK"); break;
        case LP_CONFLICT_ABORT:    lp_buf_puts(out, " ON CONFLICT ABORT"); break;
        case LP_CONFLICT_FAIL:     lp_buf_puts(out, " ON CONFLICT FAIL"); break;
        case LP_CONFLICT_IGNORE:   lp_buf_puts(out, " ON CONFLICT IGNORE"); break;
        case LP_CONFLICT_REPLACE:  lp_buf_puts(out, " ON CONFLICT REPLACE"); break;
        default: break;
    }
}

static const char *or_conflict_sql(int c) {
    switch (c) {
        case LP_CONFLICT_ROLLBACK: return "OR ROLLBACK ";
        case LP_CONFLICT_ABORT:    return "OR ABORT ";
        case LP_CONFLICT_FAIL:     return "OR FAIL ";
        case LP_CONFLICT_IGNORE:   return "OR IGNORE ";
        case LP_CONFLICT_REPLACE:  return "OR REPLACE ";
        default: return "";
    }
}

/* ================================================================== */
/*  FK action helper                                                   */
/* ================================================================== */

static void sql_fk_action(LpBuf *out, const char *event, LpFKAction a) {
    switch (a) {
        case LP_FK_SET_NULL:    lp_buf_printf(out, " ON %s SET NULL", event); break;
        case LP_FK_SET_DEFAULT: lp_buf_printf(out, " ON %s SET DEFAULT", event); break;
        case LP_FK_CASCADE:     lp_buf_printf(out, " ON %s CASCADE", event); break;
        case LP_FK_RESTRICT:    lp_buf_printf(out, " ON %s RESTRICT", event); break;
        default: break;
    }
}

/* ================================================================== */
/*  Expression output                                                  */
/* ================================================================== */

static void sql_expr(LpNode *node, LpBuf *out, int parent_prec) {
    if (!node) return;

    switch (node->kind) {
        case LP_EXPR_LITERAL_INT:
        case LP_EXPR_LITERAL_FLOAT:
            lp_buf_puts(out, node->u.literal.value);
            break;

        case LP_EXPR_LITERAL_STRING:
            sql_str_lit(out, node->u.literal.value);
            break;

        case LP_EXPR_LITERAL_BLOB:
            lp_buf_puts(out, node->u.literal.value);
            break;

        case LP_EXPR_LITERAL_NULL:
            lp_buf_puts(out, "NULL");
            break;

        case LP_EXPR_LITERAL_BOOL:
            lp_buf_puts(out, node->u.literal.value);
            break;

        case LP_EXPR_COLUMN_REF:
            if (node->u.column_ref.schema) {
                sql_ident(out, node->u.column_ref.schema);
                lp_buf_putc(out, '.');
            }
            if (node->u.column_ref.table) {
                sql_ident(out, node->u.column_ref.table);
                lp_buf_putc(out, '.');
            }
            sql_ident(out, node->u.column_ref.column);
            break;

        case LP_EXPR_BINARY_OP: {
            int prec = binop_prec(node->u.binary.op);
            int need_parens = (prec < parent_prec);
            if (need_parens) lp_buf_putc(out, '(');
            sql_expr(node->u.binary.left, out, prec);
            lp_buf_puts(out, binop_sql(node->u.binary.op));
            sql_expr(node->u.binary.right, out, prec + 1);
            if (node->u.binary.escape) {
                lp_buf_puts(out, " ESCAPE ");
                sql_expr(node->u.binary.escape, out, 0);
            }
            if (need_parens) lp_buf_putc(out, ')');
            break;
        }

        case LP_EXPR_UNARY_OP:
            switch (node->u.unary.op) {
                case LP_UOP_MINUS:
                    lp_buf_putc(out, '-');
                    /* Space after '-' if operand is also unary minus/plus to avoid '--' comment */
                    if (node->u.unary.operand &&
                        node->u.unary.operand->kind == LP_EXPR_UNARY_OP &&
                        (node->u.unary.operand->u.unary.op == LP_UOP_MINUS ||
                         node->u.unary.operand->u.unary.op == LP_UOP_PLUS))
                        lp_buf_putc(out, ' ');
                    sql_expr(node->u.unary.operand, out, 99);
                    break;
                case LP_UOP_PLUS:   lp_buf_putc(out, '+'); sql_expr(node->u.unary.operand, out, 99); break;
                case LP_UOP_NOT:    lp_buf_puts(out, "NOT "); sql_expr(node->u.unary.operand, out, 0); break;
                case LP_UOP_BITNOT: lp_buf_putc(out, '~'); sql_expr(node->u.unary.operand, out, 99); break;
            }
            break;

        case LP_EXPR_FUNCTION: {
            lp_buf_puts(out, node->u.function.name);
            if (node->u.function.is_ctime_kw) break;
            lp_buf_putc(out, '(');
            if (node->u.function.distinct) lp_buf_puts(out, "DISTINCT ");
            for (int i = 0; i < node->u.function.args.count; i++) {
                if (i > 0) lp_buf_puts(out, ", ");
                sql_expr(node->u.function.args.items[i], out, 0);
            }
            if (node->u.function.order_by.count > 0) {
                lp_buf_puts(out, " ORDER BY ");
                for (int i = 0; i < node->u.function.order_by.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    sql_node(node->u.function.order_by.items[i], out);
                }
            }
            lp_buf_putc(out, ')');
            if (node->u.function.filter) {
                lp_buf_puts(out, " FILTER (WHERE ");
                sql_expr(node->u.function.filter, out, 0);
                lp_buf_putc(out, ')');
            }
            if (node->u.function.over) {
                lp_buf_puts(out, " OVER ");
                sql_node(node->u.function.over, out);
            }
            break;
        }

        case LP_EXPR_CAST:
            lp_buf_puts(out, "CAST(");
            sql_expr(node->u.cast.expr, out, 0);
            lp_buf_puts(out, " AS ");
            lp_buf_puts(out, node->u.cast.type_name);
            lp_buf_putc(out, ')');
            break;

        case LP_EXPR_COLLATE:
            sql_expr(node->u.collate.expr, out, 0);
            lp_buf_puts(out, " COLLATE ");
            sql_ident(out, node->u.collate.collation);
            break;

        case LP_EXPR_BETWEEN:
            sql_expr(node->u.between.expr, out, 0);
            lp_buf_puts(out, node->u.between.is_not ? " NOT BETWEEN " : " BETWEEN ");
            sql_expr(node->u.between.low, out, 0);
            lp_buf_puts(out, " AND ");
            sql_expr(node->u.between.high, out, 0);
            break;

        case LP_EXPR_IN:
            sql_expr(node->u.in.expr, out, 0);
            lp_buf_puts(out, node->u.in.is_not ? " NOT IN " : " IN ");
            if (node->u.in.select) {
                lp_buf_putc(out, '(');
                sql_select_body(node->u.in.select, out);
                lp_buf_putc(out, ')');
            } else {
                lp_buf_putc(out, '(');
                for (int i = 0; i < node->u.in.values.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    sql_expr(node->u.in.values.items[i], out, 0);
                }
                lp_buf_putc(out, ')');
            }
            break;

        case LP_EXPR_EXISTS:
            lp_buf_puts(out, "EXISTS (");
            sql_select_body(node->u.exists.select, out);
            lp_buf_putc(out, ')');
            break;

        case LP_EXPR_SUBQUERY:
            lp_buf_putc(out, '(');
            sql_select_body(node->u.subquery.select, out);
            lp_buf_putc(out, ')');
            break;

        case LP_EXPR_CASE:
            lp_buf_puts(out, "CASE");
            if (node->u.case_.operand) {
                lp_buf_putc(out, ' ');
                sql_expr(node->u.case_.operand, out, 0);
            }
            /* when_exprs: pairs of [when, then, when, then, ...] */
            for (int i = 0; i + 1 < node->u.case_.when_exprs.count; i += 2) {
                lp_buf_puts(out, " WHEN ");
                sql_expr(node->u.case_.when_exprs.items[i], out, 0);
                lp_buf_puts(out, " THEN ");
                sql_expr(node->u.case_.when_exprs.items[i + 1], out, 0);
            }
            if (node->u.case_.else_expr) {
                lp_buf_puts(out, " ELSE ");
                sql_expr(node->u.case_.else_expr, out, 0);
            }
            lp_buf_puts(out, " END");
            break;

        case LP_EXPR_RAISE:
            lp_buf_puts(out, "RAISE(");
            switch (node->u.raise.type) {
                case LP_RAISE_IGNORE:   lp_buf_puts(out, "IGNORE"); break;
                case LP_RAISE_ROLLBACK: lp_buf_puts(out, "ROLLBACK"); break;
                case LP_RAISE_ABORT:    lp_buf_puts(out, "ABORT"); break;
                case LP_RAISE_FAIL:     lp_buf_puts(out, "FAIL"); break;
            }
            if (node->u.raise.message) {
                lp_buf_puts(out, ", ");
                sql_expr(node->u.raise.message, out, 0);
            }
            lp_buf_putc(out, ')');
            break;

        case LP_EXPR_VARIABLE:
            lp_buf_puts(out, node->u.variable.name);
            break;

        case LP_EXPR_STAR:
            if (node->u.star.table) {
                sql_ident(out, node->u.star.table);
                lp_buf_putc(out, '.');
            }
            lp_buf_putc(out, '*');
            break;

        case LP_EXPR_VECTOR:
            lp_buf_putc(out, '(');
            for (int i = 0; i < node->u.vector.values.count; i++) {
                if (i > 0) lp_buf_puts(out, ", ");
                sql_expr(node->u.vector.values.items[i], out, 0);
            }
            lp_buf_putc(out, ')');
            break;

        default:
            /* Non-expression node used in expression context — delegate */
            sql_node(node, out);
            break;
    }
}

/* ================================================================== */
/*  FROM clause output                                                 */
/* ================================================================== */

static void sql_from(LpNode *node, LpBuf *out) {
    if (!node) return;

    switch (node->kind) {
        case LP_FROM_TABLE:
            sql_schema_name(out, node->u.from_table.schema, node->u.from_table.name);
            if (node->u.from_table.func_args.count > 0) {
                lp_buf_putc(out, '(');
                for (int i = 0; i < node->u.from_table.func_args.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    sql_expr(node->u.from_table.func_args.items[i], out, 0);
                }
                lp_buf_putc(out, ')');
            }
            if (node->u.from_table.alias) {
                lp_buf_puts(out, " AS ");
                sql_ident(out, node->u.from_table.alias);
            }
            if (node->u.from_table.indexed_by) {
                lp_buf_puts(out, " INDEXED BY ");
                sql_ident(out, node->u.from_table.indexed_by);
            }
            if (node->u.from_table.not_indexed)
                lp_buf_puts(out, " NOT INDEXED");
            break;

        case LP_FROM_SUBQUERY:
            lp_buf_putc(out, '(');
            if (node->u.from_subquery.select &&
                (node->u.from_subquery.select->kind == LP_JOIN_CLAUSE ||
                 node->u.from_subquery.select->kind == LP_FROM_TABLE ||
                 node->u.from_subquery.select->kind == LP_FROM_SUBQUERY)) {
                /* Parenthesized join/table expression, not a real subquery */
                sql_from(node->u.from_subquery.select, out);
            } else {
                sql_select_body(node->u.from_subquery.select, out);
            }
            lp_buf_putc(out, ')');
            if (node->u.from_subquery.alias) {
                lp_buf_puts(out, " AS ");
                sql_ident(out, node->u.from_subquery.alias);
            }
            break;

        case LP_JOIN_CLAUSE: {
            sql_from(node->u.join.left, out);
            int jt = node->u.join.join_type;
            if (jt & LP_JOIN_NATURAL) lp_buf_puts(out, " NATURAL");
            if (jt & LP_JOIN_LEFT)       lp_buf_puts(out, " LEFT");
            else if (jt & LP_JOIN_RIGHT) lp_buf_puts(out, " RIGHT");
            else if (jt & LP_JOIN_FULL)  lp_buf_puts(out, " FULL");
            if (jt & LP_JOIN_OUTER) lp_buf_puts(out, " OUTER");
            if (jt & LP_JOIN_CROSS)      lp_buf_puts(out, " CROSS");
            else if (jt & LP_JOIN_INNER) {
                /* Only emit INNER if no LEFT/RIGHT/FULL/CROSS */
                if (!(jt & (LP_JOIN_LEFT|LP_JOIN_RIGHT|LP_JOIN_FULL|LP_JOIN_CROSS|LP_JOIN_NATURAL)))
                    lp_buf_puts(out, " INNER");
            }
            lp_buf_puts(out, " JOIN ");
            sql_from(node->u.join.right, out);
            if (node->u.join.on_expr) {
                lp_buf_puts(out, " ON ");
                sql_expr(node->u.join.on_expr, out, 0);
            }
            if (node->u.join.using_columns.count > 0) {
                lp_buf_puts(out, " USING (");
                for (int i = 0; i < node->u.join.using_columns.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    LpNode *col = node->u.join.using_columns.items[i];
                    if (col->kind == LP_EXPR_COLUMN_REF)
                        sql_ident(out, col->u.column_ref.column);
                    else
                        sql_expr(col, out, 0);
                }
                lp_buf_putc(out, ')');
            }
            break;
        }

        default:
            sql_node(node, out);
            break;
    }
}

/* ================================================================== */
/*  Window definition output                                           */
/* ================================================================== */

static void sql_window_body(LpNode *node, LpBuf *out) {
    if (!node || node->kind != LP_WINDOW_DEF) return;

    /* Base window reference */
    if (node->u.window_def.base_name) {
        sql_ident(out, node->u.window_def.base_name);
        if (node->u.window_def.partition_by.count > 0 ||
            node->u.window_def.order_by.count > 0 ||
            node->u.window_def.frame)
            lp_buf_putc(out, ' ');
    }

    if (node->u.window_def.partition_by.count > 0) {
        lp_buf_puts(out, "PARTITION BY ");
        for (int i = 0; i < node->u.window_def.partition_by.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            sql_expr(node->u.window_def.partition_by.items[i], out, 0);
        }
        if (node->u.window_def.order_by.count > 0 || node->u.window_def.frame)
            lp_buf_putc(out, ' ');
    }

    if (node->u.window_def.order_by.count > 0) {
        lp_buf_puts(out, "ORDER BY ");
        for (int i = 0; i < node->u.window_def.order_by.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            sql_node(node->u.window_def.order_by.items[i], out);
        }
        if (node->u.window_def.frame) lp_buf_putc(out, ' ');
    }

    if (node->u.window_def.frame)
        sql_node(node->u.window_def.frame, out);
}

/* ================================================================== */
/*  Frame bound output                                                 */
/* ================================================================== */

static void sql_frame_bound(LpNode *node, LpBuf *out) {
    if (!node || node->kind != LP_FRAME_BOUND) return;
    switch (node->u.frame_bound.type) {
        case LP_BOUND_CURRENT_ROW:
            lp_buf_puts(out, "CURRENT ROW"); break;
        case LP_BOUND_UNBOUNDED_PRECEDING:
            lp_buf_puts(out, "UNBOUNDED PRECEDING"); break;
        case LP_BOUND_UNBOUNDED_FOLLOWING:
            lp_buf_puts(out, "UNBOUNDED FOLLOWING"); break;
        case LP_BOUND_PRECEDING:
            sql_expr(node->u.frame_bound.expr, out, 0);
            lp_buf_puts(out, " PRECEDING"); break;
        case LP_BOUND_FOLLOWING:
            sql_expr(node->u.frame_bound.expr, out, 0);
            lp_buf_puts(out, " FOLLOWING"); break;
    }
}

/* ================================================================== */
/*  SELECT body (shared by SELECT, compound, subquery)                 */
/* ================================================================== */

static void sql_select_body(LpNode *node, LpBuf *out) {
    if (!node) return;

    if (node->kind == LP_COMPOUND_SELECT) {
        sql_select_body(node->u.compound.left, out);
        switch (node->u.compound.op) {
            case LP_COMPOUND_UNION:     lp_buf_puts(out, " UNION "); break;
            case LP_COMPOUND_UNION_ALL: lp_buf_puts(out, " UNION ALL "); break;
            case LP_COMPOUND_INTERSECT: lp_buf_puts(out, " INTERSECT "); break;
            case LP_COMPOUND_EXCEPT:    lp_buf_puts(out, " EXCEPT "); break;
        }
        sql_select_body(node->u.compound.right, out);
        return;
    }

    if (node->kind != LP_STMT_SELECT) {
        sql_node(node, out);
        return;
    }

    /* Check if this is a VALUES-only select (result_columns are VALUES_ROW) */
    if (node->u.select.result_columns.count > 0 &&
        node->u.select.result_columns.items[0]->kind == LP_VALUES_ROW) {
        lp_buf_puts(out, "VALUES ");
        for (int i = 0; i < node->u.select.result_columns.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            LpNode *row = node->u.select.result_columns.items[i];
            lp_buf_putc(out, '(');
            for (int j = 0; j < row->u.values_row.values.count; j++) {
                if (j > 0) lp_buf_puts(out, ", ");
                sql_expr(row->u.values_row.values.items[j], out, 0);
            }
            lp_buf_putc(out, ')');
        }
        return;
    }

    /* WITH clause */
    if (node->u.select.with)
        sql_node(node->u.select.with, out);

    lp_buf_puts(out, "SELECT ");
    if (node->u.select.distinct) lp_buf_puts(out, "DISTINCT ");

    /* Result columns */
    for (int i = 0; i < node->u.select.result_columns.count; i++) {
        if (i > 0) lp_buf_puts(out, ", ");
        LpNode *rc = node->u.select.result_columns.items[i];
        if (rc->kind == LP_RESULT_COLUMN) {
            sql_expr(rc->u.result_column.expr, out, 0);
            if (rc->u.result_column.alias) {
                lp_buf_puts(out, " AS ");
                sql_ident(out, rc->u.result_column.alias);
            }
        } else {
            sql_expr(rc, out, 0);
        }
    }

    /* FROM */
    if (node->u.select.from) {
        lp_buf_puts(out, " FROM ");
        sql_from(node->u.select.from, out);
    }

    /* WHERE */
    if (node->u.select.where) {
        lp_buf_puts(out, " WHERE ");
        sql_expr(node->u.select.where, out, 0);
    }

    /* GROUP BY */
    if (node->u.select.group_by.count > 0) {
        lp_buf_puts(out, " GROUP BY ");
        for (int i = 0; i < node->u.select.group_by.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            sql_expr(node->u.select.group_by.items[i], out, 0);
        }
    }

    /* HAVING */
    if (node->u.select.having) {
        lp_buf_puts(out, " HAVING ");
        sql_expr(node->u.select.having, out, 0);
    }

    /* WINDOW */
    if (node->u.select.window_defs.count > 0) {
        lp_buf_puts(out, " WINDOW ");
        for (int i = 0; i < node->u.select.window_defs.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            LpNode *w = node->u.select.window_defs.items[i];
            if (w->u.window_def.name) {
                sql_ident(out, w->u.window_def.name);
                lp_buf_puts(out, " AS (");
            } else {
                lp_buf_putc(out, '(');
            }
            sql_window_body(w, out);
            lp_buf_putc(out, ')');
        }
    }

    /* ORDER BY */
    if (node->u.select.order_by.count > 0) {
        lp_buf_puts(out, " ORDER BY ");
        for (int i = 0; i < node->u.select.order_by.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            sql_node(node->u.select.order_by.items[i], out);
        }
    }

    /* LIMIT */
    if (node->u.select.limit)
        sql_node(node->u.select.limit, out);
}

/* ================================================================== */
/*  Foreign key output                                                 */
/* ================================================================== */

static void sql_foreign_key(LpNode *node, LpBuf *out) {
    if (!node || node->kind != LP_FOREIGN_KEY) return;
    lp_buf_puts(out, "REFERENCES ");
    sql_ident(out, node->u.foreign_key.table);
    if (node->u.foreign_key.columns.count > 0) {
        lp_buf_putc(out, '(');
        for (int i = 0; i < node->u.foreign_key.columns.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            LpNode *c = node->u.foreign_key.columns.items[i];
            if (c->kind == LP_EXPR_COLUMN_REF)
                sql_ident(out, c->u.column_ref.column);
            else
                sql_expr(c, out, 0);
        }
        lp_buf_putc(out, ')');
    }
    sql_fk_action(out, "DELETE", node->u.foreign_key.on_delete);
    sql_fk_action(out, "UPDATE", node->u.foreign_key.on_update);
}

/* ================================================================== */
/*  Column/table constraint output                                     */
/* ================================================================== */

static void sql_column_constraint(LpNode *node, LpBuf *out) {
    if (!node || node->kind != LP_COLUMN_CONSTRAINT) return;

    if (node->u.column_constraint.name) {
        lp_buf_puts(out, "CONSTRAINT ");
        sql_ident(out, node->u.column_constraint.name);
        lp_buf_putc(out, ' ');
    }

    switch (node->u.column_constraint.type) {
        case LP_CCONS_PRIMARY_KEY:
            lp_buf_puts(out, "PRIMARY KEY");
            if (node->u.column_constraint.sort_order == LP_SORT_ASC)
                lp_buf_puts(out, " ASC");
            else if (node->u.column_constraint.sort_order == LP_SORT_DESC)
                lp_buf_puts(out, " DESC");
            sql_conflict(out, node->u.column_constraint.conflict_action);
            if (node->u.column_constraint.is_autoinc)
                lp_buf_puts(out, " AUTOINCREMENT");
            break;
        case LP_CCONS_NOT_NULL:
            lp_buf_puts(out, "NOT NULL");
            sql_conflict(out, node->u.column_constraint.conflict_action);
            break;
        case LP_CCONS_UNIQUE:
            lp_buf_puts(out, "UNIQUE");
            sql_conflict(out, node->u.column_constraint.conflict_action);
            break;
        case LP_CCONS_CHECK:
            lp_buf_puts(out, "CHECK (");
            sql_expr(node->u.column_constraint.expr, out, 0);
            lp_buf_putc(out, ')');
            break;
        case LP_CCONS_DEFAULT:
            lp_buf_puts(out, "DEFAULT ");
            if (node->u.column_constraint.expr) {
                /* SQL DEFAULT allows bare: signed-number, literal, CTIME keyword.
                ** Everything else needs parentheses. */
                LpNode *e = node->u.column_constraint.expr;
                int bare = (e->kind == LP_EXPR_LITERAL_INT ||
                            e->kind == LP_EXPR_LITERAL_FLOAT ||
                            e->kind == LP_EXPR_LITERAL_STRING ||
                            e->kind == LP_EXPR_LITERAL_BLOB ||
                            e->kind == LP_EXPR_LITERAL_NULL ||
                            e->kind == LP_EXPR_LITERAL_BOOL ||
                            (e->kind == LP_EXPR_FUNCTION && e->u.function.is_ctime_kw) ||
                            (e->kind == LP_EXPR_UNARY_OP &&
                             (e->u.unary.op == LP_UOP_MINUS || e->u.unary.op == LP_UOP_PLUS) &&
                             e->u.unary.operand &&
                             (e->u.unary.operand->kind == LP_EXPR_LITERAL_INT ||
                              e->u.unary.operand->kind == LP_EXPR_LITERAL_FLOAT)));
                if (!bare) lp_buf_putc(out, '(');
                sql_expr(e, out, 0);
                if (!bare) lp_buf_putc(out, ')');
            }
            break;
        case LP_CCONS_REFERENCES:
            sql_foreign_key(node->u.column_constraint.fk, out);
            break;
        case LP_CCONS_COLLATE:
            lp_buf_puts(out, "COLLATE ");
            sql_ident(out, node->u.column_constraint.collation);
            break;
        case LP_CCONS_GENERATED:
            lp_buf_puts(out, "GENERATED ALWAYS AS (");
            sql_expr(node->u.column_constraint.expr, out, 0);
            lp_buf_puts(out, node->u.column_constraint.generated_type ? ") STORED" : ") VIRTUAL");
            break;
        case LP_CCONS_NULL:
            lp_buf_puts(out, "NULL");
            break;
    }
}

static void sql_table_constraint(LpNode *node, LpBuf *out) {
    if (!node || node->kind != LP_TABLE_CONSTRAINT) return;

    if (node->u.table_constraint.name) {
        lp_buf_puts(out, "CONSTRAINT ");
        sql_ident(out, node->u.table_constraint.name);
        lp_buf_putc(out, ' ');
    }

    switch (node->u.table_constraint.type) {
        case LP_TCONS_PRIMARY_KEY:
            lp_buf_puts(out, "PRIMARY KEY (");
            goto columns_list;
        case LP_TCONS_UNIQUE:
            lp_buf_puts(out, "UNIQUE (");
        columns_list:
            for (int i = 0; i < node->u.table_constraint.columns.count; i++) {
                if (i > 0) lp_buf_puts(out, ", ");
                sql_node(node->u.table_constraint.columns.items[i], out);
            }
            lp_buf_putc(out, ')');
            sql_conflict(out, node->u.table_constraint.conflict_action);
            break;
        case LP_TCONS_CHECK:
            lp_buf_puts(out, "CHECK (");
            sql_expr(node->u.table_constraint.expr, out, 0);
            lp_buf_putc(out, ')');
            break;
        case LP_TCONS_FOREIGN_KEY:
            lp_buf_puts(out, "FOREIGN KEY (");
            for (int i = 0; i < node->u.table_constraint.columns.count; i++) {
                if (i > 0) lp_buf_puts(out, ", ");
                sql_node(node->u.table_constraint.columns.items[i], out);
            }
            lp_buf_puts(out, ") ");
            sql_foreign_key(node->u.table_constraint.fk, out);
            break;
    }
}

/* ================================================================== */
/*  Upsert output                                                      */
/* ================================================================== */

static void sql_upsert(LpNode *node, LpBuf *out) {
    if (!node || node->kind != LP_UPSERT) return;

    lp_buf_puts(out, "ON CONFLICT");
    if (node->u.upsert.conflict_target.count > 0) {
        lp_buf_puts(out, " (");
        for (int i = 0; i < node->u.upsert.conflict_target.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            sql_node(node->u.upsert.conflict_target.items[i], out);
        }
        lp_buf_putc(out, ')');
        if (node->u.upsert.conflict_where) {
            lp_buf_puts(out, " WHERE ");
            sql_expr(node->u.upsert.conflict_where, out, 0);
        }
    }

    if (node->u.upsert.set_clauses.count > 0) {
        lp_buf_puts(out, " DO UPDATE SET ");
        for (int i = 0; i < node->u.upsert.set_clauses.count; i++) {
            if (i > 0) lp_buf_puts(out, ", ");
            sql_node(node->u.upsert.set_clauses.items[i], out);
        }
        if (node->u.upsert.where) {
            lp_buf_puts(out, " WHERE ");
            sql_expr(node->u.upsert.where, out, 0);
        }
    } else {
        lp_buf_puts(out, " DO NOTHING");
    }

    if (node->u.upsert.next) {
        lp_buf_putc(out, ' ');
        sql_upsert(node->u.upsert.next, out);
    }
}

/* ================================================================== */
/*  Returning clause                                                   */
/* ================================================================== */

static void sql_returning(LpNodeList *ret, LpBuf *out) {
    if (!ret || ret->count == 0) return;
    lp_buf_puts(out, " RETURNING ");
    for (int i = 0; i < ret->count; i++) {
        if (i > 0) lp_buf_puts(out, ", ");
        LpNode *rc = ret->items[i];
        if (rc->kind == LP_RESULT_COLUMN) {
            sql_expr(rc->u.result_column.expr, out, 0);
            if (rc->u.result_column.alias) {
                lp_buf_puts(out, " AS ");
                sql_ident(out, rc->u.result_column.alias);
            }
        } else {
            sql_expr(rc, out, 0);
        }
    }
}

/* ================================================================== */
/*  Generic node output                                                */
/* ================================================================== */

static void sql_node(LpNode *node, LpBuf *out) {
    if (!node) return;

    switch (node->kind) {
        /* --- SELECT / COMPOUND --- */
        case LP_STMT_SELECT:
        case LP_COMPOUND_SELECT:
            sql_select_body(node, out);
            break;

        /* --- INSERT --- */
        case LP_STMT_INSERT:
            lp_buf_puts(out, "INSERT ");
            lp_buf_puts(out, or_conflict_sql(node->u.insert.or_conflict));
            lp_buf_puts(out, "INTO ");
            sql_schema_name(out, node->u.insert.schema, node->u.insert.table);
            if (node->u.insert.alias) {
                lp_buf_puts(out, " AS ");
                sql_ident(out, node->u.insert.alias);
            }
            if (node->u.insert.columns.count > 0) {
                lp_buf_puts(out, " (");
                for (int i = 0; i < node->u.insert.columns.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    LpNode *c = node->u.insert.columns.items[i];
                    if (c->kind == LP_EXPR_COLUMN_REF)
                        sql_ident(out, c->u.column_ref.column);
                    else
                        sql_expr(c, out, 0);
                }
                lp_buf_putc(out, ')');
            }
            if (node->u.insert.source) {
                lp_buf_putc(out, ' ');
                sql_select_body(node->u.insert.source, out);
            } else {
                lp_buf_puts(out, " DEFAULT VALUES");
            }
            if (node->u.insert.upsert) {
                lp_buf_putc(out, ' ');
                sql_upsert(node->u.insert.upsert, out);
            }
            sql_returning(&node->u.insert.returning, out);
            break;

        /* --- UPDATE --- */
        case LP_STMT_UPDATE:
            lp_buf_puts(out, "UPDATE ");
            lp_buf_puts(out, or_conflict_sql(node->u.update.or_conflict));
            sql_schema_name(out, node->u.update.schema, node->u.update.table);
            if (node->u.update.alias) {
                lp_buf_puts(out, " AS ");
                sql_ident(out, node->u.update.alias);
            }
            lp_buf_puts(out, " SET ");
            for (int i = 0; i < node->u.update.set_clauses.count; i++) {
                if (i > 0) lp_buf_puts(out, ", ");
                sql_node(node->u.update.set_clauses.items[i], out);
            }
            if (node->u.update.from) {
                lp_buf_puts(out, " FROM ");
                sql_from(node->u.update.from, out);
            }
            if (node->u.update.where) {
                lp_buf_puts(out, " WHERE ");
                sql_expr(node->u.update.where, out, 0);
            }
            if (node->u.update.order_by.count > 0) {
                lp_buf_puts(out, " ORDER BY ");
                for (int i = 0; i < node->u.update.order_by.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    sql_node(node->u.update.order_by.items[i], out);
                }
            }
            if (node->u.update.limit) sql_node(node->u.update.limit, out);
            sql_returning(&node->u.update.returning, out);
            break;

        /* --- DELETE --- */
        case LP_STMT_DELETE:
            lp_buf_puts(out, "DELETE FROM ");
            sql_schema_name(out, node->u.del.schema, node->u.del.table);
            if (node->u.del.alias) {
                lp_buf_puts(out, " AS ");
                sql_ident(out, node->u.del.alias);
            }
            if (node->u.del.where) {
                lp_buf_puts(out, " WHERE ");
                sql_expr(node->u.del.where, out, 0);
            }
            if (node->u.del.order_by.count > 0) {
                lp_buf_puts(out, " ORDER BY ");
                for (int i = 0; i < node->u.del.order_by.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    sql_node(node->u.del.order_by.items[i], out);
                }
            }
            if (node->u.del.limit) sql_node(node->u.del.limit, out);
            sql_returning(&node->u.del.returning, out);
            break;

        /* --- CREATE TABLE --- */
        case LP_STMT_CREATE_TABLE:
            lp_buf_puts(out, "CREATE ");
            if (node->u.create_table.temp) lp_buf_puts(out, "TEMP ");
            lp_buf_puts(out, "TABLE ");
            if (node->u.create_table.if_not_exists) lp_buf_puts(out, "IF NOT EXISTS ");
            sql_schema_name(out, node->u.create_table.schema, node->u.create_table.name);
            if (node->u.create_table.as_select) {
                lp_buf_puts(out, " AS ");
                sql_select_body(node->u.create_table.as_select, out);
            } else {
                lp_buf_puts(out, " (");
                int first = 1;
                for (int i = 0; i < node->u.create_table.columns.count; i++) {
                    if (!first) lp_buf_puts(out, ", "); first = 0;
                    sql_node(node->u.create_table.columns.items[i], out);
                }
                for (int i = 0; i < node->u.create_table.constraints.count; i++) {
                    if (!first) lp_buf_puts(out, ", "); first = 0;
                    sql_table_constraint(node->u.create_table.constraints.items[i], out);
                }
                lp_buf_putc(out, ')');
            }
            if ((node->u.create_table.options & LP_TBL_WITHOUT_ROWID) &&
                (node->u.create_table.options & LP_TBL_STRICT)) {
                lp_buf_puts(out, " WITHOUT ROWID, STRICT");
            } else if (node->u.create_table.options & LP_TBL_WITHOUT_ROWID) {
                lp_buf_puts(out, " WITHOUT ROWID");
            } else if (node->u.create_table.options & LP_TBL_STRICT) {
                lp_buf_puts(out, " STRICT");
            }
            break;

        /* --- CREATE INDEX --- */
        case LP_STMT_CREATE_INDEX:
            lp_buf_puts(out, "CREATE ");
            if (node->u.create_index.is_unique) lp_buf_puts(out, "UNIQUE ");
            lp_buf_puts(out, "INDEX ");
            if (node->u.create_index.if_not_exists) lp_buf_puts(out, "IF NOT EXISTS ");
            sql_schema_name(out, node->u.create_index.schema, node->u.create_index.name);
            lp_buf_puts(out, " ON ");
            sql_ident(out, node->u.create_index.table);
            lp_buf_puts(out, " (");
            for (int i = 0; i < node->u.create_index.columns.count; i++) {
                if (i > 0) lp_buf_puts(out, ", ");
                sql_node(node->u.create_index.columns.items[i], out);
            }
            lp_buf_putc(out, ')');
            if (node->u.create_index.where) {
                lp_buf_puts(out, " WHERE ");
                sql_expr(node->u.create_index.where, out, 0);
            }
            break;

        /* --- CREATE VIEW --- */
        case LP_STMT_CREATE_VIEW:
            lp_buf_puts(out, "CREATE ");
            if (node->u.create_view.temp) lp_buf_puts(out, "TEMP ");
            lp_buf_puts(out, "VIEW ");
            if (node->u.create_view.if_not_exists) lp_buf_puts(out, "IF NOT EXISTS ");
            sql_schema_name(out, node->u.create_view.schema, node->u.create_view.name);
            if (node->u.create_view.col_names.count > 0) {
                lp_buf_putc(out, '(');
                for (int i = 0; i < node->u.create_view.col_names.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    LpNode *c = node->u.create_view.col_names.items[i];
                    if (c->kind == LP_EXPR_COLUMN_REF)
                        sql_ident(out, c->u.column_ref.column);
                    else
                        sql_expr(c, out, 0);
                }
                lp_buf_putc(out, ')');
            }
            lp_buf_puts(out, " AS ");
            sql_select_body(node->u.create_view.select, out);
            break;

        /* --- CREATE TRIGGER --- */
        case LP_STMT_CREATE_TRIGGER:
            lp_buf_puts(out, "CREATE ");
            if (node->u.create_trigger.temp) lp_buf_puts(out, "TEMP ");
            lp_buf_puts(out, "TRIGGER ");
            if (node->u.create_trigger.if_not_exists) lp_buf_puts(out, "IF NOT EXISTS ");
            sql_schema_name(out, node->u.create_trigger.schema, node->u.create_trigger.name);
            switch (node->u.create_trigger.time) {
                case LP_TRIGGER_BEFORE:     lp_buf_puts(out, " BEFORE"); break;
                case LP_TRIGGER_AFTER:      lp_buf_puts(out, " AFTER"); break;
                case LP_TRIGGER_INSTEAD_OF: lp_buf_puts(out, " INSTEAD OF"); break;
            }
            switch (node->u.create_trigger.event) {
                case LP_TRIGGER_INSERT: lp_buf_puts(out, " INSERT"); break;
                case LP_TRIGGER_UPDATE:
                    lp_buf_puts(out, " UPDATE");
                    if (node->u.create_trigger.update_columns.count > 0) {
                        lp_buf_puts(out, " OF ");
                        for (int i = 0; i < node->u.create_trigger.update_columns.count; i++) {
                            if (i > 0) lp_buf_puts(out, ", ");
                            LpNode *c = node->u.create_trigger.update_columns.items[i];
                            if (c->kind == LP_EXPR_COLUMN_REF)
                                sql_ident(out, c->u.column_ref.column);
                            else
                                sql_expr(c, out, 0);
                        }
                    }
                    break;
                case LP_TRIGGER_DELETE: lp_buf_puts(out, " DELETE"); break;
            }
            lp_buf_puts(out, " ON ");
            sql_ident(out, node->u.create_trigger.table_name);
            if (node->u.create_trigger.when) {
                lp_buf_puts(out, " WHEN ");
                sql_expr(node->u.create_trigger.when, out, 0);
            }
            lp_buf_puts(out, " BEGIN ");
            for (int i = 0; i < node->u.create_trigger.body.count; i++) {
                LpNode *cmd = node->u.create_trigger.body.items[i];
                if (cmd->kind == LP_TRIGGER_CMD)
                    sql_node(cmd->u.trigger_cmd.stmt, out);
                else
                    sql_node(cmd, out);
                lp_buf_puts(out, "; ");
            }
            lp_buf_puts(out, "END");
            break;

        /* --- CREATE VIRTUAL TABLE --- */
        case LP_STMT_CREATE_VTABLE:
            lp_buf_puts(out, "CREATE VIRTUAL TABLE ");
            if (node->u.create_vtable.if_not_exists) lp_buf_puts(out, "IF NOT EXISTS ");
            sql_schema_name(out, node->u.create_vtable.schema, node->u.create_vtable.name);
            lp_buf_puts(out, " USING ");
            sql_ident(out, node->u.create_vtable.module);
            if (node->u.create_vtable.module_args) {
                lp_buf_putc(out, '(');
                lp_buf_puts(out, node->u.create_vtable.module_args);
                lp_buf_putc(out, ')');
            }
            break;

        /* --- DROP --- */
        case LP_STMT_DROP:
            lp_buf_puts(out, "DROP ");
            switch (node->u.drop.target) {
                case LP_DROP_TABLE:   lp_buf_puts(out, "TABLE "); break;
                case LP_DROP_INDEX:   lp_buf_puts(out, "INDEX "); break;
                case LP_DROP_VIEW:    lp_buf_puts(out, "VIEW "); break;
                case LP_DROP_TRIGGER: lp_buf_puts(out, "TRIGGER "); break;
            }
            if (node->u.drop.if_exists) lp_buf_puts(out, "IF EXISTS ");
            sql_schema_name(out, node->u.drop.schema, node->u.drop.name);
            break;

        /* --- Transaction --- */
        case LP_STMT_BEGIN:
            lp_buf_puts(out, "BEGIN");
            switch (node->u.begin.trans_type) {
                case LP_TRANS_IMMEDIATE: lp_buf_puts(out, " IMMEDIATE"); break;
                case LP_TRANS_EXCLUSIVE: lp_buf_puts(out, " EXCLUSIVE"); break;
                default: break;
            }
            break;
        case LP_STMT_COMMIT:
            lp_buf_puts(out, "COMMIT"); break;
        case LP_STMT_ROLLBACK:
            lp_buf_puts(out, "ROLLBACK"); break;
        case LP_STMT_SAVEPOINT:
            lp_buf_puts(out, "SAVEPOINT ");
            sql_ident(out, node->u.savepoint.name);
            break;
        case LP_STMT_RELEASE:
            lp_buf_puts(out, "RELEASE ");
            sql_ident(out, node->u.savepoint.name);
            break;
        case LP_STMT_ROLLBACK_TO:
            lp_buf_puts(out, "ROLLBACK TO ");
            sql_ident(out, node->u.savepoint.name);
            break;

        /* --- PRAGMA --- */
        case LP_STMT_PRAGMA:
            lp_buf_puts(out, "PRAGMA ");
            sql_schema_name(out, node->u.pragma.schema, node->u.pragma.name);
            if (node->u.pragma.value) {
                lp_buf_puts(out, " = ");
                if (node->u.pragma.is_neg) lp_buf_putc(out, '-');
                lp_buf_puts(out, node->u.pragma.value);
            }
            break;

        /* --- VACUUM --- */
        case LP_STMT_VACUUM:
            lp_buf_puts(out, "VACUUM");
            if (node->u.vacuum.schema) {
                lp_buf_putc(out, ' ');
                sql_ident(out, node->u.vacuum.schema);
            }
            if (node->u.vacuum.into) {
                lp_buf_puts(out, " INTO ");
                sql_expr(node->u.vacuum.into, out, 0);
            }
            break;

        /* --- REINDEX --- */
        case LP_STMT_REINDEX:
            lp_buf_puts(out, "REINDEX");
            if (node->u.reindex.name) {
                lp_buf_putc(out, ' ');
                sql_schema_name(out, node->u.reindex.schema, node->u.reindex.name);
            }
            break;

        /* --- ANALYZE --- */
        case LP_STMT_ANALYZE:
            lp_buf_puts(out, "ANALYZE");
            if (node->u.reindex.name) {
                lp_buf_putc(out, ' ');
                sql_schema_name(out, node->u.reindex.schema, node->u.reindex.name);
            }
            break;

        /* --- ATTACH / DETACH --- */
        case LP_STMT_ATTACH:
            lp_buf_puts(out, "ATTACH ");
            sql_expr(node->u.attach.filename, out, 0);
            lp_buf_puts(out, " AS ");
            sql_expr(node->u.attach.dbname, out, 0);
            break;
        case LP_STMT_DETACH:
            lp_buf_puts(out, "DETACH ");
            sql_expr(node->u.detach.dbname, out, 0);
            break;

        /* --- ALTER TABLE --- */
        case LP_STMT_ALTER:
            lp_buf_puts(out, "ALTER TABLE ");
            sql_schema_name(out, node->u.alter.schema, node->u.alter.table_name);
            switch (node->u.alter.alter_type) {
                case LP_ALTER_RENAME_TABLE:
                    lp_buf_puts(out, " RENAME TO ");
                    sql_ident(out, node->u.alter.new_name);
                    break;
                case LP_ALTER_ADD_COLUMN:
                    lp_buf_puts(out, " ADD COLUMN ");
                    if (node->u.alter.column_def)
                        sql_node(node->u.alter.column_def, out);
                    break;
                case LP_ALTER_DROP_COLUMN:
                    lp_buf_puts(out, " DROP COLUMN ");
                    sql_ident(out, node->u.alter.column_name);
                    break;
                case LP_ALTER_RENAME_COLUMN:
                    lp_buf_puts(out, " RENAME COLUMN ");
                    sql_ident(out, node->u.alter.column_name);
                    lp_buf_puts(out, " TO ");
                    sql_ident(out, node->u.alter.new_name);
                    break;
            }
            break;

        /* --- EXPLAIN --- */
        case LP_STMT_EXPLAIN:
            lp_buf_puts(out, node->u.explain.is_query_plan ? "EXPLAIN QUERY PLAN " : "EXPLAIN ");
            sql_node(node->u.explain.stmt, out);
            break;

        /* --- ORDER TERM --- */
        case LP_ORDER_TERM:
            sql_expr(node->u.order_term.expr, out, 0);
            if (node->u.order_term.direction == LP_SORT_DESC) lp_buf_puts(out, " DESC");
            else if (node->u.order_term.direction == LP_SORT_ASC) lp_buf_puts(out, " ASC");
            if (node->u.order_term.nulls == 1) lp_buf_puts(out, " NULLS FIRST");
            else if (node->u.order_term.nulls == 2) lp_buf_puts(out, " NULLS LAST");
            break;

        /* --- LIMIT --- */
        case LP_LIMIT:
            lp_buf_puts(out, " LIMIT ");
            sql_expr(node->u.limit.count, out, 0);
            if (node->u.limit.offset) {
                lp_buf_puts(out, " OFFSET ");
                sql_expr(node->u.limit.offset, out, 0);
            }
            break;

        /* --- COLUMN DEF --- */
        case LP_COLUMN_DEF:
            sql_ident(out, node->u.column_def.name);
            if (node->u.column_def.type_name) {
                lp_buf_putc(out, ' ');
                lp_buf_puts(out, node->u.column_def.type_name);
            }
            for (int i = 0; i < node->u.column_def.constraints.count; i++) {
                lp_buf_putc(out, ' ');
                sql_column_constraint(node->u.column_def.constraints.items[i], out);
            }
            break;

        /* --- COLUMN CONSTRAINT --- */
        case LP_COLUMN_CONSTRAINT:
            sql_column_constraint(node, out);
            break;

        /* --- TABLE CONSTRAINT --- */
        case LP_TABLE_CONSTRAINT:
            sql_table_constraint(node, out);
            break;

        /* --- FOREIGN KEY --- */
        case LP_FOREIGN_KEY:
            sql_foreign_key(node, out);
            break;

        /* --- CTE --- */
        case LP_CTE:
            sql_ident(out, node->u.cte.name);
            if (node->u.cte.columns.count > 0) {
                lp_buf_putc(out, '(');
                for (int i = 0; i < node->u.cte.columns.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    LpNode *c = node->u.cte.columns.items[i];
                    if (c->kind == LP_EXPR_COLUMN_REF)
                        sql_ident(out, c->u.column_ref.column);
                    else
                        sql_expr(c, out, 0);
                }
                lp_buf_putc(out, ')');
            }
            lp_buf_puts(out, " AS ");
            if (node->u.cte.materialized == LP_MATERIALIZE_YES)
                lp_buf_puts(out, "MATERIALIZED ");
            else if (node->u.cte.materialized == LP_MATERIALIZE_NO)
                lp_buf_puts(out, "NOT MATERIALIZED ");
            lp_buf_putc(out, '(');
            sql_select_body(node->u.cte.select, out);
            lp_buf_putc(out, ')');
            break;

        /* --- WITH --- */
        case LP_WITH:
            lp_buf_puts(out, "WITH ");
            if (node->u.with.recursive) lp_buf_puts(out, "RECURSIVE ");
            for (int i = 0; i < node->u.with.ctes.count; i++) {
                if (i > 0) lp_buf_puts(out, ", ");
                sql_node(node->u.with.ctes.items[i], out);
            }
            lp_buf_putc(out, ' ');
            break;

        /* --- UPSERT --- */
        case LP_UPSERT:
            sql_upsert(node, out);
            break;

        /* --- RETURNING --- */
        case LP_RETURNING:
            sql_returning(&node->u.returning.columns, out);
            break;

        /* --- WINDOW DEF --- */
        case LP_WINDOW_DEF:
            if (node->u.window_def.name &&
                !node->u.window_def.frame &&
                node->u.window_def.partition_by.count == 0 &&
                node->u.window_def.order_by.count == 0 &&
                !node->u.window_def.base_name) {
                /* Bare window reference: OVER w */
                sql_ident(out, node->u.window_def.name);
            } else {
                lp_buf_putc(out, '(');
                sql_window_body(node, out);
                lp_buf_putc(out, ')');
            }
            break;

        /* --- WINDOW FRAME --- */
        case LP_WINDOW_FRAME:
            switch (node->u.window_frame.type) {
                case LP_FRAME_ROWS:   lp_buf_puts(out, "ROWS "); break;
                case LP_FRAME_RANGE:  lp_buf_puts(out, "RANGE "); break;
                case LP_FRAME_GROUPS: lp_buf_puts(out, "GROUPS "); break;
            }
            if (node->u.window_frame.end) {
                lp_buf_puts(out, "BETWEEN ");
                sql_frame_bound(node->u.window_frame.start, out);
                lp_buf_puts(out, " AND ");
                sql_frame_bound(node->u.window_frame.end, out);
            } else {
                sql_frame_bound(node->u.window_frame.start, out);
            }
            switch (node->u.window_frame.exclude) {
                case LP_EXCLUDE_CURRENT_ROW: lp_buf_puts(out, " EXCLUDE CURRENT ROW"); break;
                case LP_EXCLUDE_GROUP:       lp_buf_puts(out, " EXCLUDE GROUP"); break;
                case LP_EXCLUDE_TIES:        lp_buf_puts(out, " EXCLUDE TIES"); break;
                case LP_EXCLUDE_NO_OTHERS:   lp_buf_puts(out, " EXCLUDE NO OTHERS"); break;
                default: break;
            }
            break;

        /* --- FRAME BOUND --- */
        case LP_FRAME_BOUND:
            sql_frame_bound(node, out);
            break;

        /* --- SET CLAUSE --- */
        case LP_SET_CLAUSE:
            if (node->u.set_clause.column) {
                sql_ident(out, node->u.set_clause.column);
            } else if (node->u.set_clause.columns.count > 0) {
                lp_buf_putc(out, '(');
                for (int i = 0; i < node->u.set_clause.columns.count; i++) {
                    if (i > 0) lp_buf_puts(out, ", ");
                    LpNode *c = node->u.set_clause.columns.items[i];
                    if (c->kind == LP_EXPR_COLUMN_REF)
                        sql_ident(out, c->u.column_ref.column);
                    else
                        sql_expr(c, out, 0);
                }
                lp_buf_putc(out, ')');
            }
            lp_buf_puts(out, " = ");
            sql_expr(node->u.set_clause.expr, out, 0);
            break;

        /* --- INDEX COLUMN --- */
        case LP_INDEX_COLUMN:
            sql_expr(node->u.index_column.expr, out, 0);
            if (node->u.index_column.collation) {
                lp_buf_puts(out, " COLLATE ");
                sql_ident(out, node->u.index_column.collation);
            }
            if (node->u.index_column.sort_order == LP_SORT_ASC) lp_buf_puts(out, " ASC");
            else if (node->u.index_column.sort_order == LP_SORT_DESC) lp_buf_puts(out, " DESC");
            break;

        /* --- VALUES ROW --- */
        case LP_VALUES_ROW:
            lp_buf_putc(out, '(');
            for (int i = 0; i < node->u.values_row.values.count; i++) {
                if (i > 0) lp_buf_puts(out, ", ");
                sql_expr(node->u.values_row.values.items[i], out, 0);
            }
            lp_buf_putc(out, ')');
            break;

        /* --- TRIGGER CMD --- */
        case LP_TRIGGER_CMD:
            sql_node(node->u.trigger_cmd.stmt, out);
            break;

        /* --- FROM clause nodes --- */
        case LP_FROM_TABLE:
        case LP_FROM_SUBQUERY:
        case LP_JOIN_CLAUSE:
            sql_from(node, out);
            break;

        /* --- Expressions — delegate --- */
        default:
            sql_expr(node, out, 0);
            break;
    }
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

char *lp_ast_to_sql(LpNode *root, arena_t *arena) {
    if (!root) return NULL;
    LpBuf buf;
    lp_buf_init(&buf);
    sql_node(root, &buf);
    return lp_buf_finish(&buf, arena);
}
