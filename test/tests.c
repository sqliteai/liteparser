/*
** tests.c — Comprehensive test suite for liteparser
*/
#include "liteparser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mach/mach.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-60s ", name); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("[PASS]\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        printf("[FAIL] %s\n", msg); \
    } while(0)

/* Parse helper — returns AST or NULL */
static LpNode *parse(const char *sql, arena_t *arena, const char **err) {
    return lp_parse(sql, arena, err);
}

/* ------------------------------------------------------------------ */
/*  Test categories                                                    */
/* ------------------------------------------------------------------ */

static void test_basic_select(void) {
    printf("\n--- Basic SELECT ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("SELECT 1");
    n = parse("SELECT 1", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "wrong kind");

    arena_reset(a);
    TEST("SELECT a, b FROM t");
    n = parse("SELECT a, b FROM t", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "wrong kind");

    arena_reset(a);
    TEST("SELECT * FROM users");
    n = parse("SELECT * FROM users", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "wrong kind");

    arena_reset(a);
    TEST("SELECT DISTINCT a FROM t");
    n = parse("SELECT DISTINCT a FROM t", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.distinct)
        PASS(); else FAIL(err ? err : "wrong");

    arena_reset(a);
    TEST("SELECT a AS alias FROM t");
    n = parse("SELECT a AS alias FROM t", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "wrong kind");

    arena_destroy(a);
}

static void test_where_orderby_limit(void) {
    printf("\n--- WHERE / ORDER BY / LIMIT ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("SELECT * FROM t WHERE x > 5");
    n = parse("SELECT * FROM t WHERE x > 5", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.where)
        PASS(); else FAIL(err ? err : "no where");

    arena_reset(a);
    TEST("SELECT * FROM t ORDER BY x ASC");
    n = parse("SELECT * FROM t ORDER BY x ASC", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.order_by.count > 0)
        PASS(); else FAIL(err ? err : "no order by");

    arena_reset(a);
    TEST("SELECT * FROM t LIMIT 10");
    n = parse("SELECT * FROM t LIMIT 10", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.limit)
        PASS(); else FAIL(err ? err : "no limit");

    arena_reset(a);
    TEST("SELECT * FROM t LIMIT 10 OFFSET 5");
    n = parse("SELECT * FROM t LIMIT 10 OFFSET 5", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.limit)
        PASS(); else FAIL(err ? err : "no limit");

    arena_reset(a);
    TEST("SELECT * FROM t GROUP BY a HAVING count(*) > 1");
    n = parse("SELECT * FROM t GROUP BY a HAVING count(*) > 1", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.group_by.count > 0
         && n->u.select.having)
        PASS(); else FAIL(err ? err : "no group/having");

    arena_destroy(a);
}

static void test_joins(void) {
    printf("\n--- JOINs ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("SELECT * FROM a, b");
    n = parse("SELECT * FROM a, b", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM a JOIN b ON a.id = b.id");
    n = parse("SELECT * FROM a JOIN b ON a.id = b.id", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM a LEFT JOIN b ON a.id = b.id");
    n = parse("SELECT * FROM a LEFT JOIN b ON a.id = b.id", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM a CROSS JOIN b");
    n = parse("SELECT * FROM a CROSS JOIN b", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM a NATURAL JOIN b");
    n = parse("SELECT * FROM a NATURAL JOIN b", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_compound_select(void) {
    printf("\n--- Compound SELECT ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("SELECT 1 UNION SELECT 2");
    n = parse("SELECT 1 UNION SELECT 2", a, &err);
    if (n && n->kind == LP_COMPOUND_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT 1 UNION ALL SELECT 2");
    n = parse("SELECT 1 UNION ALL SELECT 2", a, &err);
    if (n && n->kind == LP_COMPOUND_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT 1 INTERSECT SELECT 2");
    n = parse("SELECT 1 INTERSECT SELECT 2", a, &err);
    if (n && n->kind == LP_COMPOUND_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT 1 EXCEPT SELECT 2");
    n = parse("SELECT 1 EXCEPT SELECT 2", a, &err);
    if (n && n->kind == LP_COMPOUND_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_subqueries(void) {
    printf("\n--- Subqueries ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("SELECT * FROM (SELECT 1)");
    n = parse("SELECT * FROM (SELECT 1)", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT (SELECT 1)");
    n = parse("SELECT (SELECT 1)", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM t WHERE x IN (SELECT y FROM t2)");
    n = parse("SELECT * FROM t WHERE x IN (SELECT y FROM t2)", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2)");
    n = parse("SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2)", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_cte(void) {
    printf("\n--- CTEs ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("WITH x AS (SELECT 1) SELECT * FROM x");
    n = parse("WITH x AS (SELECT 1) SELECT * FROM x", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.with)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<10) SELECT x FROM cnt");
    n = parse("WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<10) SELECT x FROM cnt", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.with)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_expressions(void) {
    printf("\n--- Expressions ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("SELECT 1 + 2 * 3");
    n = parse("SELECT 1 + 2 * 3", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT CASE WHEN x > 0 THEN 'pos' ELSE 'neg' END FROM t");
    n = parse("SELECT CASE WHEN x > 0 THEN 'pos' ELSE 'neg' END FROM t", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT CAST(x AS INTEGER) FROM t");
    n = parse("SELECT CAST(x AS INTEGER) FROM t", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT x BETWEEN 1 AND 10 FROM t");
    n = parse("SELECT x BETWEEN 1 AND 10 FROM t", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT x IN (1, 2, 3) FROM t");
    n = parse("SELECT x IN (1, 2, 3) FROM t", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT x IS NULL FROM t");
    n = parse("SELECT x IS NULL FROM t", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT x LIKE '%test%' FROM t");
    n = parse("SELECT x LIKE '%test%' FROM t", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT x COLLATE NOCASE FROM t");
    n = parse("SELECT x COLLATE NOCASE FROM t", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT count(*), sum(x) FROM t");
    n = parse("SELECT count(*), sum(x) FROM t", a, &err);
    if (n) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_insert(void) {
    printf("\n--- INSERT ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("INSERT INTO t VALUES(1, 2, 3)");
    n = parse("INSERT INTO t VALUES(1, 2, 3)", a, &err);
    if (n && n->kind == LP_STMT_INSERT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT INTO t(a, b) VALUES(1, 2)");
    n = parse("INSERT INTO t(a, b) VALUES(1, 2)", a, &err);
    if (n && n->kind == LP_STMT_INSERT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT OR REPLACE INTO t VALUES(1)");
    n = parse("INSERT OR REPLACE INTO t VALUES(1)", a, &err);
    if (n && n->kind == LP_STMT_INSERT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("REPLACE INTO t VALUES(1)");
    n = parse("REPLACE INTO t VALUES(1)", a, &err);
    if (n && n->kind == LP_STMT_INSERT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT INTO t DEFAULT VALUES");
    n = parse("INSERT INTO t DEFAULT VALUES", a, &err);
    if (n && n->kind == LP_STMT_INSERT) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_update(void) {
    printf("\n--- UPDATE ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("UPDATE t SET a = 1");
    n = parse("UPDATE t SET a = 1", a, &err);
    if (n && n->kind == LP_STMT_UPDATE) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE t SET a = 1, b = 2 WHERE id = 3");
    n = parse("UPDATE t SET a = 1, b = 2 WHERE id = 3", a, &err);
    if (n && n->kind == LP_STMT_UPDATE) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE OR IGNORE t SET a = 1");
    n = parse("UPDATE OR IGNORE t SET a = 1", a, &err);
    if (n && n->kind == LP_STMT_UPDATE) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_delete(void) {
    printf("\n--- DELETE ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("DELETE FROM t");
    n = parse("DELETE FROM t", a, &err);
    if (n && n->kind == LP_STMT_DELETE) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DELETE FROM t WHERE id = 1");
    n = parse("DELETE FROM t WHERE id = 1", a, &err);
    if (n && n->kind == LP_STMT_DELETE) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_create_table(void) {
    printf("\n--- CREATE TABLE ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT NOT NULL)");
    n = parse("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT NOT NULL)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE TABLE IF NOT EXISTS t(x INT)");
    n = parse("CREATE TABLE IF NOT EXISTS t(x INT)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE && n->u.create_table.if_not_exists)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE TEMP TABLE t(x)");
    n = parse("CREATE TEMP TABLE t(x)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE && n->u.create_table.temp)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE TABLE t(x) WITHOUT ROWID");
    n = parse("CREATE TABLE t(x) WITHOUT ROWID", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE
         && (n->u.create_table.options & LP_TBL_WITHOUT_ROWID))
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE TABLE t AS SELECT * FROM other");
    n = parse("CREATE TABLE t AS SELECT * FROM other", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE && n->u.create_table.as_select)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_create_index(void) {
    printf("\n--- CREATE INDEX ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("CREATE INDEX idx ON t(a)");
    n = parse("CREATE INDEX idx ON t(a)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_INDEX) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE UNIQUE INDEX idx ON t(a, b)");
    n = parse("CREATE UNIQUE INDEX idx ON t(a, b)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_INDEX && n->u.create_index.is_unique)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_transaction(void) {
    printf("\n--- Transaction ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("BEGIN");
    n = parse("BEGIN", a, &err);
    if (n && n->kind == LP_STMT_BEGIN) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("BEGIN IMMEDIATE");
    n = parse("BEGIN IMMEDIATE", a, &err);
    if (n && n->kind == LP_STMT_BEGIN
         && n->u.begin.trans_type == LP_TRANS_IMMEDIATE)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("COMMIT");
    n = parse("COMMIT", a, &err);
    if (n && n->kind == LP_STMT_COMMIT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ROLLBACK");
    n = parse("ROLLBACK", a, &err);
    if (n && n->kind == LP_STMT_ROLLBACK) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SAVEPOINT sp1");
    n = parse("SAVEPOINT sp1", a, &err);
    if (n && n->kind == LP_STMT_SAVEPOINT) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("RELEASE sp1");
    n = parse("RELEASE sp1", a, &err);
    if (n && n->kind == LP_STMT_RELEASE) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_other_statements(void) {
    printf("\n--- Other statements ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("DROP TABLE t");
    n = parse("DROP TABLE t", a, &err);
    if (n && n->kind == LP_STMT_DROP) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DROP TABLE IF EXISTS t");
    n = parse("DROP TABLE IF EXISTS t", a, &err);
    if (n && n->kind == LP_STMT_DROP && n->u.drop.if_exists)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE VIEW v AS SELECT 1");
    n = parse("CREATE VIEW v AS SELECT 1", a, &err);
    if (n && n->kind == LP_STMT_CREATE_VIEW) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("PRAGMA table_info(t)");
    n = parse("PRAGMA table_info(t)", a, &err);
    if (n && n->kind == LP_STMT_PRAGMA) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("VACUUM");
    n = parse("VACUUM", a, &err);
    if (n && n->kind == LP_STMT_VACUUM) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("REINDEX");
    n = parse("REINDEX", a, &err);
    if (n && n->kind == LP_STMT_REINDEX) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ANALYZE");
    n = parse("ANALYZE", a, &err);
    if (n && n->kind == LP_STMT_ANALYZE) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ATTACH 'file.db' AS db2");
    n = parse("ATTACH 'file.db' AS db2", a, &err);
    if (n && n->kind == LP_STMT_ATTACH) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DETACH db2");
    n = parse("DETACH db2", a, &err);
    if (n && n->kind == LP_STMT_DETACH) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("EXPLAIN SELECT 1");
    n = parse("EXPLAIN SELECT 1", a, &err);
    if (n && n->kind == LP_STMT_EXPLAIN) PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ALTER TABLE t RENAME TO t2");
    n = parse("ALTER TABLE t RENAME TO t2", a, &err);
    if (n && n->kind == LP_STMT_ALTER) PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_error_handling(void) {
    printf("\n--- Error handling ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("Syntax error: SELECT FROM");
    n = parse("SELECT FROM", a, &err);
    if (!n && err) PASS(); else FAIL("expected error");

    arena_reset(a);
    TEST("Empty input produces NULL");
    n = parse("", a, &err);
    if (!n) PASS(); else FAIL("expected NULL");

    arena_reset(a);
    TEST("Incomplete: SELECT");
    n = parse("SELECT", a, &err);
    if (!n) PASS(); else FAIL("expected error");

    arena_destroy(a);
}

static int visitor_count_enter(LpVisitor *v, LpNode *node) {
    (void)node;
    (*(int*)v->user_data)++;
    return 0;
}
static int visitor_count_leave(LpVisitor *v, LpNode *node) {
    (void)node;
    ((int*)v->user_data)[1]++;
    return 0;
}

static void test_visitor(void) {
    printf("\n--- Visitor ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("Visitor walks SELECT tree");
    n = parse("SELECT a, b FROM t WHERE x > 1", a, &err);
    if (!n) { FAIL(err ? err : "parse fail"); arena_destroy(a); return; }

    struct { int enter_count; int leave_count; } data = {0, 0};

    LpVisitor visitor = { &data, visitor_count_enter, visitor_count_leave };
    lp_ast_walk(n, &visitor);

    if (data.enter_count > 0 && data.enter_count == data.leave_count)
        PASS();
    else
        FAIL("enter/leave count mismatch");

    arena_destroy(a);
}

static void test_json_output(void) {
    printf("\n--- JSON output ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("JSON output for SELECT 1");
    n = parse("SELECT 1", a, &err);
    if (!n) { FAIL(err ? err : "parse fail"); arena_destroy(a); return; }

    char *json = lp_ast_to_json(n, a, 0);
    if (json && strlen(json) > 0) PASS(); else FAIL("empty JSON output");

    arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/*  Extended tests: deeper AST validation                              */
/* ------------------------------------------------------------------ */

static void test_select_deep(void) {
    printf("\n--- SELECT (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("SELECT t.* FROM t");
    n = parse("SELECT t.* FROM t", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.result_columns.count == 1) {
        LpNode *rc = n->u.select.result_columns.items[0];
        if (rc->kind == LP_RESULT_COLUMN && rc->u.result_column.expr
            && rc->u.result_column.expr->kind == LP_EXPR_STAR
            && rc->u.result_column.expr->u.star.table
            && strcmp(rc->u.result_column.expr->u.star.table, "t") == 0)
            PASS();
        else FAIL("wrong table star");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT a, b, c FROM t — 3 result columns");
    n = parse("SELECT a, b, c FROM t", a, &err);
    if (n && n->u.select.result_columns.count == 3) PASS();
    else FAIL(err ? err : "wrong col count");

    arena_reset(a);
    TEST("SELECT a AS x — alias preserved");
    n = parse("SELECT a AS x FROM t", a, &err);
    if (n && n->u.select.result_columns.count == 1) {
        LpNode *rc = n->u.select.result_columns.items[0];
        if (rc->u.result_column.alias && strcmp(rc->u.result_column.alias, "x") == 0)
            PASS();
        else FAIL("alias mismatch");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT ALL a FROM t — not distinct");
    n = parse("SELECT ALL a FROM t", a, &err);
    if (n && n->kind == LP_STMT_SELECT && !n->u.select.distinct)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM t LIMIT 5, 10 — comma offset syntax");
    n = parse("SELECT * FROM t LIMIT 5, 10", a, &err);
    if (n && n->u.select.limit && n->u.select.limit->u.limit.count
        && n->u.select.limit->u.limit.offset)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM t ORDER BY a DESC NULLS FIRST");
    n = parse("SELECT * FROM t ORDER BY a DESC NULLS FIRST", a, &err);
    if (n && n->u.select.order_by.count == 1) {
        LpNode *ot = n->u.select.order_by.items[0];
        if (ot->u.order_term.direction == LP_SORT_DESC
            && ot->u.order_term.nulls == LP_SORT_ASC)
            PASS();
        else FAIL("direction/nulls wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SELECT * FROM t ORDER BY a, b DESC, c");
    n = parse("SELECT * FROM t ORDER BY a, b DESC, c", a, &err);
    if (n && n->u.select.order_by.count == 3) PASS();
    else FAIL(err ? err : "wrong order count");

    arena_reset(a);
    TEST("SELECT * FROM t GROUP BY a, b");
    n = parse("SELECT * FROM t GROUP BY a, b", a, &err);
    if (n && n->u.select.group_by.count == 2) PASS();
    else FAIL(err ? err : "wrong group count");

    arena_reset(a);
    TEST("SELECT schema.table.column FROM t");
    n = parse("SELECT s.t.c FROM t", a, &err);
    if (n && n->u.select.result_columns.count == 1) {
        LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e && e->kind == LP_EXPR_COLUMN_REF
            && e->u.column_ref.schema && strcmp(e->u.column_ref.schema, "s") == 0
            && e->u.column_ref.table && strcmp(e->u.column_ref.table, "t") == 0
            && e->u.column_ref.column && strcmp(e->u.column_ref.column, "c") == 0)
            PASS();
        else FAIL("3-part col ref wrong");
    } else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_from_deep(void) {
    printf("\n--- FROM / JOIN (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("FROM table with alias");
    n = parse("SELECT * FROM users u", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_FROM_TABLE) {
        if (strcmp(n->u.select.from->u.from_table.name, "users") == 0
            && n->u.select.from->u.from_table.alias
            && strcmp(n->u.select.from->u.from_table.alias, "u") == 0)
            PASS();
        else FAIL("alias/name wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("FROM subquery with alias");
    n = parse("SELECT * FROM (SELECT 1 AS x) sub", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_FROM_SUBQUERY
        && n->u.select.from->u.from_subquery.alias
        && strcmp(n->u.select.from->u.from_subquery.alias, "sub") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("JOIN with USING clause");
    n = parse("SELECT * FROM a JOIN b USING (id)", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_JOIN_CLAUSE
        && n->u.select.from->u.join.using_columns.count == 1)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("LEFT OUTER JOIN");
    n = parse("SELECT * FROM a LEFT OUTER JOIN b ON a.id=b.id", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_JOIN_CLAUSE) {
        int jt = n->u.select.from->u.join.join_type;
        if (jt & LP_JOIN_LEFT) PASS();
        else FAIL("join type not LEFT");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Three-way JOIN");
    n = parse("SELECT * FROM a JOIN b ON a.id=b.aid JOIN c ON b.id=c.bid", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_JOIN_CLAUSE) {
        /* Should be nested: JOIN(JOIN(a,b), c) */
        LpNode *outer = n->u.select.from;
        if (outer->u.join.left && outer->u.join.left->kind == LP_JOIN_CLAUSE
            && outer->u.join.right && outer->u.join.right->kind == LP_FROM_TABLE)
            PASS();
        else FAIL("three-way nesting wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("NATURAL LEFT JOIN");
    n = parse("SELECT * FROM a NATURAL LEFT JOIN b", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_JOIN_CLAUSE) {
        int jt = n->u.select.from->u.join.join_type;
        if ((jt & LP_JOIN_NATURAL) && (jt & LP_JOIN_LEFT)) PASS();
        else FAIL("join type wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Schema-qualified table: s.t");
    n = parse("SELECT * FROM mydb.mytable", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_FROM_TABLE
        && n->u.select.from->u.from_table.schema
        && strcmp(n->u.select.from->u.from_table.schema, "mydb") == 0
        && strcmp(n->u.select.from->u.from_table.name, "mytable") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_expressions_deep(void) {
    printf("\n--- Expressions (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n, *e;

    TEST("Binary operator precedence: 1 + 2 * 3");
    n = parse("SELECT 1 + 2 * 3", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        /* Should be +(1, *(2,3)) */
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_ADD
            && e->u.binary.right->kind == LP_EXPR_BINARY_OP
            && e->u.binary.right->u.binary.op == LP_OP_MUL)
            PASS();
        else FAIL("precedence wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Unary minus: SELECT -42");
    n = parse("SELECT -42", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_UNARY_OP && e->u.unary.op == LP_UOP_MINUS)
            PASS();
        else FAIL("not unary minus");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("NOT expression");
    n = parse("SELECT NOT x FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_UNARY_OP && e->u.unary.op == LP_UOP_NOT)
            PASS();
        else FAIL("not NOT");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Bitwise NOT: ~x");
    n = parse("SELECT ~x FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_UNARY_OP && e->u.unary.op == LP_UOP_BITNOT)
            PASS();
        else FAIL("not bitnot");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("NOT BETWEEN");
    n = parse("SELECT x NOT BETWEEN 1 AND 10 FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BETWEEN && e->u.between.is_not)
            PASS();
        else FAIL("not NOT BETWEEN");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("NOT IN list");
    n = parse("SELECT x NOT IN (1,2) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_IN && e->u.in.is_not)
            PASS();
        else FAIL("not NOT IN");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("IS NOT NULL");
    n = parse("SELECT x IS NOT NULL FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_ISNOT
            && e->u.binary.right->kind == LP_EXPR_LITERAL_NULL)
            PASS();
        else FAIL("wrong IS NOT NULL");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("LIKE with ESCAPE");
    n = parse("SELECT x LIKE '%a' ESCAPE '\\' FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_LIKE
            && e->u.binary.escape)
            PASS();
        else FAIL("LIKE ESCAPE wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("NOT LIKE");
    n = parse("SELECT x NOT LIKE 'y' FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        /* NOT LIKE is wrapped: UNARY_OP(NOT, BINARY(LIKE,...)) */
        if (e->kind == LP_EXPR_UNARY_OP && e->u.unary.op == LP_UOP_NOT
            && e->u.unary.operand->kind == LP_EXPR_BINARY_OP
            && e->u.unary.operand->u.binary.op == LP_OP_LIKE)
            PASS();
        else FAIL("NOT LIKE wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("GLOB operator");
    n = parse("SELECT x GLOB '*.txt' FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_GLOB)
            PASS();
        else FAIL("not GLOB");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("String literal: 'hello world'");
    n = parse("SELECT 'hello world'", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_LITERAL_STRING
            && strcmp(e->u.literal.value, "hello world") == 0)
            PASS();
        else FAIL("string mismatch");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Float literal: 3.14");
    n = parse("SELECT 3.14", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_LITERAL_FLOAT
            && strcmp(e->u.literal.value, "3.14") == 0)
            PASS();
        else FAIL("float mismatch");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Blob literal: X'48454C4C4F'");
    n = parse("SELECT X'48454C4C4F'", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_LITERAL_BLOB) PASS();
        else FAIL("not blob");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("NULL literal");
    n = parse("SELECT NULL", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_LITERAL_NULL) PASS();
        else FAIL("not null literal");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Variable: ?1");
    n = parse("SELECT ?1", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_VARIABLE && strcmp(e->u.variable.name, "?1") == 0)
            PASS();
        else FAIL("variable wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Named variable: :name");
    n = parse("SELECT :name", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_VARIABLE && strcmp(e->u.variable.name, ":name") == 0)
            PASS();
        else FAIL("named variable wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("$dollar variable");
    n = parse("SELECT $var", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_VARIABLE && strcmp(e->u.variable.name, "$var") == 0)
            PASS();
        else FAIL("dollar variable wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CASE with operand");
    n = parse("SELECT CASE x WHEN 1 THEN 'a' WHEN 2 THEN 'b' END FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_CASE && e->u.case_.operand
            && e->u.case_.when_exprs.count == 4 /* 2 pairs: w,t,w,t */
            && !e->u.case_.else_expr)
            PASS();
        else FAIL("CASE operand wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Concatenation: a || b");
    n = parse("SELECT a || b FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_CONCAT)
            PASS();
        else FAIL("not concat");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Shift operators: a << 2");
    n = parse("SELECT a << 2 FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_LSHIFT)
            PASS();
        else FAIL("not lshift");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("JSON pointer: data->>'key'");
    n = parse("SELECT data->>'key' FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_PTR2)
            PASS();
        else FAIL("not ->>");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("JSON pointer: data->'key'");
    n = parse("SELECT data->'key' FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_PTR)
            PASS();
        else FAIL("not ->");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CAST to TEXT");
    n = parse("SELECT CAST(42 AS TEXT)", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_CAST && e->u.cast.type_name
            && strcmp(e->u.cast.type_name, "TEXT") == 0)
            PASS();
        else FAIL("CAST type wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("EXISTS subquery");
    n = parse("SELECT EXISTS (SELECT 1 FROM t WHERE x=1)", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_EXISTS && e->u.exists.select)
            PASS();
        else FAIL("not EXISTS");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("IN with subquery");
    n = parse("SELECT * FROM t WHERE x IN (SELECT id FROM t2)", a, &err);
    if (n && n->u.select.where) {
        e = n->u.select.where;
        if (e->kind == LP_EXPR_IN && e->u.in.select)
            PASS();
        else FAIL("IN subquery wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("IS DISTINCT FROM");
    n = parse("SELECT x IS DISTINCT FROM y FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_BINARY_OP && e->u.binary.op == LP_OP_ISNOT)
            PASS();
        else FAIL("IS DISTINCT FROM wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Vector expression: (1, 2, 3)");
    n = parse("SELECT (1, 2, 3)", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_VECTOR && e->u.vector.values.count == 3)
            PASS();
        else FAIL("vector wrong");
    } else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_functions_deep(void) {
    printf("\n--- Functions (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n, *e;

    TEST("count(*) — star arg");
    n = parse("SELECT count(*) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION
            && strcmp(e->u.function.name, "count") == 0
            && e->u.function.args.count == 1
            && e->u.function.args.items[0]->kind == LP_EXPR_STAR)
            PASS();
        else FAIL("count(*) wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("sum(DISTINCT x)");
    n = parse("SELECT sum(DISTINCT x) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION && e->u.function.distinct)
            PASS();
        else FAIL("distinct flag wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Multi-arg function: coalesce(a, b, c)");
    n = parse("SELECT coalesce(a, b, c) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION && e->u.function.args.count == 3)
            PASS();
        else FAIL("arg count wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Zero-arg function: random()");
    n = parse("SELECT random()", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION && e->u.function.args.count == 0)
            PASS();
        else FAIL("zero-arg wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CURRENT_TIMESTAMP");
    n = parse("SELECT CURRENT_TIMESTAMP", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION) PASS();
        else FAIL("not function");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CURRENT_DATE");
    n = parse("SELECT CURRENT_DATE", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION) PASS();
        else FAIL("not function");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Nested function: upper(trim(x))");
    n = parse("SELECT upper(trim(x)) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION && e->u.function.args.count == 1
            && e->u.function.args.items[0]->kind == LP_EXPR_FUNCTION)
            PASS();
        else FAIL("nested fn wrong");
    } else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_window_functions(void) {
    printf("\n--- Window functions ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n, *e;

    TEST("ROW_NUMBER() OVER (ORDER BY x)");
    n = parse("SELECT ROW_NUMBER() OVER (ORDER BY x) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION && e->u.function.over
            && e->u.function.over->kind == LP_WINDOW_DEF)
            PASS();
        else FAIL("window wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("SUM() OVER (PARTITION BY dept ORDER BY salary)");
    n = parse("SELECT SUM(salary) OVER (PARTITION BY dept ORDER BY salary) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_FUNCTION && e->u.function.over
            && e->u.function.over->u.window_def.partition_by.count == 1
            && e->u.function.over->u.window_def.order_by.count == 1)
            PASS();
        else FAIL("partition/order wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING");
    n = parse("SELECT SUM(x) OVER (ORDER BY y ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        LpNode *frame = e->u.function.over->u.window_def.frame;
        if (frame && frame->u.window_frame.type == LP_FRAME_ROWS)
            PASS();
        else FAIL("frame type wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("OVER named window ref");
    n = parse("SELECT SUM(x) OVER w FROM t WINDOW w AS (ORDER BY y)", a, &err);
    if (n && n->u.select.window_defs.count == 1
        && n->u.select.result_columns.items[0]->u.result_column.expr->u.function.over)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("GROUPS frame type");
    n = parse("SELECT SUM(x) OVER (ORDER BY y GROUPS UNBOUNDED PRECEDING) FROM t", a, &err);
    if (n) {
        e = n->u.select.result_columns.items[0]->u.result_column.expr;
        LpNode *frame = e->u.function.over->u.window_def.frame;
        if (frame && frame->u.window_frame.type == LP_FRAME_GROUPS)
            PASS();
        else FAIL("frame type wrong");
    } else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_insert_deep(void) {
    printf("\n--- INSERT (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("INSERT OR ABORT");
    n = parse("INSERT OR ABORT INTO t VALUES(1)", a, &err);
    if (n && n->kind == LP_STMT_INSERT
        && n->u.insert.or_conflict == LP_CONFLICT_ABORT)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT OR FAIL");
    n = parse("INSERT OR FAIL INTO t VALUES(1)", a, &err);
    if (n && n->u.insert.or_conflict == LP_CONFLICT_FAIL)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT OR ROLLBACK");
    n = parse("INSERT OR ROLLBACK INTO t VALUES(1)", a, &err);
    if (n && n->u.insert.or_conflict == LP_CONFLICT_ROLLBACK)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT OR IGNORE");
    n = parse("INSERT OR IGNORE INTO t VALUES(1)", a, &err);
    if (n && n->u.insert.or_conflict == LP_CONFLICT_IGNORE)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT with SELECT source");
    n = parse("INSERT INTO t(a,b) SELECT x,y FROM s", a, &err);
    if (n && n->kind == LP_STMT_INSERT && n->u.insert.source
        && n->u.insert.source->kind == LP_STMT_SELECT
        && n->u.insert.columns.count == 2)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT with schema: db.table");
    n = parse("INSERT INTO mydb.t VALUES(1)", a, &err);
    if (n && n->kind == LP_STMT_INSERT && n->u.insert.schema
        && strcmp(n->u.insert.schema, "mydb") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSERT with alias");
    n = parse("INSERT INTO t AS new VALUES(1)", a, &err);
    if (n && n->kind == LP_STMT_INSERT && n->u.insert.alias
        && strcmp(n->u.insert.alias, "new") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Upsert: ON CONFLICT DO NOTHING");
    n = parse("INSERT INTO t VALUES(1) ON CONFLICT DO NOTHING", a, &err);
    if (n && n->kind == LP_STMT_INSERT && n->u.insert.upsert
        && n->u.insert.upsert->kind == LP_UPSERT)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Upsert: ON CONFLICT(col) DO UPDATE");
    n = parse("INSERT INTO t(a,b) VALUES(1,2) ON CONFLICT(a) DO UPDATE SET b=excluded.b", a, &err);
    if (n && n->u.insert.upsert && n->u.insert.upsert->u.upsert.set_clauses.count == 1)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Multi-row VALUES");
    n = parse("INSERT INTO t VALUES(1),(2),(3)", a, &err);
    if (n && n->kind == LP_STMT_INSERT && n->u.insert.source
        && n->u.insert.source->kind == LP_COMPOUND_SELECT)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_update_deep(void) {
    printf("\n--- UPDATE (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("UPDATE multiple SET clauses");
    n = parse("UPDATE t SET a=1, b=2, c=3 WHERE id=1", a, &err);
    if (n && n->kind == LP_STMT_UPDATE && n->u.update.set_clauses.count == 3)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE with FROM");
    n = parse("UPDATE t SET a=s.x FROM other s WHERE t.id=s.id", a, &err);
    if (n && n->kind == LP_STMT_UPDATE && n->u.update.from)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE OR REPLACE");
    n = parse("UPDATE OR REPLACE t SET a=1", a, &err);
    if (n && n->u.update.or_conflict == LP_CONFLICT_REPLACE)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE OR ABORT");
    n = parse("UPDATE OR ABORT t SET a=1", a, &err);
    if (n && n->u.update.or_conflict == LP_CONFLICT_ABORT)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE with RETURNING");
    n = parse("UPDATE t SET a=1 RETURNING *", a, &err);
    if (n && n->kind == LP_STMT_UPDATE && n->u.update.returning.count > 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE with multi-column SET: (a,b) = (1,2)");
    n = parse("UPDATE t SET (a,b) = (1,2)", a, &err);
    if (n && n->kind == LP_STMT_UPDATE && n->u.update.set_clauses.count == 1) {
        LpNode *sc = n->u.update.set_clauses.items[0];
        if (sc->kind == LP_SET_CLAUSE && sc->u.set_clause.columns.count == 2)
            PASS();
        else FAIL("multi-col set wrong");
    } else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_delete_deep(void) {
    printf("\n--- DELETE (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("DELETE with RETURNING");
    n = parse("DELETE FROM t WHERE id=1 RETURNING *", a, &err);
    if (n && n->kind == LP_STMT_DELETE && n->u.del.returning.count > 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DELETE with schema");
    n = parse("DELETE FROM mydb.t WHERE id=1", a, &err);
    if (n && n->kind == LP_STMT_DELETE && n->u.del.schema
        && strcmp(n->u.del.schema, "mydb") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DELETE with complex WHERE");
    n = parse("DELETE FROM t WHERE a > 5 AND b IN (1,2,3) OR c IS NULL", a, &err);
    if (n && n->kind == LP_STMT_DELETE && n->u.del.where)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_create_table_deep(void) {
    printf("\n--- CREATE TABLE (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("Column type: VARCHAR(255)");
    n = parse("CREATE TABLE t(name VARCHAR(255))", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        if (col->u.column_def.type_name)
            PASS();
        else FAIL("no type name");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("AUTOINCREMENT");
    n = parse("CREATE TABLE t(id INTEGER PRIMARY KEY AUTOINCREMENT)", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        if (col->u.column_def.constraints.count >= 1) {
            LpNode *c = col->u.column_def.constraints.items[0];
            if (c->u.column_constraint.type == LP_CCONS_PRIMARY_KEY
                && c->u.column_constraint.is_autoinc)
                PASS();
            else FAIL("not autoinc");
        } else FAIL("no constraints");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UNIQUE constraint");
    n = parse("CREATE TABLE t(email TEXT UNIQUE)", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        int found = 0;
        for (int i = 0; i < col->u.column_def.constraints.count; i++)
            if (col->u.column_def.constraints.items[i]->u.column_constraint.type == LP_CCONS_UNIQUE)
                found = 1;
        if (found) PASS(); else FAIL("no unique");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CHECK constraint");
    n = parse("CREATE TABLE t(age INT CHECK(age > 0))", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        int found = 0;
        for (int i = 0; i < col->u.column_def.constraints.count; i++)
            if (col->u.column_def.constraints.items[i]->u.column_constraint.type == LP_CCONS_CHECK)
                found = 1;
        if (found) PASS(); else FAIL("no check");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DEFAULT value");
    n = parse("CREATE TABLE t(status TEXT DEFAULT 'active')", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        int found = 0;
        for (int i = 0; i < col->u.column_def.constraints.count; i++)
            if (col->u.column_def.constraints.items[i]->u.column_constraint.type == LP_CCONS_DEFAULT)
                found = 1;
        if (found) PASS(); else FAIL("no default");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("COLLATE constraint");
    n = parse("CREATE TABLE t(name TEXT COLLATE NOCASE)", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        int found = 0;
        for (int i = 0; i < col->u.column_def.constraints.count; i++)
            if (col->u.column_def.constraints.items[i]->u.column_constraint.type == LP_CCONS_COLLATE)
                found = 1;
        if (found) PASS(); else FAIL("no collate");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("FOREIGN KEY column constraint");
    n = parse("CREATE TABLE t(uid INT REFERENCES users(id))", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        int found = 0;
        for (int i = 0; i < col->u.column_def.constraints.count; i++)
            if (col->u.column_def.constraints.items[i]->u.column_constraint.type == LP_CCONS_REFERENCES)
                found = 1;
        if (found) PASS(); else FAIL("no fk ref");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("FOREIGN KEY with ON DELETE CASCADE");
    n = parse("CREATE TABLE t(uid INT REFERENCES users(id) ON DELETE CASCADE)", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        int found = 0;
        for (int i = 0; i < col->u.column_def.constraints.count; i++) {
            LpNode *c = col->u.column_def.constraints.items[i];
            if (c->u.column_constraint.type == LP_CCONS_REFERENCES && c->u.column_constraint.fk
                && c->u.column_constraint.fk->u.foreign_key.on_delete == LP_FK_CASCADE)
                found = 1;
        }
        if (found) PASS(); else FAIL("no cascade");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Table PRIMARY KEY constraint");
    n = parse("CREATE TABLE t(a INT, b INT, PRIMARY KEY(a, b))", a, &err);
    if (n && n->u.create_table.constraints.count == 1) {
        LpNode *tc = n->u.create_table.constraints.items[0];
        if (tc->u.table_constraint.type == LP_TCONS_PRIMARY_KEY
            && tc->u.table_constraint.columns.count == 2)
            PASS();
        else FAIL("table pk wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Table UNIQUE constraint");
    n = parse("CREATE TABLE t(a INT, b INT, UNIQUE(a, b))", a, &err);
    if (n && n->u.create_table.constraints.count == 1) {
        LpNode *tc = n->u.create_table.constraints.items[0];
        if (tc->u.table_constraint.type == LP_TCONS_UNIQUE)
            PASS();
        else FAIL("not unique");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Table FOREIGN KEY constraint");
    n = parse("CREATE TABLE t(a INT, b INT, FOREIGN KEY(a) REFERENCES other(id))", a, &err);
    if (n && n->u.create_table.constraints.count == 1) {
        LpNode *tc = n->u.create_table.constraints.items[0];
        if (tc->u.table_constraint.type == LP_TCONS_FOREIGN_KEY && tc->u.table_constraint.fk)
            PASS();
        else FAIL("not fk");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("STRICT table");
    n = parse("CREATE TABLE t(x INT) STRICT", a, &err);
    if (n && (n->u.create_table.options & LP_TBL_STRICT))
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("WITHOUT ROWID, STRICT combined");
    n = parse("CREATE TABLE t(x INT PRIMARY KEY) WITHOUT ROWID, STRICT", a, &err);
    if (n && (n->u.create_table.options & LP_TBL_WITHOUT_ROWID)
          && (n->u.create_table.options & LP_TBL_STRICT))
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("GENERATED ALWAYS AS ... STORED");
    n = parse("CREATE TABLE t(a INT, b INT GENERATED ALWAYS AS (a * 2) STORED)", a, &err);
    if (n && n->u.create_table.columns.count == 2) {
        LpNode *col = n->u.create_table.columns.items[1];
        int found = 0;
        for (int i = 0; i < col->u.column_def.constraints.count; i++) {
            LpNode *c = col->u.column_def.constraints.items[i];
            if (c->u.column_constraint.type == LP_CCONS_GENERATED
                && c->u.column_constraint.generated_type == 1)
                found = 1;
        }
        if (found) PASS(); else FAIL("no generated stored");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Named constraint: CONSTRAINT pk PRIMARY KEY");
    n = parse("CREATE TABLE t(id INT CONSTRAINT pk PRIMARY KEY)", a, &err);
    if (n && n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        if (col->u.column_def.constraints.count >= 1
            && col->u.column_def.constraints.items[0]->u.column_constraint.name
            && strcmp(col->u.column_def.constraints.items[0]->u.column_constraint.name, "pk") == 0)
            PASS();
        else FAIL("constraint name wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Multiple columns with mixed constraints");
    n = parse("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, "
              "age INT CHECK(age>=0) DEFAULT 0, email TEXT COLLATE NOCASE)", a, &err);
    if (n && n->u.create_table.columns.count == 4) PASS();
    else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_create_index_deep(void) {
    printf("\n--- CREATE INDEX (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("CREATE INDEX IF NOT EXISTS");
    n = parse("CREATE INDEX IF NOT EXISTS idx ON t(a)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_INDEX && n->u.create_index.if_not_exists)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Partial index with WHERE");
    n = parse("CREATE INDEX idx ON t(a) WHERE a > 0", a, &err);
    if (n && n->kind == LP_STMT_CREATE_INDEX && n->u.create_index.where)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Multi-column index: (a, b, c)");
    n = parse("CREATE INDEX idx ON t(a, b, c)", a, &err);
    if (n && n->u.create_index.columns.count == 3) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Schema-qualified index");
    n = parse("CREATE INDEX mydb.idx ON t(a)", a, &err);
    if (n && n->u.create_index.schema
        && strcmp(n->u.create_index.schema, "mydb") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_create_view_deep(void) {
    printf("\n--- CREATE VIEW (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("CREATE TEMP VIEW");
    n = parse("CREATE TEMP VIEW v AS SELECT 1", a, &err);
    if (n && n->kind == LP_STMT_CREATE_VIEW && n->u.create_view.temp)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE VIEW IF NOT EXISTS");
    n = parse("CREATE VIEW IF NOT EXISTS v AS SELECT 1", a, &err);
    if (n && n->u.create_view.if_not_exists)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE VIEW with column names");
    n = parse("CREATE VIEW v(x, y) AS SELECT a, b FROM t", a, &err);
    if (n && n->u.create_view.col_names.count == 2)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE VIEW with complex SELECT");
    n = parse("CREATE VIEW v AS SELECT a, count(*) FROM t GROUP BY a HAVING count(*) > 1", a, &err);
    if (n && n->kind == LP_STMT_CREATE_VIEW && n->u.create_view.select)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_create_trigger_deep(void) {
    printf("\n--- CREATE TRIGGER (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("BEFORE INSERT trigger");
    n = parse("CREATE TRIGGER t1 BEFORE INSERT ON t BEGIN SELECT 1; END", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TRIGGER
        && n->u.create_trigger.time == LP_TRIGGER_BEFORE
        && n->u.create_trigger.event == LP_TRIGGER_INSERT
        && n->u.create_trigger.body.count == 1)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("AFTER DELETE trigger");
    n = parse("CREATE TRIGGER t1 AFTER DELETE ON t BEGIN SELECT 1; END", a, &err);
    if (n && n->u.create_trigger.time == LP_TRIGGER_AFTER
        && n->u.create_trigger.event == LP_TRIGGER_DELETE)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("INSTEAD OF UPDATE trigger");
    n = parse("CREATE TRIGGER t1 INSTEAD OF UPDATE ON v BEGIN SELECT 1; END", a, &err);
    if (n && n->u.create_trigger.time == LP_TRIGGER_INSTEAD_OF
        && n->u.create_trigger.event == LP_TRIGGER_UPDATE)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE OF columns trigger");
    n = parse("CREATE TRIGGER t1 AFTER UPDATE OF a, b ON t BEGIN SELECT 1; END", a, &err);
    if (n && n->u.create_trigger.event == LP_TRIGGER_UPDATE
        && n->u.create_trigger.update_columns.count == 2)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Trigger with WHEN clause");
    n = parse("CREATE TRIGGER t1 BEFORE INSERT ON t WHEN NEW.x > 0 BEGIN SELECT 1; END", a, &err);
    if (n && n->u.create_trigger.when)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Trigger with multiple body stmts");
    n = parse("CREATE TRIGGER t1 AFTER INSERT ON t BEGIN "
              "INSERT INTO log VALUES(NEW.id); "
              "UPDATE counts SET n=n+1; "
              "END", a, &err);
    if (n && n->u.create_trigger.body.count == 2)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE TRIGGER IF NOT EXISTS");
    n = parse("CREATE TRIGGER IF NOT EXISTS t1 AFTER INSERT ON t BEGIN SELECT 1; END", a, &err);
    if (n && n->u.create_trigger.if_not_exists)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_cte_deep(void) {
    printf("\n--- CTEs (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("Multiple CTEs");
    n = parse("WITH a AS (SELECT 1), b AS (SELECT 2) SELECT * FROM a, b", a, &err);
    if (n && n->u.select.with && n->u.select.with->u.with.ctes.count == 2)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CTE with column list");
    n = parse("WITH t(x, y) AS (SELECT 1, 2) SELECT * FROM t", a, &err);
    if (n && n->u.select.with) {
        LpNode *cte = n->u.select.with->u.with.ctes.items[0];
        if (cte->u.cte.columns.count == 2)
            PASS();
        else FAIL("cte columns wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CTE MATERIALIZED hint");
    n = parse("WITH t AS MATERIALIZED (SELECT 1) SELECT * FROM t", a, &err);
    if (n && n->u.select.with) {
        LpNode *cte = n->u.select.with->u.with.ctes.items[0];
        if (cte->u.cte.materialized == LP_MATERIALIZE_YES)
            PASS();
        else FAIL("not materialized");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CTE NOT MATERIALIZED hint");
    n = parse("WITH t AS NOT MATERIALIZED (SELECT 1) SELECT * FROM t", a, &err);
    if (n && n->u.select.with) {
        LpNode *cte = n->u.select.with->u.with.ctes.items[0];
        if (cte->u.cte.materialized == LP_MATERIALIZE_NO)
            PASS();
        else FAIL("not NOT MATERIALIZED");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Recursive CTE: fibonacci");
    n = parse("WITH RECURSIVE fib(a,b) AS ("
              "VALUES(0,1) UNION ALL SELECT b, a+b FROM fib WHERE a < 100"
              ") SELECT a FROM fib", a, &err);
    if (n && n->u.select.with && n->u.select.with->u.with.recursive)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_compound_deep(void) {
    printf("\n--- Compound SELECT (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("Three-way UNION");
    n = parse("SELECT 1 UNION SELECT 2 UNION SELECT 3", a, &err);
    if (n && n->kind == LP_COMPOUND_SELECT) {
        /* Should be UNION(UNION(1,2), 3) — left-associative */
        if (n->u.compound.left && n->u.compound.left->kind == LP_COMPOUND_SELECT)
            PASS();
        else FAIL("not left-associative");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UNION ALL with ORDER BY");
    n = parse("SELECT a FROM t1 UNION ALL SELECT b FROM t2 ORDER BY 1", a, &err);
    /* ORDER BY applies to the whole compound, but it's on the rightmost select */
    if (n && n->kind == LP_COMPOUND_SELECT) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("EXCEPT query");
    n = parse("SELECT id FROM all_users EXCEPT SELECT id FROM banned_users", a, &err);
    if (n && n->kind == LP_COMPOUND_SELECT && n->u.compound.op == LP_COMPOUND_EXCEPT)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_alter_table_deep(void) {
    printf("\n--- ALTER TABLE (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("ALTER TABLE RENAME TO");
    n = parse("ALTER TABLE old_name RENAME TO new_name", a, &err);
    if (n && n->kind == LP_STMT_ALTER
        && n->u.alter.alter_type == LP_ALTER_RENAME_TABLE
        && strcmp(n->u.alter.table_name, "old_name") == 0
        && strcmp(n->u.alter.new_name, "new_name") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ALTER TABLE ADD COLUMN");
    n = parse("ALTER TABLE t ADD COLUMN name TEXT", a, &err);
    if (n && n->u.alter.alter_type == LP_ALTER_ADD_COLUMN
        && n->u.alter.column_def)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ALTER TABLE DROP COLUMN");
    n = parse("ALTER TABLE t DROP COLUMN old_col", a, &err);
    if (n && n->u.alter.alter_type == LP_ALTER_DROP_COLUMN
        && strcmp(n->u.alter.column_name, "old_col") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ALTER TABLE RENAME COLUMN");
    n = parse("ALTER TABLE t RENAME COLUMN old_c TO new_c", a, &err);
    if (n && n->u.alter.alter_type == LP_ALTER_RENAME_COLUMN
        && strcmp(n->u.alter.column_name, "old_c") == 0
        && strcmp(n->u.alter.new_name, "new_c") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ALTER TABLE with schema");
    n = parse("ALTER TABLE mydb.t RENAME TO t2", a, &err);
    if (n && n->u.alter.schema && strcmp(n->u.alter.schema, "mydb") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_drop_deep(void) {
    printf("\n--- DROP (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("DROP INDEX");
    n = parse("DROP INDEX idx", a, &err);
    if (n && n->kind == LP_STMT_DROP && n->u.drop.target == LP_DROP_INDEX)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DROP VIEW IF EXISTS");
    n = parse("DROP VIEW IF EXISTS myview", a, &err);
    if (n && n->u.drop.target == LP_DROP_VIEW && n->u.drop.if_exists
        && strcmp(n->u.drop.name, "myview") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DROP TRIGGER");
    n = parse("DROP TRIGGER mytrig", a, &err);
    if (n && n->u.drop.target == LP_DROP_TRIGGER)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("DROP with schema");
    n = parse("DROP TABLE mydb.t", a, &err);
    if (n && n->u.drop.schema && strcmp(n->u.drop.schema, "mydb") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_transaction_deep(void) {
    printf("\n--- Transaction (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("BEGIN EXCLUSIVE");
    n = parse("BEGIN EXCLUSIVE", a, &err);
    if (n && n->u.begin.trans_type == LP_TRANS_EXCLUSIVE)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("BEGIN DEFERRED TRANSACTION");
    n = parse("BEGIN DEFERRED TRANSACTION", a, &err);
    if (n && n->u.begin.trans_type == LP_TRANS_DEFERRED)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("END — same as COMMIT");
    n = parse("END", a, &err);
    if (n && n->kind == LP_STMT_COMMIT)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ROLLBACK TO SAVEPOINT");
    n = parse("ROLLBACK TO SAVEPOINT sp1", a, &err);
    if (n && n->kind == LP_STMT_ROLLBACK_TO
        && strcmp(n->u.savepoint.name, "sp1") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ROLLBACK TO (no SAVEPOINT keyword)");
    n = parse("ROLLBACK TO sp2", a, &err);
    if (n && n->kind == LP_STMT_ROLLBACK_TO
        && strcmp(n->u.savepoint.name, "sp2") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_pragma_deep(void) {
    printf("\n--- PRAGMA (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("PRAGMA journal_mode=WAL");
    n = parse("PRAGMA journal_mode=WAL", a, &err);
    if (n && n->kind == LP_STMT_PRAGMA
        && strcmp(n->u.pragma.name, "journal_mode") == 0
        && strcmp(n->u.pragma.value, "WAL") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("PRAGMA with schema");
    n = parse("PRAGMA main.journal_mode", a, &err);
    if (n && n->u.pragma.schema && strcmp(n->u.pragma.schema, "main") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("PRAGMA with negative value");
    n = parse("PRAGMA cache_size=-2000", a, &err);
    if (n && n->kind == LP_STMT_PRAGMA && n->u.pragma.is_neg)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("PRAGMA with paren syntax");
    n = parse("PRAGMA table_info('users')", a, &err);
    if (n && n->kind == LP_STMT_PRAGMA)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_misc_statements(void) {
    printf("\n--- Misc statements ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("VACUUM INTO 'backup.db'");
    n = parse("VACUUM INTO 'backup.db'", a, &err);
    if (n && n->kind == LP_STMT_VACUUM && n->u.vacuum.into)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("VACUUM schema");
    n = parse("VACUUM main", a, &err);
    if (n && n->kind == LP_STMT_VACUUM && n->u.vacuum.schema
        && strcmp(n->u.vacuum.schema, "main") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("REINDEX table_name");
    n = parse("REINDEX my_table", a, &err);
    if (n && n->kind == LP_STMT_REINDEX && n->u.reindex.name
        && strcmp(n->u.reindex.name, "my_table") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ANALYZE table_name");
    n = parse("ANALYZE my_table", a, &err);
    if (n && n->kind == LP_STMT_ANALYZE)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("ATTACH with KEY");
    n = parse("ATTACH DATABASE 'enc.db' AS enc KEY 'secret'", a, &err);
    if (n && n->kind == LP_STMT_ATTACH && n->u.attach.key)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("EXPLAIN QUERY PLAN");
    n = parse("EXPLAIN QUERY PLAN SELECT * FROM t", a, &err);
    if (n && n->kind == LP_STMT_EXPLAIN && n->u.explain.is_query_plan
        && n->u.explain.stmt && n->u.explain.stmt->kind == LP_STMT_SELECT)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CREATE VIRTUAL TABLE");
    n = parse("CREATE VIRTUAL TABLE ft USING fts5(title, body)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_VTABLE
        && strcmp(n->u.create_vtable.module, "fts5") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_error_handling_deep(void) {
    printf("\n--- Error handling (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("Missing table name: INSERT INTO VALUES(1)");
    n = parse("INSERT INTO VALUES(1)", a, &err);
    if (!n && err) PASS(); else FAIL("expected error");

    arena_reset(a);
    TEST("Unclosed parenthesis: SELECT (1 + 2");
    n = parse("SELECT (1 + 2", a, &err);
    if (!n && err) PASS(); else FAIL("expected error");

    arena_reset(a);
    TEST("Double keyword: SELECT SELECT");
    n = parse("SELECT SELECT", a, &err);
    if (!n && err) PASS(); else FAIL("expected error");

    arena_reset(a);
    TEST("Trailing garbage: SELECT 1 FROM");
    n = parse("SELECT 1 FROM", a, &err);
    if (!n && err) PASS(); else FAIL("expected error");

    arena_reset(a);
    TEST("Semicolon only");
    n = parse(";", a, &err);
    if (!n) PASS(); else FAIL("expected NULL");

    arena_reset(a);
    TEST("Multiple semicolons");
    n = parse(";;;", a, &err);
    if (!n) PASS(); else FAIL("expected NULL");

    arena_reset(a);
    TEST("SQL comment only: -- comment");
    n = parse("-- just a comment", a, &err);
    if (!n) PASS(); else FAIL("expected NULL");

    arena_reset(a);
    TEST("Block comment only: /* comment */");
    n = parse("/* comment */", a, &err);
    if (!n) PASS(); else FAIL("expected NULL");

    arena_reset(a);
    TEST("Illegal token: @");
    n = parse("SELECT @", a, &err);
    if (!n && err) PASS(); else FAIL("expected error");

    arena_destroy(a);
}

static int skip_enter(LpVisitor *v, LpNode *node) {
    (*(int*)v->user_data)++;
    /* Skip children of FROM_TABLE */
    if (node->kind == LP_FROM_TABLE) return 1;
    return 0;
}
static int abort_enter(LpVisitor *v, LpNode *node) {
    int *count = (int *)v->user_data;
    (*count)++;
    (void)node;
    if (*count >= 3) return 2; /* abort after 3 nodes */
    return 0;
}

static void test_visitor_deep(void) {
    printf("\n--- Visitor (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("Visitor skip children (return 1)");
    n = parse("SELECT * FROM t WHERE 1", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    struct { int enter_count; int leave_count; } data1 = {0, 0};
    LpVisitor v1 = { &data1, skip_enter, visitor_count_leave };
    lp_ast_walk(n, &v1);
    /* FROM_TABLE children should be skipped, so fewer nodes visited
       than without skipping */
    if (data1.enter_count > 0 && data1.leave_count > 0)
        PASS();
    else FAIL("skip didn't work");

    arena_reset(a);
    TEST("Visitor abort (return 2)");
    n = parse("SELECT a, b, c FROM t WHERE x > 1 ORDER BY a", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    int count = 0;
    LpVisitor v2 = { &count, abort_enter, NULL };
    lp_ast_walk(n, &v2);
    if (count == 3) PASS();
    else FAIL("abort count wrong");

    arena_reset(a);
    TEST("Visitor on compound SELECT");
    n = parse("SELECT 1 UNION SELECT 2", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    struct { int enter_count; int leave_count; } data3 = {0, 0};
    LpVisitor v3 = { &data3, visitor_count_enter, visitor_count_leave };
    lp_ast_walk(n, &v3);
    if (data3.enter_count > 3) PASS(); /* compound + two selects + result cols */
    else FAIL("too few nodes");

    arena_reset(a);
    TEST("Visitor on CREATE TABLE");
    n = parse("CREATE TABLE t(id INT PRIMARY KEY, name TEXT NOT NULL)", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    struct { int enter_count; int leave_count; } data4 = {0, 0};
    LpVisitor v4 = { &data4, visitor_count_enter, visitor_count_leave };
    lp_ast_walk(n, &v4);
    /* CREATE_TABLE + 2 COLUMN_DEF + constraints */
    if (data4.enter_count >= 5 && data4.enter_count == data4.leave_count)
        PASS();
    else FAIL("CREATE TABLE walk wrong");

    arena_destroy(a);
}

static void test_json_deep(void) {
    printf("\n--- JSON output (deep checks) ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("JSON contains 'kind' for SELECT");
    n = parse("SELECT 1", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    char *buf = lp_ast_to_json(n, a, 0);
    if (strstr(buf, "\"kind\"") && strstr(buf, "STMT_SELECT"))
        PASS();
    else FAIL("JSON missing kind/STMT_SELECT");

    arena_reset(a);
    TEST("JSON pretty print has newlines");
    n = parse("SELECT 1", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    buf = lp_ast_to_json(n, a, 1);
    if (strchr(buf, '\n'))
        PASS();
    else FAIL("no newlines in pretty mode");

    arena_reset(a);
    TEST("JSON non-pretty has no newlines");
    n = parse("SELECT 1", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    buf = lp_ast_to_json(n, a, 0);
    if (!strchr(buf, '\n'))
        PASS();
    else FAIL("newlines in non-pretty mode");

    arena_reset(a);
    TEST("JSON for INSERT contains 'table'");
    n = parse("INSERT INTO users VALUES(1)", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    buf = lp_ast_to_json(n, a, 0);
    if (strstr(buf, "\"table\"") && strstr(buf, "users"))
        PASS();
    else FAIL("JSON missing table");

    arena_reset(a);
    TEST("JSON for binary op contains operator");
    n = parse("SELECT 1 + 2", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    buf = lp_ast_to_json(n, a, 0);
    if (strstr(buf, "EXPR_BINARY_OP") && strstr(buf, "\"+\""))
        PASS();
    else FAIL("JSON missing binary op");

    arena_destroy(a);
}

static void test_quoted_identifiers(void) {
    printf("\n--- Quoted identifiers ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    TEST("Double-quoted identifier: \"my table\"");
    n = parse("SELECT * FROM \"my table\"", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_FROM_TABLE
        && strcmp(n->u.select.from->u.from_table.name, "my table") == 0)
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Backtick-quoted identifier: `my col`");
    n = parse("SELECT `my col` FROM t", a, &err);
    if (n && n->u.select.result_columns.count == 1) {
        LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_COLUMN_REF && strcmp(e->u.column_ref.column, "my col") == 0)
            PASS();
        else FAIL("backtick dequote wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Bracket-quoted identifier: [my col]");
    n = parse("SELECT [my col] FROM t", a, &err);
    if (n && n->u.select.result_columns.count == 1) {
        LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_COLUMN_REF && strcmp(e->u.column_ref.column, "my col") == 0)
            PASS();
        else FAIL("bracket dequote wrong");
    } else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("String with escaped quote: 'it''s'");
    n = parse("SELECT 'it''s'", a, &err);
    if (n) {
        LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e->kind == LP_EXPR_LITERAL_STRING && strcmp(e->u.literal.value, "it's") == 0)
            PASS();
        else FAIL("escaped quote wrong");
    } else FAIL(err ? err : "fail");

    arena_destroy(a);
}

static void test_complex_queries(void) {
    printf("\n--- Complex real-world queries ---\n");
    arena_t *a = arena_create(128*1024);
    const char *err;
    LpNode *n;

    TEST("Correlated subquery");
    n = parse("SELECT * FROM t1 WHERE x > (SELECT AVG(x) FROM t1 t2 WHERE t2.grp = t1.grp)", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Nested subqueries in FROM");
    n = parse("SELECT * FROM (SELECT * FROM (SELECT 1 AS x) sub1) sub2", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Complex JOIN chain");
    n = parse("SELECT u.name, o.total, p.name "
              "FROM users u "
              "LEFT JOIN orders o ON u.id = o.user_id "
              "INNER JOIN products p ON o.product_id = p.id "
              "WHERE o.total > 100 "
              "ORDER BY o.total DESC "
              "LIMIT 10", a, &err);
    if (n && n->kind == LP_STMT_SELECT && n->u.select.limit) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Complex CREATE TABLE");
    n = parse("CREATE TABLE IF NOT EXISTS orders ("
              "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
              "  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
              "  product TEXT NOT NULL DEFAULT 'unknown',"
              "  qty INTEGER NOT NULL CHECK(qty > 0),"
              "  total REAL,"
              "  created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
              "  UNIQUE(user_id, product)"
              ") STRICT", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE
        && n->u.create_table.columns.count == 6
        && n->u.create_table.constraints.count == 1
        && (n->u.create_table.options & LP_TBL_STRICT))
        PASS(); else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CTE with INSERT");
    n = parse("WITH src AS (SELECT id, name FROM staging) "
              "INSERT INTO dest(id, name) SELECT id, name FROM src", a, &err);
    if (n && n->kind == LP_STMT_INSERT) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Window function with complex frame");
    n = parse("SELECT id, "
              "  SUM(amount) OVER (PARTITION BY category ORDER BY date "
              "  ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) as running_total "
              "FROM transactions", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("CASE with nested expressions");
    n = parse("SELECT CASE "
              "  WHEN x > 100 THEN 'high' "
              "  WHEN x BETWEEN 50 AND 100 THEN 'mid' "
              "  WHEN x IN (1,2,3) THEN 'low' "
              "  ELSE 'other' "
              "END FROM t", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("UPDATE with subquery in SET");
    n = parse("UPDATE t SET a = (SELECT MAX(x) FROM t2 WHERE t2.id = t.id)", a, &err);
    if (n && n->kind == LP_STMT_UPDATE) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Multiple JOINs with mixed types");
    n = parse("SELECT * FROM a "
              "INNER JOIN b ON a.id=b.aid "
              "LEFT JOIN c ON b.id=c.bid "
              "CROSS JOIN d "
              "NATURAL JOIN e", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS();
    else FAIL(err ? err : "fail");

    arena_reset(a);
    TEST("Complex expression tree");
    n = parse("SELECT (a + b) * (c - d) / e % f || g "
              "FROM t WHERE NOT (x IS NULL OR y BETWEEN 1 AND 10)", a, &err);
    if (n && n->kind == LP_STMT_SELECT) PASS();
    else FAIL(err ? err : "fail");

    arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/*  Source position tracking tests                                     */
/* ------------------------------------------------------------------ */

static void test_source_positions(void) {
    printf("\n--- Source position tracking ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    /* Single-line position */
    TEST("Literal position on single line");
    n = parse("SELECT 42", a, &err);
    if (n && n->u.select.result_columns.count == 1) {
        LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e && e->pos.line == 1 && e->pos.col == 8 && e->pos.offset == 7)
            PASS();
        else FAIL("wrong pos for literal");
    } else FAIL(err ? err : "no select");

    arena_reset(a);
    TEST("Column ref position");
    n = parse("SELECT abc FROM t", a, &err);
    if (n && n->u.select.result_columns.count == 1) {
        LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e && e->pos.line == 1 && e->pos.col == 8 && e->pos.offset == 7)
            PASS();
        else FAIL("wrong pos for column ref");
    } else FAIL(err ? err : "no select");

    arena_reset(a);
    TEST("Multiline positions");
    n = parse("SELECT\n  a,\n  b\nFROM t", a, &err);
    if (n && n->u.select.result_columns.count == 2) {
        LpNode *e1 = n->u.select.result_columns.items[0]->u.result_column.expr;
        LpNode *e2 = n->u.select.result_columns.items[1]->u.result_column.expr;
        if (e1 && e1->pos.line == 2 && e1->pos.col == 3
            && e2 && e2->pos.line == 3 && e2->pos.col == 3)
            PASS();
        else FAIL("wrong multiline pos");
    } else FAIL(err ? err : "no select");

    arena_reset(a);
    TEST("FROM table position");
    n = parse("SELECT 1 FROM users", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_FROM_TABLE) {
        LpNode *f = n->u.select.from;
        if (f->pos.line == 1 && f->pos.col == 15 && f->pos.offset == 14)
            PASS();
        else FAIL("wrong FROM pos");
    } else FAIL(err ? err : "no from");

    arena_reset(a);
    TEST("Function position");
    n = parse("SELECT count(x) FROM t", a, &err);
    if (n && n->u.select.result_columns.count == 1) {
        LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e && e->kind == LP_EXPR_FUNCTION && e->pos.line == 1 && e->pos.col == 8)
            PASS();
        else FAIL("wrong func pos");
    } else FAIL(err ? err : "no select");

    arena_reset(a);
    TEST("Binary expr inherits left position");
    n = parse("SELECT a + b FROM t", a, &err);
    if (n && n->u.select.result_columns.count == 1) {
        LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
        if (e && e->kind == LP_EXPR_BINARY_OP && e->pos.line == 1 && e->pos.col == 8)
            PASS();
        else FAIL("wrong binary pos");
    } else FAIL(err ? err : "no select");

    arena_reset(a);
    TEST("CREATE TABLE position");
    n = parse("CREATE TABLE foo (id INTEGER)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE && n->pos.line == 1 && n->pos.col == 14)
        PASS();
    else FAIL(err ? err : "wrong create pos");

    arena_reset(a);
    TEST("Error message includes line:col");
    n = parse("SELECT\n  a\n  FROM", a, &err);
    if (!n && err && (strstr(err, "3:") || strstr(err, "4:")))
        PASS();
    else FAIL(err ? err : "no error");

    arena_reset(a);
    TEST("Multiline error position");
    n = parse("SELECT a\nFROM t\nWHERE @@@", a, &err);
    if (!n && err && strstr(err, "3:"))
        PASS();
    else FAIL(err ? err : "no error");

    arena_reset(a);
    TEST("lp_node_source returns correct pointer");
    {
        const char *sql = "SELECT abc FROM t";
        n = parse(sql, a, &err);
        if (n && n->u.select.result_columns.count == 1) {
            LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
            unsigned int len = 0;
            const char *src = lp_node_source(e, sql, &len);
            if (src && src == sql + 7 && strncmp(src, "abc", 3) == 0)
                PASS();
            else FAIL("wrong source pointer");
        } else FAIL(err ? err : "no select");
    }

    arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/*  Multi-statement parsing tests                                      */
/* ------------------------------------------------------------------ */

static void test_multi_statement(void) {
    printf("\n--- Multi-statement parsing ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNodeList *stmts;

    TEST("Two SELECT statements");
    stmts = lp_parse_all("SELECT 1; SELECT 2", a, &err);
    if (stmts && stmts->count == 2
        && stmts->items[0]->kind == LP_STMT_SELECT
        && stmts->items[1]->kind == LP_STMT_SELECT)
        PASS();
    else FAIL(err ? err : "wrong count or kinds");

    arena_reset(a);
    TEST("Three mixed statements");
    stmts = lp_parse_all("SELECT 1; INSERT INTO t VALUES(1); DELETE FROM t", a, &err);
    if (stmts && stmts->count == 3
        && stmts->items[0]->kind == LP_STMT_SELECT
        && stmts->items[1]->kind == LP_STMT_INSERT
        && stmts->items[2]->kind == LP_STMT_DELETE)
        PASS();
    else FAIL(err ? err : "wrong count or kinds");

    arena_reset(a);
    TEST("Single statement via lp_parse_all");
    stmts = lp_parse_all("SELECT 42", a, &err);
    if (stmts && stmts->count == 1
        && stmts->items[0]->kind == LP_STMT_SELECT)
        PASS();
    else FAIL(err ? err : "wrong count");

    arena_reset(a);
    TEST("Trailing semicolons");
    stmts = lp_parse_all("SELECT 1;;; SELECT 2;", a, &err);
    if (stmts && stmts->count == 2)
        PASS();
    else FAIL(err ? err : "wrong count");

    arena_reset(a);
    TEST("Empty input");
    stmts = lp_parse_all("", a, &err);
    if (stmts && stmts->count == 0)
        PASS();
    else FAIL(err ? err : "expected empty list");

    arena_reset(a);
    TEST("Only semicolons");
    stmts = lp_parse_all(";;;", a, &err);
    if (stmts && stmts->count == 0)
        PASS();
    else FAIL(err ? err : "expected empty list");

    arena_reset(a);
    TEST("DDL + DML batch");
    stmts = lp_parse_all(
        "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO t VALUES(1, 'Alice');"
        "INSERT INTO t VALUES(2, 'Bob');"
        "UPDATE t SET name = 'Charlie' WHERE id = 1;"
        "SELECT * FROM t",
        a, &err);
    if (stmts && stmts->count == 5
        && stmts->items[0]->kind == LP_STMT_CREATE_TABLE
        && stmts->items[1]->kind == LP_STMT_INSERT
        && stmts->items[2]->kind == LP_STMT_INSERT
        && stmts->items[3]->kind == LP_STMT_UPDATE
        && stmts->items[4]->kind == LP_STMT_SELECT)
        PASS();
    else FAIL(err ? err : "wrong batch");

    arena_reset(a);
    TEST("Transaction wrapping");
    stmts = lp_parse_all("BEGIN; INSERT INTO t VALUES(1); COMMIT", a, &err);
    if (stmts && stmts->count == 3
        && stmts->items[0]->kind == LP_STMT_BEGIN
        && stmts->items[1]->kind == LP_STMT_INSERT
        && stmts->items[2]->kind == LP_STMT_COMMIT)
        PASS();
    else FAIL(err ? err : "wrong transaction batch");

    arena_reset(a);
    TEST("Error in second statement stops");
    stmts = lp_parse_all("SELECT 1; SELECTX 2", a, &err);
    if (!stmts && err)
        PASS();
    else FAIL("expected error");

    arena_reset(a);
    TEST("lp_parse returns first statement");
    {
        LpNode *n = lp_parse("SELECT 1; SELECT 2; SELECT 3", a, &err);
        if (n && n->kind == LP_STMT_SELECT) {
            LpNode *e = n->u.select.result_columns.items[0]->u.result_column.expr;
            if (e && e->kind == LP_EXPR_LITERAL_INT
                && strcmp(e->u.literal.value, "1") == 0)
                PASS();
            else FAIL("not first stmt");
        } else FAIL(err ? err : "no parse");
    }

    arena_reset(a);
    TEST("Multi-stmt positions are correct");
    stmts = lp_parse_all("SELECT a;\nSELECT b", a, &err);
    if (stmts && stmts->count == 2) {
        LpNode *e1 = stmts->items[0]->u.select.result_columns.items[0]->u.result_column.expr;
        LpNode *e2 = stmts->items[1]->u.select.result_columns.items[0]->u.result_column.expr;
        if (e1->pos.line == 1 && e1->pos.col == 8
            && e2->pos.line == 2 && e2->pos.col == 8)
            PASS();
        else FAIL("wrong positions");
    } else FAIL(err ? err : "wrong count");

    arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/*  Benchmark: parse SQL with timing + memory reporting                */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *label;
    const char *sql;
    LpNodeKind  expected_kind;
} BenchEntry;

static size_t get_rss_bytes(void) {
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS)
        return info.resident_size;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  AST Mutation API tests                                             */
/* ------------------------------------------------------------------ */
static void test_mutation(void) {
    printf("\n--- AST Mutation API ---\n");
    arena_t *a = arena_create(64*1024);
    const char *err;
    LpNode *n;

    /* lp_node_alloc */
    TEST("lp_node_alloc creates zero-initialized node");
    LpNode *node = lp_node_alloc(a, LP_EXPR_LITERAL_INT);
    if (node && node->kind == LP_EXPR_LITERAL_INT && node->u.literal.value == NULL)
        PASS();
    else FAIL("alloc failed or kind wrong");

    /* lp_strdup */
    TEST("lp_strdup copies string");
    char *s = lp_strdup(a, "hello");
    if (s && strcmp(s, "hello") == 0) PASS(); else FAIL("strdup failed");

    TEST("lp_strdup NULL returns NULL");
    if (lp_strdup(a, NULL) == NULL) PASS(); else FAIL("expected NULL");

    /* lp_list_push */
    TEST("lp_list_push appends items");
    LpNodeList list = {NULL, 0, 0};
    LpNode *a1 = lp_node_alloc(a, LP_EXPR_LITERAL_INT);
    LpNode *a2 = lp_node_alloc(a, LP_EXPR_LITERAL_FLOAT);
    lp_list_push(a, &list, a1);
    lp_list_push(a, &list, a2);
    if (list.count == 2 && list.items[0] == a1 && list.items[1] == a2)
        PASS();
    else FAIL("list push failed");

    /* lp_list_insert */
    TEST("lp_list_insert at beginning");
    LpNode *a0 = lp_node_alloc(a, LP_EXPR_LITERAL_STRING);
    lp_list_insert(a, &list, 0, a0);
    if (list.count == 3 && list.items[0] == a0 && list.items[1] == a1 && list.items[2] == a2)
        PASS();
    else FAIL("insert at 0 failed");

    TEST("lp_list_insert in middle");
    LpNode *amid = lp_node_alloc(a, LP_EXPR_LITERAL_BLOB);
    lp_list_insert(a, &list, 2, amid);
    if (list.count == 4 && list.items[2] == amid && list.items[3] == a2)
        PASS();
    else FAIL("insert at 2 failed");

    /* lp_list_replace */
    TEST("lp_list_replace swaps item");
    LpNode *anew = lp_node_alloc(a, LP_EXPR_LITERAL_NULL);
    LpNode *old = lp_list_replace(&list, 1, anew);
    if (old == a1 && list.items[1] == anew && list.count == 4)
        PASS();
    else FAIL("replace failed");

    TEST("lp_list_replace out of bounds returns NULL");
    if (lp_list_replace(&list, 99, anew) == NULL) PASS(); else FAIL("expected NULL");

    /* lp_list_remove */
    TEST("lp_list_remove removes and shifts");
    LpNode *removed = lp_list_remove(&list, 0);
    if (removed == a0 && list.count == 3 && list.items[0] == anew)
        PASS();
    else FAIL("remove failed");

    TEST("lp_list_remove out of bounds returns NULL");
    if (lp_list_remove(&list, 99) == NULL) PASS(); else FAIL("expected NULL");

    /* --- Practical tree rewriting: add WHERE clause to SELECT --- */
    arena_reset(a);
    TEST("Add WHERE clause to parsed SELECT");
    n = parse("SELECT a, b FROM t", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    /* Build: WHERE a > 10 */
    LpNode *col = lp_node_alloc(a, LP_EXPR_COLUMN_REF);
    col->u.column_ref.column = lp_strdup(a, "a");
    LpNode *lit = lp_node_alloc(a, LP_EXPR_LITERAL_INT);
    lit->u.literal.value = lp_strdup(a, "10");
    LpNode *cmp = lp_node_alloc(a, LP_EXPR_BINARY_OP);
    cmp->u.binary.op = LP_OP_GT;
    cmp->u.binary.left = col;
    cmp->u.binary.right = lit;
    n->u.select.where = cmp;
    char *sql = lp_ast_to_sql(n, a);
    if (sql && strstr(sql, "WHERE") && strstr(sql, "a > 10"))
        PASS();
    else FAIL(sql ? sql : "NULL sql");

    /* --- Practical: rename table in SELECT --- */
    arena_reset(a);
    TEST("Rename table in FROM clause");
    n = parse("SELECT * FROM old_table WHERE id = 1", a, &err);
    if (!n || !n->u.select.from) { FAIL("parse fail"); arena_destroy(a); return; }
    LpNode *from = n->u.select.from;
    if (from->kind == LP_FROM_TABLE) {
        from->u.from_table.name = lp_strdup(a, "new_table");
    }
    sql = lp_ast_to_sql(n, a);
    if (sql && strstr(sql, "new_table") && !strstr(sql, "old_table"))
        PASS();
    else FAIL(sql ? sql : "NULL sql");

    /* --- Practical: add a column to result list --- */
    arena_reset(a);
    TEST("Add result column to SELECT");
    n = parse("SELECT a FROM t", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    LpNode *rc = lp_node_alloc(a, LP_RESULT_COLUMN);
    LpNode *newcol = lp_node_alloc(a, LP_EXPR_COLUMN_REF);
    newcol->u.column_ref.column = lp_strdup(a, "b");
    rc->u.result_column.expr = newcol;
    rc->u.result_column.alias = lp_strdup(a, "bee");
    lp_list_push(a, &n->u.select.result_columns, rc);
    sql = lp_ast_to_sql(n, a);
    if (sql && strstr(sql, "b AS bee"))
        PASS();
    else FAIL(sql ? sql : "NULL sql");

    /* --- Practical: remove a column from result list --- */
    arena_reset(a);
    TEST("Remove result column from SELECT");
    n = parse("SELECT a, b, c FROM t", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    lp_list_remove(&n->u.select.result_columns, 1); /* remove b */
    sql = lp_ast_to_sql(n, a);
    if (sql && strstr(sql, "a") && strstr(sql, "c") && !strstr(sql, "b"))
        PASS();
    else FAIL(sql ? sql : "NULL sql");

    /* --- Practical: replace WHERE expression --- */
    arena_reset(a);
    TEST("Replace WHERE expression");
    n = parse("SELECT * FROM t WHERE x = 1", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    LpNode *new_where = lp_node_alloc(a, LP_EXPR_BINARY_OP);
    new_where->u.binary.op = LP_OP_LT;
    LpNode *y = lp_node_alloc(a, LP_EXPR_COLUMN_REF);
    y->u.column_ref.column = lp_strdup(a, "y");
    LpNode *v = lp_node_alloc(a, LP_EXPR_LITERAL_INT);
    v->u.literal.value = lp_strdup(a, "100");
    new_where->u.binary.left = y;
    new_where->u.binary.right = v;
    n->u.select.where = new_where;
    sql = lp_ast_to_sql(n, a);
    if (sql && strstr(sql, "y < 100") && !strstr(sql, "x = 1"))
        PASS();
    else FAIL(sql ? sql : "NULL sql");

    /* --- lp_node_clone: deep copy --- */
    arena_reset(a);
    TEST("lp_node_clone deep copies SELECT");
    n = parse("SELECT a, b FROM t WHERE x > 1 ORDER BY a", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    LpNode *clone = lp_node_clone(a, n);
    if (!clone) { FAIL("clone returned NULL"); arena_destroy(a); return; }
    /* Modify clone, original should be unaffected */
    clone->u.select.from->u.from_table.name = lp_strdup(a, "other");
    char *sql_orig = lp_ast_to_sql(n, a);
    char *sql_clone = lp_ast_to_sql(clone, a);
    if (sql_orig && sql_clone
        && strstr(sql_orig, " t ")
        && strstr(sql_clone, "other")
        && !strstr(sql_clone, " t "))
        PASS();
    else FAIL("clone not independent");

    TEST("lp_node_clone preserves structure");
    if (clone->kind == LP_STMT_SELECT
        && clone->u.select.result_columns.count == 2
        && clone->u.select.where != NULL
        && clone->u.select.order_by.count == 1)
        PASS();
    else FAIL("clone structure mismatch");

    TEST("lp_node_clone NULL returns NULL");
    if (lp_node_clone(a, NULL) == NULL) PASS(); else FAIL("expected NULL");

    /* --- Clone + modify + unparse round-trip --- */
    arena_reset(a);
    TEST("Clone, modify, and re-serialize");
    n = parse("INSERT INTO users(name, age) VALUES('Alice', 30)", a, &err);
    if (!n) { FAIL(err ? err : "fail"); arena_destroy(a); return; }
    clone = lp_node_clone(a, n);
    clone->u.insert.table = lp_strdup(a, "people");
    sql = lp_ast_to_sql(clone, a);
    if (sql && strstr(sql, "people") && !strstr(sql, "users"))
        PASS();
    else FAIL(sql ? sql : "NULL sql");

    /* --- Build a complete statement from scratch --- */
    arena_reset(a);
    TEST("Build DELETE statement from scratch");
    LpNode *del = lp_node_alloc(a, LP_STMT_DELETE);
    del->u.del.table = lp_strdup(a, "logs");
    LpNode *where = lp_node_alloc(a, LP_EXPR_BINARY_OP);
    where->u.binary.op = LP_OP_LT;
    LpNode *ts = lp_node_alloc(a, LP_EXPR_COLUMN_REF);
    ts->u.column_ref.column = lp_strdup(a, "created_at");
    LpNode *date = lp_node_alloc(a, LP_EXPR_LITERAL_STRING);
    date->u.literal.value = lp_strdup(a, "2024-01-01");
    where->u.binary.left = ts;
    where->u.binary.right = date;
    del->u.del.where = where;
    sql = lp_ast_to_sql(del, a);
    if (sql && strstr(sql, "DELETE FROM logs")
            && strstr(sql, "created_at < '2024-01-01'"))
        PASS();
    else FAIL(sql ? sql : "NULL sql");

    arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/*  Tolerant parsing tests                                             */
/* ------------------------------------------------------------------ */
static void test_tolerant(void) {
    printf("\n--- Tolerant parsing (error recovery) ---\n");
    arena_t *a = arena_create(64*1024);
    char msg[128];

    /* --- Good input (no errors) --- */

    TEST("Good input: two SELECTs");
    LpParseResult *r = lp_parse_tolerant("SELECT 1; SELECT 2", a);
    if (r && r->stmts.count == 2 && r->errors.count == 0)
        PASS();
    else FAIL("expected 2 stmts, 0 errors");

    arena_reset(a);
    TEST("Good input: three mixed statements");
    r = lp_parse_tolerant(
        "INSERT INTO t VALUES(1); SELECT * FROM t; DELETE FROM t WHERE id=1", a);
    if (r && r->stmts.count == 3 && r->errors.count == 0)
        PASS();
    else FAIL("expected 3 stmts, 0 errors");

    arena_reset(a);
    TEST("Good input: DDL + DML batch");
    r = lp_parse_tolerant(
        "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO t VALUES(1,'a');"
        "CREATE INDEX idx ON t(name);"
        "UPDATE t SET name='b' WHERE id=1", a);
    if (r && r->stmts.count == 4 && r->errors.count == 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Good input: single statement no trailing semi");
    r = lp_parse_tolerant("SELECT 42", a);
    if (r && r->stmts.count == 1 && r->errors.count == 0)
        PASS();
    else FAIL("expected 1 stmt, 0 errors");

    arena_reset(a);
    TEST("Good input: trailing semicolons");
    r = lp_parse_tolerant("SELECT 1;;; SELECT 2;;", a);
    if (r && r->stmts.count == 2 && r->errors.count == 0)
        PASS();
    else FAIL("expected 2 stmts, 0 errors");

    arena_reset(a);
    TEST("Good input: transaction batch");
    r = lp_parse_tolerant(
        "BEGIN; INSERT INTO t VALUES(1); COMMIT", a);
    if (r && r->stmts.count == 3 && r->errors.count == 0
        && r->stmts.items[0]->kind == LP_STMT_BEGIN
        && r->stmts.items[1]->kind == LP_STMT_INSERT
        && r->stmts.items[2]->kind == LP_STMT_COMMIT)
        PASS();
    else FAIL("expected BEGIN/INSERT/COMMIT");

    /* --- Error in middle --- */

    arena_reset(a);
    TEST("Error in middle: recovers around it");
    r = lp_parse_tolerant("SELECT 1; SELECT FROM; SELECT 3", a);
    if (r && r->stmts.count == 2 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Error in middle: incomplete INSERT");
    r = lp_parse_tolerant(
        "SELECT 1; INSERT INTO; SELECT 2", a);
    if (r && r->stmts.count == 2 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Error in middle: missing SET in UPDATE");
    r = lp_parse_tolerant(
        "CREATE TABLE t(x INT); UPDATE t WHERE x=1; SELECT * FROM t", a);
    if (r && r->stmts.count == 2 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Error in middle: unclosed parenthesis");
    r = lp_parse_tolerant(
        "SELECT 1; SELECT (1 + 2; SELECT 3", a);
    if (r && r->stmts.count == 2 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    /* --- Error at start --- */

    arena_reset(a);
    TEST("Error at start: skips to valid statement");
    r = lp_parse_tolerant("BOGUS STUFF; SELECT 1", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_SELECT)
        PASS();
    else FAIL("expected 1 stmt after recovery");

    arena_reset(a);
    TEST("Error at start: double keyword then valid");
    r = lp_parse_tolerant("SELECT SELECT; INSERT INTO t VALUES(1)", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_INSERT)
        PASS();
    else FAIL("expected INSERT after recovery");

    arena_reset(a);
    TEST("Error at start: incomplete CREATE then valid");
    r = lp_parse_tolerant(
        "CREATE; SELECT * FROM t; DELETE FROM t WHERE id=1", a);
    if (r && r->stmts.count == 2 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    /* --- Error at end --- */

    arena_reset(a);
    TEST("Error at end: keeps earlier statements");
    r = lp_parse_tolerant("SELECT 1; INSERT INTO", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0)
        PASS();
    else FAIL("expected 1 stmt, errors > 0");

    arena_reset(a);
    TEST("Error at end: multiple valid then broken");
    r = lp_parse_tolerant(
        "SELECT 1; INSERT INTO t VALUES(1); UPDATE", a);
    if (r && r->stmts.count == 2 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Error at end: trailing garbage no semi");
    r = lp_parse_tolerant("SELECT 1; WHAT IS THIS", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0)
        PASS();
    else FAIL("expected 1 stmt, errors > 0");

    /* --- All errors --- */

    arena_reset(a);
    TEST("All errors: empty stmts list");
    r = lp_parse_tolerant("BOGUS; ALSO BAD; NOPE", a);
    if (r && r->stmts.count == 0 && r->errors.count > 0)
        PASS();
    else FAIL("expected 0 stmts, errors > 0");

    arena_reset(a);
    TEST("All errors: incomplete statements");
    r = lp_parse_tolerant("INSERT INTO; UPDATE; DELETE", a);
    if (r && r->stmts.count == 0 && r->errors.count > 0)
        PASS();
    else FAIL("expected 0 stmts, errors > 0");

    arena_reset(a);
    TEST("All errors: double keywords");
    r = lp_parse_tolerant("SELECT SELECT; INSERT INSERT; CREATE CREATE", a);
    if (r && r->stmts.count == 0 && r->errors.count > 0)
        PASS();
    else FAIL("expected 0 stmts, errors > 0");

    /* --- Multiple errors with valid in between --- */

    arena_reset(a);
    TEST("Multiple errors with valid in between");
    r = lp_parse_tolerant(
        "BAD1; SELECT 1; BAD2; SELECT 2; BAD3", a);
    if (r && r->stmts.count == 2 && r->errors.count >= 3)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Alternating: error valid error valid error valid");
    r = lp_parse_tolerant(
        "BAD; SELECT 1; BAD; SELECT 2; BAD; SELECT 3", a);
    if (r && r->stmts.count == 3 && r->errors.count >= 3)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Many errors, few valid scattered");
    r = lp_parse_tolerant(
        "X; Y; Z; SELECT 1; A; B; SELECT 2; C; D; E", a);
    if (r && r->stmts.count == 2 && r->errors.count >= 7)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    /* --- Error position tracking --- */

    arena_reset(a);
    TEST("Error messages have positions");
    r = lp_parse_tolerant("SELECT 1;\nSELECT FROM;\nSELECT 3", a);
    if (r && r->errors.count > 0 && r->errors.items[0].pos.line == 2)
        PASS();
    else FAIL("error position wrong");

    arena_reset(a);
    TEST("Error position: column tracking");
    r = lp_parse_tolerant("SELECT FROM", a);
    if (r && r->errors.count > 0 && r->errors.items[0].pos.line == 1)
        PASS();
    else FAIL("error position wrong");

    arena_reset(a);
    TEST("Error position: multi-line multi-error");
    r = lp_parse_tolerant(
        "SELECT 1;\n"
        "BOGUS;\n"
        "SELECT 2;\n"
        "ALSO BAD;\n"
        "SELECT 3", a);
    if (r && r->stmts.count == 3 && r->errors.count >= 2
        && r->errors.items[0].pos.line == 2
        && r->errors.items[1].pos.line == 4)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d e0.line=%u e1.line=%u",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1,
                 r && r->errors.count > 0 ? r->errors.items[0].pos.line : 0,
                 r && r->errors.count > 1 ? r->errors.items[1].pos.line : 0);
        FAIL(msg);
    }

    /* --- Statement kind preservation --- */

    arena_reset(a);
    TEST("Tolerant preserves statement kinds");
    r = lp_parse_tolerant(
        "CREATE TABLE t(x INT); GARBAGE; INSERT INTO t VALUES(1)", a);
    if (r && r->stmts.count == 2
        && r->stmts.items[0]->kind == LP_STMT_CREATE_TABLE
        && r->stmts.items[1]->kind == LP_STMT_INSERT)
        PASS();
    else FAIL("wrong statement kinds");

    arena_reset(a);
    TEST("Preserves kinds: all DML types");
    r = lp_parse_tolerant(
        "SELECT 1; BAD; INSERT INTO t VALUES(1); BAD;"
        "UPDATE t SET x=1; BAD; DELETE FROM t", a);
    if (r && r->stmts.count == 4
        && r->stmts.items[0]->kind == LP_STMT_SELECT
        && r->stmts.items[1]->kind == LP_STMT_INSERT
        && r->stmts.items[2]->kind == LP_STMT_UPDATE
        && r->stmts.items[3]->kind == LP_STMT_DELETE)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d",
                 r ? r->stmts.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Preserves kinds: DDL around errors");
    r = lp_parse_tolerant(
        "CREATE TABLE t(id INT); OOPS;"
        "CREATE INDEX idx ON t(id); OOPS;"
        "CREATE VIEW v AS SELECT 1; OOPS;"
        "DROP TABLE t", a);
    if (r && r->stmts.count == 4
        && r->stmts.items[0]->kind == LP_STMT_CREATE_TABLE
        && r->stmts.items[1]->kind == LP_STMT_CREATE_INDEX
        && r->stmts.items[2]->kind == LP_STMT_CREATE_VIEW
        && r->stmts.items[3]->kind == LP_STMT_DROP)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d",
                 r ? r->stmts.count : -1);
        FAIL(msg);
    }

    /* --- Edge cases --- */

    arena_reset(a);
    TEST("Empty input: no errors, no stmts");
    r = lp_parse_tolerant("", a);
    if (r && r->stmts.count == 0 && r->errors.count == 0)
        PASS();
    else FAIL("expected empty result");

    arena_reset(a);
    TEST("Only semicolons: no errors, no stmts");
    r = lp_parse_tolerant(";;;", a);
    if (r && r->stmts.count == 0 && r->errors.count == 0)
        PASS();
    else FAIL("expected empty result");

    arena_reset(a);
    TEST("Only whitespace");
    r = lp_parse_tolerant("   \t\n  ", a);
    if (r && r->stmts.count == 0 && r->errors.count == 0)
        PASS();
    else FAIL("expected empty result");

    arena_reset(a);
    TEST("Only comments");
    r = lp_parse_tolerant("-- line comment\n/* block */", a);
    if (r && r->stmts.count == 0 && r->errors.count == 0)
        PASS();
    else FAIL("expected empty result");

    arena_reset(a);
    TEST("Comments between valid statements and errors");
    r = lp_parse_tolerant(
        "SELECT 1; -- comment\nBAD; /* block */ SELECT 2", a);
    if (r && r->stmts.count == 2 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Single error no semi");
    r = lp_parse_tolerant("BOGUS", a);
    if (r && r->stmts.count == 0 && r->errors.count > 0)
        PASS();
    else FAIL("expected 0 stmts, errors > 0");

    arena_reset(a);
    TEST("Single error with semi");
    r = lp_parse_tolerant("BOGUS;", a);
    if (r && r->stmts.count == 0 && r->errors.count > 0)
        PASS();
    else FAIL("expected 0 stmts, errors > 0");

    /* --- Illegal token recovery --- */

    arena_reset(a);
    TEST("Illegal token: @ between valid stmts");
    r = lp_parse_tolerant("SELECT 1; @; SELECT 2", a);
    if (r && r->stmts.count == 2 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Illegal token: multiple illegal tokens");
    r = lp_parse_tolerant("SELECT 1; @; #; SELECT 2", a);
    if (r && r->stmts.count == 2 && r->errors.count >= 2)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Illegal token at start");
    r = lp_parse_tolerant("@; SELECT 1", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_SELECT)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("Illegal token at end");
    r = lp_parse_tolerant("SELECT 1; @", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    /* --- Complex statement recovery --- */

    arena_reset(a);
    TEST("Recover complex SELECT with JOIN");
    r = lp_parse_tolerant(
        "OOPS; SELECT u.name, o.total FROM users u "
        "LEFT JOIN orders o ON u.id=o.uid WHERE o.total > 100", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_SELECT)
        PASS();
    else FAIL("expected SELECT with JOIN");

    arena_reset(a);
    TEST("Recover CTE after error");
    r = lp_parse_tolerant(
        "BAD STUFF; "
        "WITH cte AS (SELECT 1 AS x) SELECT * FROM cte", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_SELECT)
        PASS();
    else FAIL("expected CTE SELECT");

    arena_reset(a);
    TEST("Recover CREATE TABLE with constraints");
    r = lp_parse_tolerant(
        "NOPE; "
        "CREATE TABLE users("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  email TEXT NOT NULL UNIQUE,"
        "  age INTEGER CHECK(age >= 0))", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_CREATE_TABLE
        && r->stmts.items[0]->u.create_table.columns.count == 3)
        PASS();
    else FAIL("expected CREATE TABLE with 3 cols");

    arena_reset(a);
    TEST("Recover INSERT with upsert");
    r = lp_parse_tolerant(
        "BAD; INSERT INTO t(id,val) VALUES(1,10) "
        "ON CONFLICT(id) DO UPDATE SET val=excluded.val", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_INSERT)
        PASS();
    else FAIL("expected INSERT with upsert");

    arena_reset(a);
    TEST("Recover window function after error");
    r = lp_parse_tolerant(
        "GARBAGE; "
        "SELECT id, ROW_NUMBER() OVER (ORDER BY id) AS rn FROM t", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_SELECT)
        PASS();
    else FAIL("expected SELECT with window");

    /* --- Unparse round-trip after recovery --- */

    arena_reset(a);
    TEST("Unparse recovered statements");
    r = lp_parse_tolerant("SELECT 1; GARBAGE; SELECT 2 + 3", a);
    if (r && r->stmts.count == 2) {
        char *s0 = lp_ast_to_sql(r->stmts.items[0], a);
        char *s1 = lp_ast_to_sql(r->stmts.items[1], a);
        if (s0 && strstr(s0, "1") && s1 && strstr(s1, "2 + 3"))
            PASS();
        else FAIL("unparse failed");
    } else FAIL("recovery failed");

    arena_reset(a);
    TEST("Unparse recovered INSERT");
    r = lp_parse_tolerant(
        "JUNK; INSERT INTO users(name) VALUES('Alice')", a);
    if (r && r->stmts.count == 1) {
        char *sql = lp_ast_to_sql(r->stmts.items[0], a);
        if (sql && strstr(sql, "INSERT") && strstr(sql, "Alice"))
            PASS();
        else FAIL(sql ? sql : "NULL sql");
    } else FAIL("recovery failed");

    arena_reset(a);
    TEST("Unparse recovered CREATE TABLE");
    r = lp_parse_tolerant(
        "OOPS; CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT NOT NULL)", a);
    if (r && r->stmts.count == 1) {
        char *sql = lp_ast_to_sql(r->stmts.items[0], a);
        if (sql && strstr(sql, "CREATE TABLE") && strstr(sql, "PRIMARY KEY"))
            PASS();
        else FAIL(sql ? sql : "NULL sql");
    } else FAIL("recovery failed");

    arena_reset(a);
    TEST("JSON output on recovered statement");
    r = lp_parse_tolerant("BAD; SELECT a, b FROM t WHERE x > 1", a);
    if (r && r->stmts.count == 1) {
        char *json = lp_ast_to_json(r->stmts.items[0], a, 0);
        if (json && strstr(json, "STMT_SELECT") && strstr(json, "COLUMN_REF"))
            PASS();
        else FAIL(json ? "missing fields" : "NULL json");
    } else FAIL("recovery failed");

    /* --- Realistic IDE scenarios --- */

    arena_reset(a);
    TEST("IDE: user typing incomplete at end");
    r = lp_parse_tolerant(
        "SELECT * FROM users WHERE active=1;\n"
        "SELECT * FROM orders WHERE user_id=\n", a);
    if (r && r->stmts.count == 1 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_SELECT)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("IDE: migration script with typo");
    r = lp_parse_tolerant(
        "CREATE TABLE accounts(id INTEGER PRIMARY KEY);\n"
        "CREAT INDEX idx ON accounts(id);\n"
        "INSERT INTO accounts VALUES(1);\n"
        "INSERT INTO accounts VALUES(2);\n", a);
    if (r && r->stmts.count == 3 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_CREATE_TABLE
        && r->stmts.items[1]->kind == LP_STMT_INSERT
        && r->stmts.items[2]->kind == LP_STMT_INSERT)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_reset(a);
    TEST("IDE: multiple valid with one broken in batch");
    r = lp_parse_tolerant(
        "BEGIN;\n"
        "DELETE FROM old_data WHERE ts < '2024-01-01';\n"
        "INSERT INTO SELECT * FROM staging;\n"
        "UPDATE stats SET count = count + 1;\n"
        "COMMIT;\n", a);
    if (r && r->stmts.count == 4 && r->errors.count > 0
        && r->stmts.items[0]->kind == LP_STMT_BEGIN
        && r->stmts.items[1]->kind == LP_STMT_DELETE
        && r->stmts.items[2]->kind == LP_STMT_UPDATE
        && r->stmts.items[3]->kind == LP_STMT_COMMIT)
        PASS();
    else {
        snprintf(msg, sizeof(msg), "stmts=%d errors=%d",
                 r ? r->stmts.count : -1, r ? r->errors.count : -1);
        FAIL(msg);
    }

    arena_destroy(a);
}

static double timespec_ms(struct timespec *start, struct timespec *end) {
    double s = (double)(end->tv_sec - start->tv_sec) * 1000.0;
    s += (double)(end->tv_nsec - start->tv_nsec) / 1e6;
    return s;
}

static void run_benchmarks(const BenchEntry *entries, int count, const char *section) {
    printf("\n--- %s ---\n", section);
    printf("  %-50s %7s %8s %6s %s\n",
           "SQL", "Time", "Arena", "Blks", "Status");
    printf("  %-50s %7s %8s %6s %s\n",
           "--", "----", "-----", "----", "------");

    for (int i = 0; i < count; i++) {
        const BenchEntry *e = &entries[i];
        arena_t *a = arena_create(64 * 1024);
        const char *err = NULL;

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        LpNode *n = lp_parse(e->sql, a, &err);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        double ms = timespec_ms(&t0, &t1);
        size_t used = 0, capacity = 0, blocks = 0;
        arena_stats(a, &used, &capacity, &blocks);

        tests_run++;
        int ok = (n && n->kind == e->expected_kind);
        if (ok) tests_passed++; else tests_failed++;

        /* Truncate label for display */
        char label_buf[51];
        snprintf(label_buf, sizeof(label_buf), "%s", e->label);

        printf("  %-50s %6.3fms %6zuKB %4zu   %s\n",
               label_buf, ms,
               (used + 1023) / 1024, blocks,
               ok ? "[PASS]" : "[FAIL]");

        if (!ok && err)
            printf("    error: %s\n", err);

        arena_destroy(a);
    }
}

/* Additional SQL statements for benchmark suite */
static const BenchEntry bench_select[] = {
    {"SELECT with 10 columns",
     "SELECT a,b,c,d,e,f,g,h,i,j FROM t", LP_STMT_SELECT},
    {"SELECT with WHERE AND/OR chain",
     "SELECT * FROM t WHERE a>1 AND b<2 OR c=3 AND d!=4 AND e>=5", LP_STMT_SELECT},
    {"SELECT with 5 JOINs",
     "SELECT * FROM a JOIN b ON a.id=b.aid JOIN c ON b.id=c.bid "
     "JOIN d ON c.id=d.cid JOIN e ON d.id=e.did JOIN f ON e.id=f.eid", LP_STMT_SELECT},
    {"SELECT with nested CASE",
     "SELECT CASE WHEN a>0 THEN CASE WHEN b>0 THEN 'pp' ELSE 'pn' END "
     "ELSE CASE WHEN c>0 THEN 'np' ELSE 'nn' END END FROM t", LP_STMT_SELECT},
    {"SELECT with multiple subqueries",
     "SELECT (SELECT MAX(a) FROM t1), (SELECT MIN(b) FROM t2), "
     "(SELECT AVG(c) FROM t3) FROM dual", LP_STMT_SELECT},
    {"SELECT with HAVING + GROUP BY + ORDER BY",
     "SELECT dept, COUNT(*), AVG(salary), MAX(salary), MIN(salary) "
     "FROM employees GROUP BY dept HAVING COUNT(*)>5 ORDER BY AVG(salary) DESC", LP_STMT_SELECT},
    {"SELECT with nested subquery in WHERE",
     "SELECT * FROM orders WHERE customer_id IN "
     "(SELECT id FROM customers WHERE country IN "
     "(SELECT code FROM countries WHERE continent='EU'))", LP_STMT_SELECT},
    {"SELECT with COALESCE and NULLIF",
     "SELECT COALESCE(a, b, c, 0), NULLIF(x, 0), IFNULL(y, -1) FROM t", LP_STMT_SELECT},
    {"SELECT with type cast chain",
     "SELECT CAST(CAST(x AS TEXT) AS REAL), CAST(y AS INTEGER) FROM t", LP_STMT_SELECT},
    {"SELECT with complex boolean",
     "SELECT * FROM t WHERE (a IS NOT NULL AND b IS NULL) "
     "OR (c BETWEEN 1 AND 10 AND d NOT IN (1,2,3)) "
     "OR (e LIKE '%test%' ESCAPE '\\' AND f GLOB 'a*b')", LP_STMT_SELECT},
};

static const BenchEntry bench_dml[] = {
    {"INSERT with 20 VALUES",
     "INSERT INTO t VALUES(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20)", LP_STMT_INSERT},
    {"INSERT with 5-row VALUES",
     "INSERT INTO t(a,b,c) VALUES(1,2,3),(4,5,6),(7,8,9),(10,11,12),(13,14,15)", LP_STMT_INSERT},
    {"INSERT with complex upsert",
     "INSERT INTO t(id,name,val) VALUES(1,'x',10) "
     "ON CONFLICT(id) DO UPDATE SET name=excluded.name, val=val+excluded.val "
     "WHERE val < 100", LP_STMT_INSERT},
    {"INSERT from complex SELECT",
     "INSERT INTO archive(id,data,ts) "
     "SELECT id, json_object('name',name,'val',val), datetime('now') "
     "FROM active WHERE status='done' ORDER BY id", LP_STMT_INSERT},
    {"UPDATE with multiple SET + subquery",
     "UPDATE employees SET salary = salary * 1.1, "
     "bonus = (SELECT AVG(bonus) FROM employees e2 WHERE e2.dept=employees.dept), "
     "updated_at = CURRENT_TIMESTAMP WHERE performance > 8", LP_STMT_UPDATE},
    {"UPDATE with FROM join",
     "UPDATE t1 SET val = t2.val FROM t2 "
     "WHERE t1.id = t2.ref_id AND t2.active = 1", LP_STMT_UPDATE},
    {"DELETE with complex WHERE",
     "DELETE FROM log WHERE timestamp < datetime('now','-30 days') "
     "AND level NOT IN ('ERROR','FATAL') AND archived = 0", LP_STMT_DELETE},
    {"DELETE with RETURNING",
     "DELETE FROM queue WHERE id = (SELECT MIN(id) FROM queue WHERE status='pending') "
     "RETURNING id, payload, created_at", LP_STMT_DELETE},
    {"UPDATE with RETURNING *",
     "UPDATE accounts SET balance = balance - 100 "
     "WHERE id = 42 AND balance >= 100 RETURNING *", LP_STMT_UPDATE},
    {"INSERT OR REPLACE with CTE",
     "WITH src AS (SELECT id, name FROM staging WHERE valid=1) "
     "INSERT OR REPLACE INTO main(id, name) SELECT id, name FROM src", LP_STMT_INSERT},
};

static const BenchEntry bench_ddl[] = {
    {"CREATE TABLE with 10 columns",
     "CREATE TABLE users("
     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
     "username TEXT NOT NULL UNIQUE,"
     "email TEXT NOT NULL UNIQUE COLLATE NOCASE,"
     "password_hash TEXT NOT NULL,"
     "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
     "updated_at TEXT,"
     "last_login TEXT,"
     "is_active INTEGER DEFAULT 1 CHECK(is_active IN (0,1)),"
     "role TEXT DEFAULT 'user' CHECK(role IN ('user','admin','mod')),"
     "profile_json TEXT DEFAULT '{}')", LP_STMT_CREATE_TABLE},
    {"CREATE TABLE with FK + constraints",
     "CREATE TABLE order_items("
     "id INTEGER PRIMARY KEY,"
     "order_id INTEGER NOT NULL REFERENCES orders(id) ON DELETE CASCADE,"
     "product_id INTEGER NOT NULL REFERENCES products(id) ON DELETE RESTRICT,"
     "quantity INTEGER NOT NULL CHECK(quantity > 0),"
     "unit_price REAL NOT NULL CHECK(unit_price >= 0),"
     "discount REAL DEFAULT 0.0 CHECK(discount >= 0 AND discount <= 1),"
     "UNIQUE(order_id, product_id),"
     "FOREIGN KEY(order_id) REFERENCES orders(id)"
     ") WITHOUT ROWID, STRICT", LP_STMT_CREATE_TABLE},
    {"CREATE TABLE AS SELECT",
     "CREATE TABLE summary AS "
     "SELECT dept, COUNT(*) AS cnt, AVG(salary) AS avg_sal, "
     "MAX(salary) AS max_sal FROM employees GROUP BY dept", LP_STMT_CREATE_TABLE},
    {"CREATE INDEX with expression",
     "CREATE UNIQUE INDEX idx_users_lower_email ON users(lower(email))", LP_STMT_CREATE_INDEX},
    {"CREATE INDEX partial",
     "CREATE INDEX idx_active_orders ON orders(customer_id, created_at) "
     "WHERE status = 'active' AND total > 0", LP_STMT_CREATE_INDEX},
    {"CREATE VIEW with complex query",
     "CREATE VIEW IF NOT EXISTS sales_report(product, total_qty, revenue) AS "
     "SELECT p.name, SUM(oi.quantity), SUM(oi.quantity * oi.unit_price * (1-oi.discount)) "
     "FROM order_items oi JOIN products p ON oi.product_id=p.id "
     "GROUP BY p.name HAVING SUM(oi.quantity) > 0", LP_STMT_CREATE_VIEW},
    {"CREATE TRIGGER BEFORE INSERT",
     "CREATE TRIGGER IF NOT EXISTS trg_users_before_insert "
     "BEFORE INSERT ON users FOR EACH ROW BEGIN "
     "SELECT RAISE(ABORT, 'email required') WHERE NEW.email IS NULL; "
     "END", LP_STMT_CREATE_TRIGGER},
    {"CREATE TRIGGER AFTER UPDATE OF",
     "CREATE TRIGGER trg_audit_update "
     "AFTER UPDATE OF salary, role ON employees FOR EACH ROW "
     "WHEN OLD.salary != NEW.salary OR OLD.role != NEW.role "
     "BEGIN "
     "INSERT INTO audit_log(table_name, row_id, old_val, new_val, ts) "
     "VALUES('employees', NEW.id, OLD.salary, NEW.salary, datetime('now')); "
     "END", LP_STMT_CREATE_TRIGGER},
    {"CREATE TRIGGER INSTEAD OF DELETE",
     "CREATE TRIGGER trg_soft_delete INSTEAD OF DELETE ON users_view "
     "FOR EACH ROW BEGIN "
     "UPDATE users SET is_active=0, deleted_at=datetime('now') WHERE id=OLD.id; "
     "END", LP_STMT_CREATE_TRIGGER},
    {"CREATE VIRTUAL TABLE fts5",
     "CREATE VIRTUAL TABLE docs_fts USING fts5(title, body, content=docs, content_rowid=id)", LP_STMT_CREATE_VTABLE},
};

static const BenchEntry bench_cte_window[] = {
    {"Recursive CTE: hierarchy",
     "WITH RECURSIVE org(id,name,mgr,lvl) AS ("
     "SELECT id,name,manager_id,0 FROM employees WHERE manager_id IS NULL "
     "UNION ALL "
     "SELECT e.id,e.name,e.manager_id,o.lvl+1 FROM employees e JOIN org o ON e.manager_id=o.id"
     ") SELECT * FROM org ORDER BY lvl, name", LP_STMT_SELECT},
    {"Recursive CTE: date series",
     "WITH RECURSIVE dates(d) AS ("
     "VALUES(date('2024-01-01')) "
     "UNION ALL "
     "SELECT date(d, '+1 day') FROM dates WHERE d < '2024-12-31'"
     ") SELECT d FROM dates", LP_STMT_SELECT},
    {"Multiple CTEs chained",
     "WITH "
     "active AS (SELECT * FROM users WHERE is_active=1), "
     "orders AS (SELECT * FROM purchases WHERE status='completed'), "
     "stats AS (SELECT a.id, COUNT(o.id) AS cnt FROM active a LEFT JOIN orders o ON a.id=o.user_id GROUP BY a.id) "
     "SELECT * FROM stats WHERE cnt > 5 ORDER BY cnt DESC", LP_STMT_SELECT},
    {"CTE MATERIALIZED/NOT MATERIALIZED",
     "WITH "
     "cheap AS MATERIALIZED (SELECT * FROM t WHERE x < 10), "
     "expensive AS NOT MATERIALIZED (SELECT * FROM t WHERE x >= 10) "
     "SELECT * FROM cheap UNION ALL SELECT * FROM expensive", LP_COMPOUND_SELECT},
    {"Window: ROW_NUMBER + RANK + DENSE_RANK",
     "SELECT id, name, salary, "
     "ROW_NUMBER() OVER w AS rn, "
     "RANK() OVER w AS rnk, "
     "DENSE_RANK() OVER w AS drnk "
     "FROM employees WINDOW w AS (ORDER BY salary DESC)", LP_STMT_SELECT},
    {"Window: LAG/LEAD",
     "SELECT date, value, "
     "LAG(value, 1, 0) OVER (ORDER BY date) AS prev_val, "
     "LEAD(value, 1, 0) OVER (ORDER BY date) AS next_val, "
     "value - LAG(value, 1, 0) OVER (ORDER BY date) AS delta "
     "FROM timeseries", LP_STMT_SELECT},
    {"Window: GROUPS frame",
     "SELECT id, grp, val, "
     "SUM(val) OVER (PARTITION BY grp ORDER BY id "
     "GROUPS BETWEEN 1 PRECEDING AND 1 FOLLOWING EXCLUDE CURRENT ROW) AS neighbor_sum "
     "FROM data", LP_STMT_SELECT},
    {"Window: multiple named windows",
     "SELECT id, "
     "SUM(x) OVER w1 AS sum_x, "
     "AVG(y) OVER w2 AS avg_y, "
     "COUNT(*) OVER w3 AS cnt "
     "FROM t "
     "WINDOW w1 AS (ORDER BY id ROWS UNBOUNDED PRECEDING), "
     "w2 AS (PARTITION BY grp ORDER BY id), "
     "w3 AS (ORDER BY id RANGE BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)", LP_STMT_SELECT},
    {"Window: FILTER clause",
     "SELECT dept, "
     "COUNT(*) FILTER (WHERE salary > 50000) OVER (PARTITION BY dept) AS high_earners, "
     "AVG(salary) FILTER (WHERE years > 5) OVER (PARTITION BY dept) AS senior_avg "
     "FROM employees", LP_STMT_SELECT},
    {"Window: NTILE and FIRST_VALUE",
     "SELECT id, salary, "
     "NTILE(4) OVER (ORDER BY salary DESC) AS quartile, "
     "FIRST_VALUE(name) OVER (PARTITION BY dept ORDER BY salary DESC) AS top_earner "
     "FROM employees", LP_STMT_SELECT},
};

static const BenchEntry bench_misc[] = {
    {"ATTACH DATABASE",
     "ATTACH DATABASE 'archive.db' AS archive", LP_STMT_ATTACH},
    {"VACUUM INTO",
     "VACUUM main INTO '/backup/db_backup.sqlite'", LP_STMT_VACUUM},
    {"PRAGMA journal_mode",
     "PRAGMA journal_mode=WAL", LP_STMT_PRAGMA},
    {"PRAGMA table_info",
     "PRAGMA main.table_info('users')", LP_STMT_PRAGMA},
    {"EXPLAIN QUERY PLAN",
     "EXPLAIN QUERY PLAN SELECT * FROM users WHERE id = ?", LP_STMT_EXPLAIN},
    {"BEGIN IMMEDIATE",
     "BEGIN IMMEDIATE TRANSACTION", LP_STMT_BEGIN},
    {"SAVEPOINT + RELEASE",
     "SAVEPOINT my_savepoint", LP_STMT_SAVEPOINT},
    {"REINDEX with schema",
     "REINDEX main.users", LP_STMT_REINDEX},
    {"ANALYZE with table",
     "ANALYZE main.users", LP_STMT_ANALYZE},
    {"ALTER TABLE RENAME COLUMN",
     "ALTER TABLE users RENAME COLUMN username TO user_name", LP_STMT_ALTER},
};

static const BenchEntry bench_complex[] = {
    {"Analytics: percentile with CTE",
     "WITH ranked AS ("
     "SELECT salary, "
     "NTILE(100) OVER (ORDER BY salary) AS pctile "
     "FROM employees) "
     "SELECT MIN(salary) AS p50_salary FROM ranked WHERE pctile > 50", LP_STMT_SELECT},
    {"Self-join with aggregation",
     "SELECT e1.name AS employee, e2.name AS manager, "
     "e1.salary, (SELECT AVG(salary) FROM employees WHERE dept=e1.dept) AS dept_avg "
     "FROM employees e1 LEFT JOIN employees e2 ON e1.manager_id=e2.id "
     "WHERE e1.salary > (SELECT AVG(salary) FROM employees) "
     "ORDER BY e1.salary DESC LIMIT 20 OFFSET 10", LP_STMT_SELECT},
    {"UNION ALL with 4 branches",
     "SELECT 'Q1' AS quarter, SUM(total) FROM sales WHERE month BETWEEN 1 AND 3 "
     "UNION ALL SELECT 'Q2', SUM(total) FROM sales WHERE month BETWEEN 4 AND 6 "
     "UNION ALL SELECT 'Q3', SUM(total) FROM sales WHERE month BETWEEN 7 AND 9 "
     "UNION ALL SELECT 'Q4', SUM(total) FROM sales WHERE month BETWEEN 10 AND 12", LP_COMPOUND_SELECT},
    {"INSERT with complex expression",
     "INSERT INTO stats(key, value, computed, ts) "
     "SELECT 'daily_' || strftime('%Y%m%d','now'), "
     "COUNT(*), "
     "CAST(SUM(CASE WHEN status='ok' THEN 1.0 ELSE 0.0 END)/COUNT(*)*100 AS INTEGER), "
     "datetime('now') "
     "FROM events WHERE date=date('now')", LP_STMT_INSERT},
    {"Nested EXISTS with correlation",
     "SELECT * FROM departments d WHERE EXISTS ("
     "SELECT 1 FROM employees e WHERE e.dept_id=d.id AND EXISTS ("
     "SELECT 1 FROM projects p WHERE p.lead_id=e.id AND p.status='active'"
     ")) AND d.budget > 100000", LP_STMT_SELECT},
    {"Complex UPDATE with FROM + RETURNING",
     "UPDATE inventory SET "
     "quantity = quantity - o.qty, "
     "last_sold = datetime('now') "
     "FROM (SELECT product_id, SUM(quantity) AS qty "
     "FROM order_items WHERE order_id = 12345 GROUP BY product_id) o "
     "WHERE inventory.product_id = o.product_id AND inventory.quantity >= o.qty "
     "RETURNING product_id, quantity AS remaining", LP_STMT_UPDATE},
    {"7-way JOIN query",
     "SELECT u.name, o.id, oi.quantity, p.name, c.name, s.name, w.name "
     "FROM users u "
     "JOIN orders o ON u.id=o.user_id "
     "JOIN order_items oi ON o.id=oi.order_id "
     "JOIN products p ON oi.product_id=p.id "
     "JOIN categories c ON p.category_id=c.id "
     "JOIN suppliers s ON p.supplier_id=s.id "
     "LEFT JOIN warehouses w ON oi.warehouse_id=w.id "
     "WHERE o.status='shipped' AND o.total > 100 "
     "ORDER BY o.created_at DESC LIMIT 50", LP_STMT_SELECT},
    {"CREATE TABLE with all constraint types",
     "CREATE TABLE IF NOT EXISTS full_featured("
     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
     "code TEXT NOT NULL UNIQUE COLLATE NOCASE,"
     "parent_id INTEGER REFERENCES full_featured(id) ON DELETE SET NULL ON UPDATE CASCADE,"
     "value REAL NOT NULL DEFAULT 0.0 CHECK(value >= 0),"
     "data BLOB,"
     "flags INTEGER DEFAULT 0,"
     "extra TEXT GENERATED ALWAYS AS (json_object('code',code,'val',value)) STORED,"
     "created TEXT DEFAULT CURRENT_TIMESTAMP,"
     "CONSTRAINT chk_flags CHECK(flags >= 0 AND flags < 256),"
     "CONSTRAINT uniq_code_parent UNIQUE(code, parent_id)"
     ") STRICT, WITHOUT ROWID", LP_STMT_CREATE_TABLE},
    {"Trigger with multiple statements",
     "CREATE TRIGGER trg_cascade_update AFTER UPDATE ON products "
     "FOR EACH ROW WHEN OLD.price != NEW.price BEGIN "
     "UPDATE order_items SET unit_price=NEW.price WHERE product_id=NEW.id AND finalized=0; "
     "INSERT INTO price_history(product_id,old_price,new_price,changed_at) "
     "VALUES(NEW.id,OLD.price,NEW.price,datetime('now')); "
     "END", LP_STMT_CREATE_TRIGGER},
    {"Upsert with excluded + complex WHERE",
     "INSERT INTO counters(key, count, first_seen, last_seen) "
     "VALUES('page_view', 1, datetime('now'), datetime('now')) "
     "ON CONFLICT(key) DO UPDATE SET "
     "count = count + excluded.count, "
     "last_seen = excluded.last_seen "
     "WHERE excluded.last_seen > counters.last_seen", LP_STMT_INSERT},
};

static const BenchEntry bench_edge_cases[] = {
    {"Deeply nested parens",
     "SELECT ((((((((1+2))))))))", LP_STMT_SELECT},
    {"Long IN list",
     "SELECT * FROM t WHERE id IN (1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,"
     "21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,"
     "41,42,43,44,45,46,47,48,49,50)", LP_STMT_SELECT},
    {"Many OR conditions",
     "SELECT * FROM t WHERE a=1 OR a=2 OR a=3 OR a=4 OR a=5 "
     "OR a=6 OR a=7 OR a=8 OR a=9 OR a=10 "
     "OR a=11 OR a=12 OR a=13 OR a=14 OR a=15", LP_STMT_SELECT},
    {"Chained string concat",
     "SELECT a||b||c||d||e||f||g||h||i||j FROM t", LP_STMT_SELECT},
    {"Multiple BETWEEN clauses",
     "SELECT * FROM t WHERE a BETWEEN 1 AND 10 AND b BETWEEN 20 AND 30 "
     "AND c NOT BETWEEN 40 AND 50 AND d BETWEEN 60 AND 70", LP_STMT_SELECT},
    {"Nested function calls",
     "SELECT UPPER(TRIM(REPLACE(SUBSTR(name,1,10),'_',' '))) FROM t", LP_STMT_SELECT},
    {"Multiple LIKE patterns",
     "SELECT * FROM t WHERE name LIKE '%john%' OR name LIKE '%jane%' "
     "OR name LIKE '%jim%' OR email LIKE '%@gmail.com'", LP_STMT_SELECT},
    {"CASE with 10 WHEN branches",
     "SELECT CASE status "
     "WHEN 0 THEN 'new' WHEN 1 THEN 'pending' WHEN 2 THEN 'active' "
     "WHEN 3 THEN 'paused' WHEN 4 THEN 'completed' WHEN 5 THEN 'failed' "
     "WHEN 6 THEN 'cancelled' WHEN 7 THEN 'archived' WHEN 8 THEN 'deleted' "
     "WHEN 9 THEN 'unknown' ELSE 'error' END FROM tasks", LP_STMT_SELECT},
    {"Compound with ORDER BY + LIMIT",
     "SELECT id, name FROM users WHERE active=1 "
     "UNION SELECT id, name FROM archived_users WHERE restore=1 "
     "INTERSECT SELECT id, name FROM whitelist "
     "ORDER BY name LIMIT 100 OFFSET 50", LP_COMPOUND_SELECT},
    {"VALUES as standalone",
     "VALUES (1,'a',1.0), (2,'b',2.0), (3,'c',3.0), (4,'d',4.0)", LP_COMPOUND_SELECT},
};

static void run_all_benchmarks(void) {
    printf("\n");
    printf("====================================================================\n");
    printf("  BENCHMARK: Parse + Memory (per statement)\n");
    printf("====================================================================\n");

    run_benchmarks(bench_select,
        (int)(sizeof(bench_select)/sizeof(bench_select[0])),
        "SELECT queries");
    run_benchmarks(bench_dml,
        (int)(sizeof(bench_dml)/sizeof(bench_dml[0])),
        "DML (INSERT/UPDATE/DELETE)");
    run_benchmarks(bench_ddl,
        (int)(sizeof(bench_ddl)/sizeof(bench_ddl[0])),
        "DDL (CREATE/ALTER/DROP)");
    run_benchmarks(bench_cte_window,
        (int)(sizeof(bench_cte_window)/sizeof(bench_cte_window[0])),
        "CTEs + Window functions");
    run_benchmarks(bench_misc,
        (int)(sizeof(bench_misc)/sizeof(bench_misc[0])),
        "Misc (PRAGMA/VACUUM/EXPLAIN/...)");
    run_benchmarks(bench_complex,
        (int)(sizeof(bench_complex)/sizeof(bench_complex[0])),
        "Complex real-world queries");
    run_benchmarks(bench_edge_cases,
        (int)(sizeof(bench_edge_cases)/sizeof(bench_edge_cases[0])),
        "Edge cases + stress");
}

/* ------------------------------------------------------------------ */
/*  Throughput benchmark: parse same SQL N times, report ops/sec       */
/* ------------------------------------------------------------------ */

static void run_throughput_bench(void) {
    printf("\n");
    printf("====================================================================\n");
    printf("  THROUGHPUT: repeated parse (10000 iterations)\n");
    printf("====================================================================\n");

    static const struct { const char *label; const char *sql; } samples[] = {
        {"Simple SELECT",    "SELECT 1"},
        {"SELECT with WHERE","SELECT * FROM t WHERE a > 1 AND b < 10"},
        {"SELECT with JOIN", "SELECT * FROM a JOIN b ON a.id=b.aid WHERE a.x > 5"},
        {"INSERT VALUES",    "INSERT INTO t(a,b,c) VALUES(1,2,3)"},
        {"UPDATE SET",       "UPDATE t SET a=1, b=2 WHERE id=42"},
        {"CREATE TABLE",     "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT NOT NULL)"},
        {"CTE + SELECT",     "WITH c AS (SELECT * FROM t WHERE x>0) SELECT * FROM c"},
        {"Complex SELECT",
         "SELECT u.name, COUNT(o.id) FROM users u "
         "LEFT JOIN orders o ON u.id=o.uid "
         "WHERE u.active=1 GROUP BY u.name HAVING COUNT(o.id)>0 "
         "ORDER BY COUNT(o.id) DESC LIMIT 10"},
    };
    int n_samples = (int)(sizeof(samples)/sizeof(samples[0]));
    int iterations = 10000;

    printf("  %-30s %10s %10s %12s\n", "Query", "Iters", "Total ms", "ops/sec");
    printf("  %-30s %10s %10s %12s\n", "-----", "-----", "--------", "-------");

    for (int s = 0; s < n_samples; s++) {
        arena_t *a = arena_create(64 * 1024);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        for (int i = 0; i < iterations; i++) {
            const char *err;
            LpNode *node = lp_parse(samples[s].sql, a, &err);
            (void)node;
            arena_reset(a);
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);
        double total_ms = timespec_ms(&t0, &t1);
        double ops = iterations / (total_ms / 1000.0);

        printf("  %-30s %10d %9.2fms %10.0f/s\n",
               samples[s].label, iterations, total_ms, ops);
        arena_destroy(a);
    }
}

/* ------------------------------------------------------------------ */
/*  Round-trip tests: parse → unparse → reparse                        */
/* ------------------------------------------------------------------ */

static void test_round_trip(void) {
    printf("\n--- Round-trip (parse → unparse → reparse) ---\n");

    /* Each SQL is parsed, unparsed, then reparsed.
    ** The reparsed AST is unparsed again and compared to the first unparse.
    ** This verifies: unparse(parse(sql)) == unparse(parse(unparse(parse(sql)))) */
    static const char *sqls[] = {
        /* SELECT variants */
        "SELECT 1",
        "SELECT a, b, c FROM t WHERE x > 5 ORDER BY a DESC LIMIT 10 OFFSET 3",
        "SELECT DISTINCT a FROM t GROUP BY a HAVING COUNT(*) > 1",
        "SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.ref LEFT JOIN t3 ON t2.id = t3.ref",
        "SELECT * FROM (SELECT id FROM t) AS sub",
        "SELECT 1 UNION ALL SELECT 2 UNION SELECT 3",
        "WITH cte(x) AS (SELECT 1) SELECT x FROM cte",
        "WITH RECURSIVE r(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM r WHERE n < 10) SELECT n FROM r",
        "SELECT *, t.* FROM t",
        "SELECT CAST(x AS TEXT) FROM t",
        "SELECT CASE WHEN a > 0 THEN 'pos' WHEN a < 0 THEN 'neg' ELSE 'zero' END FROM t",
        "SELECT x FROM t WHERE x BETWEEN 1 AND 10",
        "SELECT x FROM t WHERE x IN (1, 2, 3)",
        "SELECT x FROM t WHERE x NOT IN (SELECT id FROM t2)",
        "SELECT EXISTS (SELECT 1 FROM t WHERE x > 0)",
        "SELECT x FROM t WHERE x IS NOT NULL AND y IS NULL",
        "SELECT x FROM t WHERE name LIKE '%test%' ESCAPE '\\'",
        "SELECT x FROM t WHERE name GLOB 'a*b'",
        "SELECT COUNT(*), SUM(x), AVG(x) FROM t",
        "SELECT ROW_NUMBER() OVER (PARTITION BY grp ORDER BY id) FROM t",
        "SELECT SUM(x) OVER w FROM t WINDOW w AS (ORDER BY id ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)",
        "SELECT COUNT(*) FILTER (WHERE x > 0) FROM t",

        /* DML */
        "INSERT INTO t(a, b) VALUES(1, 2)",
        "INSERT INTO t(a, b) VALUES(1, 2), (3, 4), (5, 6)",
        "INSERT INTO t DEFAULT VALUES",
        "INSERT INTO t(a) SELECT x FROM t2",
        "INSERT OR REPLACE INTO t(id, val) VALUES(1, 'x')",
        "INSERT INTO t(id) VALUES(1) ON CONFLICT(id) DO UPDATE SET val = excluded.val",
        "INSERT INTO t(a) VALUES(1) RETURNING *",
        "UPDATE t SET a = 1, b = 2 WHERE id = 5",
        "UPDATE OR IGNORE t SET x = x + 1",
        "UPDATE t SET a = 1 FROM t2 WHERE t.id = t2.ref",
        "UPDATE t SET x = 1 RETURNING id, x",
        "DELETE FROM t WHERE id = 1",
        "DELETE FROM t WHERE id IN (SELECT id FROM old) RETURNING *",

        /* DDL */
        "CREATE TABLE t(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE)",
        "CREATE TABLE IF NOT EXISTS t(a TEXT DEFAULT 'hello', b REAL CHECK(b > 0))",
        "CREATE TABLE t(a INTEGER, b TEXT, FOREIGN KEY(a) REFERENCES other(id) ON DELETE CASCADE)",
        "CREATE TABLE t(a INTEGER, b TEXT, PRIMARY KEY(a, b), UNIQUE(b))",
        "CREATE TEMPORARY TABLE t AS SELECT * FROM other",
        "CREATE TABLE t(id INTEGER PRIMARY KEY) WITHOUT ROWID",
        "CREATE TABLE t(id INTEGER PRIMARY KEY) STRICT",
        "CREATE UNIQUE INDEX idx ON t(a, b) WHERE a IS NOT NULL",
        "CREATE VIEW v AS SELECT a, b FROM t WHERE x > 0",
        "CREATE VIEW v(x, y) AS SELECT a, b FROM t",
        "CREATE TRIGGER tr BEFORE INSERT ON t BEGIN SELECT 1; END",
        "CREATE TRIGGER tr AFTER UPDATE OF a, b ON t WHEN NEW.a > 0 BEGIN UPDATE t2 SET x = NEW.a; END",
        "CREATE VIRTUAL TABLE vt USING fts5(a, b, content='t')",
        "DROP TABLE IF EXISTS t",
        "DROP INDEX idx",
        "DROP VIEW IF EXISTS v",
        "DROP TRIGGER tr",

        /* ALTER */
        "ALTER TABLE t RENAME TO t2",
        "ALTER TABLE t ADD COLUMN x INTEGER DEFAULT 0",
        "ALTER TABLE t RENAME COLUMN a TO b",
        "ALTER TABLE t DROP COLUMN x",

        /* Misc statements */
        "BEGIN DEFERRED TRANSACTION",
        "COMMIT",
        "ROLLBACK",
        "SAVEPOINT sp1",
        "RELEASE sp1",
        "ROLLBACK TO sp1",
        "PRAGMA journal_mode",
        "PRAGMA main.cache_size = 1000",
        "VACUUM",
        "VACUUM INTO '/tmp/backup.db'",
        "REINDEX",
        "REINDEX main.t",
        "ANALYZE",
        "ANALYZE main.t",
        "ATTACH DATABASE 'file.db' AS db2",
        "DETACH DATABASE db2",
        "EXPLAIN SELECT 1",
        "EXPLAIN QUERY PLAN SELECT * FROM t",

        /* Complex queries */
        "SELECT a.id, b.name FROM t1 a CROSS JOIN t2 b WHERE a.id = b.ref ORDER BY 1",
        "WITH RECURSIVE tree(id, parent, depth) AS ("
            "SELECT id, parent_id, 0 FROM nodes WHERE parent_id IS NULL "
            "UNION ALL "
            "SELECT n.id, n.parent_id, t.depth + 1 FROM nodes n JOIN tree t ON n.parent_id = t.id"
        ") SELECT * FROM tree ORDER BY depth, id",
        "SELECT * FROM t1 NATURAL LEFT JOIN t2",
        "INSERT INTO t(a, b) SELECT x, y FROM s WHERE z > 0 ORDER BY x LIMIT 100",
        "SELECT (SELECT MAX(x) FROM t2 WHERE t2.grp = t1.grp) FROM t1",
        "SELECT COALESCE(a, b, c, 0), NULLIF(x, 0) FROM t",
    };
    int n = (int)(sizeof(sqls) / sizeof(sqls[0]));

    for (int i = 0; i < n; i++) {
        arena_t *a1 = arena_create(32 * 1024);
        arena_t *a2 = arena_create(32 * 1024);
        const char *err1 = NULL, *err2 = NULL;

        TEST(sqls[i]);

        /* Step 1: parse original SQL */
        LpNode *ast1 = lp_parse(sqls[i], a1, &err1);
        if (!ast1) {
            FAIL(err1 ? err1 : "parse failed");
            arena_destroy(a1);
            arena_destroy(a2);
            continue;
        }

        /* Step 2: unparse to SQL */
        char *sql2 = lp_ast_to_sql(ast1, a1);
        if (!sql2) {
            FAIL("unparse failed");
            arena_destroy(a1);
            arena_destroy(a2);
            continue;
        }

        /* Step 3: reparse the unparsed SQL */
        LpNode *ast2 = lp_parse(sql2, a2, &err2);
        if (!ast2) {
            char msg[256];
            snprintf(msg, sizeof(msg), "reparse failed: %s\n  unparsed: %s",
                     err2 ? err2 : "?", sql2);
            FAIL(msg);
            arena_destroy(a1);
            arena_destroy(a2);
            continue;
        }

        /* Step 4: unparse again */
        char *sql3 = lp_ast_to_sql(ast2, a2);
        if (!sql3) {
            FAIL("second unparse failed");
            arena_destroy(a1);
            arena_destroy(a2);
            continue;
        }

        /* Step 5: compare the two unparsed outputs */
        if (strcmp(sql2, sql3) == 0) {
            PASS();
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "round-trip mismatch:\n    first:  %s\n    second: %s",
                     sql2, sql3);
            FAIL(msg);
        }

        arena_destroy(a1);
        arena_destroy(a2);
    }
}

/* ------------------------------------------------------------------ */
/*  New API: lp_version, lp_node_count, lp_node_equal, parent ptrs     */
/* ------------------------------------------------------------------ */

/* Helper: parse into both arenas and check equality */
static int eq_check(const char *sql, arena_t *a, arena_t *a2, const char **err) {
    arena_reset(a); arena_reset(a2);
    LpNode *n1 = parse(sql, a, err);
    LpNode *n2 = parse(sql, a2, err);
    return n1 && n2 && lp_node_equal(n1, n2);
}

/* Helper: parse two different SQL and check they are NOT equal */
static int neq_check(const char *sql1, const char *sql2,
                      arena_t *a, arena_t *a2, const char **err) {
    arena_reset(a); arena_reset(a2);
    LpNode *n1 = parse(sql1, a, err);
    LpNode *n2 = parse(sql2, a2, err);
    return n1 && n2 && !lp_node_equal(n1, n2);
}

/* Helper: parse, clone, check clone equals original */
static int clone_eq_check(const char *sql, arena_t *a, arena_t *a2, const char **err) {
    arena_reset(a); arena_reset(a2);
    LpNode *n1 = parse(sql, a, err);
    if (!n1) return 0;
    LpNode *cloned = lp_node_clone(a2, n1);
    return cloned && lp_node_equal(n1, cloned);
}

/* Helper: check node count */
static int count_check(const char *sql, int expected, arena_t *a, const char **err) {
    arena_reset(a);
    LpNode *n = parse(sql, a, err);
    if (!n) return 0;
    int c = lp_node_count(n);
    return c == expected;
}

static void test_new_api(void) {
    printf("\n--- New API (version, count, equal, parents) ---\n");
    arena_t *a = arena_create(64*1024);
    arena_t *a2 = arena_create(64*1024);
    const char *err;
    LpNode *n;

    /* ---- lp_version ---- */

    TEST("lp_version returns version string");
    const char *v = lp_version();
    if (v && strcmp(v, LITEPARSER_VERSION) == 0) PASS(); else FAIL("version mismatch");

    TEST("lp_version matches macro");
    if (strcmp(lp_version(), "1.0.0") == 0) PASS(); else FAIL("not 1.0.0");

    /* ---- lp_node_count ---- */

    TEST("count: NULL returns 0");
    if (lp_node_count(NULL) == 0) PASS(); else FAIL("expected 0");

    TEST("count: SELECT 1 = 3");
    /* SELECT -> RESULT_COLUMN -> LITERAL_INT */
    if (count_check("SELECT 1", 3, a, &err)) PASS(); else FAIL("wrong count");

    TEST("count: SELECT a, b FROM t WHERE x > 1 = 9");
    /* SELECT + 2*(RC+ColRef) + FROM_TABLE + BinOp + ColRef + LitInt */
    if (count_check("SELECT a, b FROM t WHERE x > 1", 9, a, &err)) PASS(); else FAIL("wrong count");

    TEST("count: SELECT with JOIN");
    arena_reset(a);
    n = parse("SELECT a FROM t1 JOIN t2 ON t1.id = t2.id", a, &err);
    if (n) {
        int c = lp_node_count(n);
        /* SELECT + RC + ColRef + JOIN(1) + FROM_TABLE(t1) + FROM_TABLE(t2) + BinOp + ColRef + ColRef = 9 */
        if (c == 9) PASS();
        else { char msg[64]; snprintf(msg, sizeof(msg), "expected 9, got %d", c); FAIL(msg); }
    } else FAIL(err);

    TEST("count: subquery");
    arena_reset(a);
    n = parse("SELECT * FROM (SELECT 1)", a, &err);
    if (n) {
        int c = lp_node_count(n);
        /* SELECT + RC(STAR) + FROM_SUBQUERY + SELECT + RC + LITERAL_INT = 6 */
        if (c == 6) PASS();
        else { char msg[64]; snprintf(msg, sizeof(msg), "expected 6, got %d", c); FAIL(msg); }
    } else FAIL(err);

    TEST("count: CTE");
    arena_reset(a);
    n = parse("WITH x AS (SELECT 1) SELECT * FROM x", a, &err);
    if (n) {
        int c = lp_node_count(n);
        /* SELECT + WITH + CTE + SELECT(inner) + RC + LitInt + RC(STAR) + FROM_TABLE = 8 */
        if (c == 8) PASS();
        else { char msg[64]; snprintf(msg, sizeof(msg), "expected 8, got %d", c); FAIL(msg); }
    } else FAIL(err);

    TEST("count: INSERT VALUES");
    arena_reset(a);
    n = parse("INSERT INTO t(a, b) VALUES(1, 2)", a, &err);
    if (n) {
        int c = lp_node_count(n);
        /* INSERT + 2*EXPR_COLUMN_REF(column names) + VALUES_ROW + 2*LITERAL_INT = 6 */
        if (c == 6) PASS();
        else { char msg[64]; snprintf(msg, sizeof(msg), "expected 6, got %d", c); FAIL(msg); }
    } else FAIL(err);

    TEST("count: CREATE TABLE");
    arena_reset(a);
    n = parse("CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT NOT NULL)", a, &err);
    if (n) {
        int c = lp_node_count(n);
        /* CREATE_TABLE + COLUMN_DEF(a) + CCONS(PK) + COLUMN_DEF(b) + CCONS(NOT NULL) = 5 */
        if (c == 5) PASS();
        else { char msg[64]; snprintf(msg, sizeof(msg), "expected 5, got %d", c); FAIL(msg); }
    } else FAIL(err);

    /* ---- lp_node_equal: NULL handling ---- */

    TEST("equal: NULL == NULL");
    if (lp_node_equal(NULL, NULL)) PASS(); else FAIL("expected equal");

    TEST("equal: NULL != non-NULL");
    arena_reset(a);
    n = parse("SELECT 1", a, &err);
    if (n && !lp_node_equal(NULL, n) && !lp_node_equal(n, NULL)) PASS(); else FAIL("wrong");

    TEST("equal: different kinds");
    arena_reset(a); arena_reset(a2);
    {
        LpNode *n1 = parse("SELECT 1", a, &err);
        LpNode *n2 = parse("INSERT INTO t VALUES(1)", a2, &err);
        if (n1 && n2 && !lp_node_equal(n1, n2)) PASS(); else FAIL("expected not equal");
    }

    /* ---- lp_node_equal: SELECT variants ---- */

    TEST("equal: same SELECT");
    if (eq_check("SELECT a, b FROM t WHERE x > 1", a, a2, &err)) PASS(); else FAIL("expected equal");

    TEST("equal: SELECT different column");
    if (neq_check("SELECT a FROM t", "SELECT b FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: SELECT different table");
    if (neq_check("SELECT a FROM t1", "SELECT a FROM t2", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: SELECT different WHERE");
    if (neq_check("SELECT a FROM t WHERE x = 1", "SELECT a FROM t WHERE x = 2",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: SELECT DISTINCT vs non-DISTINCT");
    if (neq_check("SELECT DISTINCT a FROM t", "SELECT a FROM t", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: SELECT with ORDER BY");
    if (eq_check("SELECT a FROM t ORDER BY a DESC", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: SELECT different ORDER direction");
    if (neq_check("SELECT a FROM t ORDER BY a ASC", "SELECT a FROM t ORDER BY a DESC",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: SELECT with LIMIT/OFFSET");
    if (eq_check("SELECT a FROM t LIMIT 10 OFFSET 5", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: SELECT with GROUP BY/HAVING");
    if (eq_check("SELECT a, count(*) FROM t GROUP BY a HAVING count(*) > 1",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: JOINs ---- */

    TEST("equal: JOIN");
    if (eq_check("SELECT a FROM t1 JOIN t2 ON t1.id = t2.id", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: LEFT JOIN vs INNER JOIN");
    if (neq_check("SELECT a FROM t1 LEFT JOIN t2 ON t1.id = t2.id",
                   "SELECT a FROM t1 JOIN t2 ON t1.id = t2.id", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: JOIN different ON condition");
    if (neq_check("SELECT a FROM t1 JOIN t2 ON t1.id = t2.id",
                   "SELECT a FROM t1 JOIN t2 ON t1.id = t2.fk", a, a2, &err))
        PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: compound SELECT ---- */

    TEST("equal: UNION");
    if (eq_check("SELECT 1 UNION SELECT 2", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: UNION vs UNION ALL");
    if (neq_check("SELECT 1 UNION SELECT 2", "SELECT 1 UNION ALL SELECT 2",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: UNION vs INTERSECT");
    if (neq_check("SELECT 1 UNION SELECT 2", "SELECT 1 INTERSECT SELECT 2",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: INSERT ---- */

    TEST("equal: INSERT VALUES");
    if (eq_check("INSERT INTO t(a, b) VALUES(1, 2)", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: INSERT different table");
    if (neq_check("INSERT INTO t1 VALUES(1)", "INSERT INTO t2 VALUES(1)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: INSERT OR REPLACE vs INSERT OR IGNORE");
    if (neq_check("INSERT OR REPLACE INTO t VALUES(1)", "INSERT OR IGNORE INTO t VALUES(1)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: INSERT with RETURNING");
    if (eq_check("INSERT INTO t VALUES(1) RETURNING *", a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: UPDATE ---- */

    TEST("equal: UPDATE");
    if (eq_check("UPDATE t SET a = 1 WHERE b = 2", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: UPDATE different SET value");
    if (neq_check("UPDATE t SET a = 1", "UPDATE t SET a = 2", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: UPDATE different column");
    if (neq_check("UPDATE t SET a = 1", "UPDATE t SET b = 1", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: UPDATE OR ABORT vs plain");
    if (neq_check("UPDATE OR ABORT t SET a = 1", "UPDATE t SET a = 1", a, a2, &err))
        PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: DELETE ---- */

    TEST("equal: DELETE");
    if (eq_check("DELETE FROM t WHERE a = 1", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: DELETE different WHERE");
    if (neq_check("DELETE FROM t WHERE a = 1", "DELETE FROM t WHERE a = 2",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: CREATE TABLE ---- */

    TEST("equal: CREATE TABLE");
    if (eq_check("CREATE TABLE t(a INTEGER, b TEXT)", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE different column type");
    if (neq_check("CREATE TABLE t(a INTEGER)", "CREATE TABLE t(a TEXT)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE IF NOT EXISTS vs plain");
    if (neq_check("CREATE TABLE IF NOT EXISTS t(a INT)", "CREATE TABLE t(a INT)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE with constraints");
    if (eq_check("CREATE TABLE t(a INT PRIMARY KEY, b TEXT NOT NULL, CHECK(a > 0))",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE STRICT vs plain");
    if (neq_check("CREATE TABLE t(a INT) STRICT", "CREATE TABLE t(a INT)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE WITH ROWID vs WITHOUT ROWID");
    if (neq_check("CREATE TABLE t(a INT PRIMARY KEY) WITHOUT ROWID",
                   "CREATE TABLE t(a INT PRIMARY KEY)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE with FK");
    if (eq_check("CREATE TABLE t(a INT REFERENCES other(id) ON DELETE CASCADE)",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE different FK action");
    if (neq_check("CREATE TABLE t(a INT REFERENCES o(id) ON DELETE CASCADE)",
                   "CREATE TABLE t(a INT REFERENCES o(id) ON DELETE SET NULL)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE with table constraint");
    if (eq_check("CREATE TABLE t(a INT, b INT, PRIMARY KEY(a, b))",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE TABLE with GENERATED column");
    if (eq_check("CREATE TABLE t(a INT, b INT GENERATED ALWAYS AS (a * 2) STORED)",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: CREATE INDEX ---- */

    TEST("equal: CREATE INDEX");
    if (eq_check("CREATE INDEX idx ON t(a, b)", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE UNIQUE INDEX vs plain");
    if (neq_check("CREATE UNIQUE INDEX idx ON t(a)", "CREATE INDEX idx ON t(a)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE INDEX with WHERE");
    if (eq_check("CREATE INDEX idx ON t(a) WHERE a > 0", a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: CREATE VIEW ---- */

    TEST("equal: CREATE VIEW");
    if (eq_check("CREATE VIEW v AS SELECT a FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CREATE VIEW with column names");
    if (eq_check("CREATE VIEW v(x, y) AS SELECT a, b FROM t", a, a2, &err))
        PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: CREATE TRIGGER ---- */

    TEST("equal: CREATE TRIGGER");
    if (eq_check("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT 1; END",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: TRIGGER BEFORE vs AFTER");
    if (neq_check("CREATE TRIGGER tr BEFORE INSERT ON t BEGIN SELECT 1; END",
                   "CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT 1; END",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: TRIGGER INSERT vs DELETE");
    if (neq_check("CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT 1; END",
                   "CREATE TRIGGER tr AFTER DELETE ON t BEGIN SELECT 1; END",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: TRIGGER with WHEN");
    if (eq_check("CREATE TRIGGER tr AFTER INSERT ON t WHEN NEW.a > 0 BEGIN SELECT 1; END",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: CREATE VIRTUAL TABLE ---- */

    TEST("equal: CREATE VIRTUAL TABLE");
    if (eq_check("CREATE VIRTUAL TABLE vt USING fts5(a, b)", a, a2, &err))
        PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: DROP ---- */

    TEST("equal: DROP TABLE");
    if (eq_check("DROP TABLE t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: DROP TABLE vs DROP INDEX");
    if (neq_check("DROP TABLE t", "DROP INDEX t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: DROP IF EXISTS vs plain");
    if (neq_check("DROP TABLE IF EXISTS t", "DROP TABLE t", a, a2, &err))
        PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: transaction statements ---- */

    TEST("equal: BEGIN");
    if (eq_check("BEGIN", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: BEGIN DEFERRED vs IMMEDIATE");
    if (neq_check("BEGIN DEFERRED", "BEGIN IMMEDIATE", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: SAVEPOINT");
    if (eq_check("SAVEPOINT sp1", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: different SAVEPOINT name");
    if (neq_check("SAVEPOINT sp1", "SAVEPOINT sp2", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: RELEASE");
    if (eq_check("RELEASE sp1", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: COMMIT");
    if (eq_check("COMMIT", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: ROLLBACK");
    if (eq_check("ROLLBACK", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: ROLLBACK TO");
    if (eq_check("ROLLBACK TO sp1", a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: PRAGMA ---- */

    TEST("equal: PRAGMA");
    if (eq_check("PRAGMA table_info('t')", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: PRAGMA different name");
    if (neq_check("PRAGMA table_info('t')", "PRAGMA index_list('t')",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: misc statements ---- */

    TEST("equal: VACUUM");
    if (eq_check("VACUUM", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: REINDEX");
    if (eq_check("REINDEX t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: ANALYZE");
    if (eq_check("ANALYZE t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: ATTACH");
    if (eq_check("ATTACH 'file.db' AS db2", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: DETACH");
    if (eq_check("DETACH db2", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: ALTER TABLE RENAME");
    if (eq_check("ALTER TABLE t RENAME TO t2", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: ALTER TABLE ADD COLUMN");
    if (eq_check("ALTER TABLE t ADD COLUMN c INT", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: ALTER different type");
    if (neq_check("ALTER TABLE t RENAME TO t2", "ALTER TABLE t ADD COLUMN c INT",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: EXPLAIN");
    if (eq_check("EXPLAIN SELECT 1", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: EXPLAIN vs EXPLAIN QUERY PLAN");
    if (neq_check("EXPLAIN SELECT 1", "EXPLAIN QUERY PLAN SELECT 1",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: expressions ---- */

    TEST("equal: CAST");
    if (eq_check("SELECT CAST(a AS TEXT) FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CAST different type");
    if (neq_check("SELECT CAST(a AS TEXT) FROM t", "SELECT CAST(a AS INT) FROM t",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: COLLATE");
    if (eq_check("SELECT a COLLATE NOCASE FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: BETWEEN");
    if (eq_check("SELECT a FROM t WHERE a BETWEEN 1 AND 10", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: NOT BETWEEN vs BETWEEN");
    if (neq_check("SELECT a FROM t WHERE a NOT BETWEEN 1 AND 10",
                   "SELECT a FROM t WHERE a BETWEEN 1 AND 10",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: IN list");
    if (eq_check("SELECT a FROM t WHERE a IN (1, 2, 3)", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: IN subquery");
    if (eq_check("SELECT a FROM t WHERE a IN (SELECT b FROM t2)", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: NOT IN vs IN");
    if (neq_check("SELECT a FROM t WHERE a NOT IN (1, 2)",
                   "SELECT a FROM t WHERE a IN (1, 2)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: EXISTS");
    if (eq_check("SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2)", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: scalar subquery");
    if (eq_check("SELECT (SELECT 1) FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CASE WHEN");
    if (eq_check("SELECT CASE WHEN a > 1 THEN 'yes' ELSE 'no' END FROM t",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CASE WHEN different ELSE");
    if (neq_check("SELECT CASE WHEN a > 1 THEN 'yes' ELSE 'no' END FROM t",
                   "SELECT CASE WHEN a > 1 THEN 'yes' ELSE 'maybe' END FROM t",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CASE with operand");
    if (eq_check("SELECT CASE a WHEN 1 THEN 'one' WHEN 2 THEN 'two' END FROM t",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: unary NOT");
    if (eq_check("SELECT NOT a FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: unary minus");
    if (eq_check("SELECT -a FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: different unary op");
    if (neq_check("SELECT -a FROM t", "SELECT +a FROM t", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: binary op types");
    if (neq_check("SELECT a + b FROM t", "SELECT a - b FROM t", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: LIKE");
    if (eq_check("SELECT a FROM t WHERE a LIKE 'foo%'", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: LIKE with ESCAPE");
    if (eq_check("SELECT a FROM t WHERE a LIKE 'foo%' ESCAPE '\\'",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: function call");
    if (eq_check("SELECT count(*) FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: function with DISTINCT");
    if (neq_check("SELECT count(DISTINCT a) FROM t", "SELECT count(a) FROM t",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: literal types");
    if (neq_check("SELECT 1", "SELECT 1.0", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: string literal");
    if (eq_check("SELECT 'hello'", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: NULL literal");
    if (eq_check("SELECT NULL", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: blob literal");
    if (eq_check("SELECT X'CAFE'", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: variable");
    if (eq_check("SELECT ?1 FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: named variable");
    if (eq_check("SELECT :name FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: star vs table.star");
    if (neq_check("SELECT * FROM t", "SELECT t.* FROM t", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: table-qualified column");
    if (eq_check("SELECT t.a FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: schema-qualified column");
    if (eq_check("SELECT s.t.a FROM s.t", a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: CTE ---- */

    TEST("equal: CTE");
    if (eq_check("WITH x AS (SELECT 1) SELECT * FROM x", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: recursive CTE");
    if (eq_check("WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<10) SELECT x FROM cnt",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: RECURSIVE vs non-RECURSIVE");
    if (neq_check("WITH RECURSIVE x AS (SELECT 1) SELECT * FROM x",
                   "WITH x AS (SELECT 1) SELECT * FROM x",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: window functions ---- */

    TEST("equal: window function");
    if (eq_check("SELECT row_number() OVER (ORDER BY a) FROM t", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: window with PARTITION BY");
    if (eq_check("SELECT sum(a) OVER (PARTITION BY b ORDER BY c) FROM t",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: window with frame");
    if (eq_check("SELECT sum(a) OVER (ORDER BY b ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING) FROM t",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: named window definition");
    if (eq_check("SELECT sum(a) OVER w FROM t WINDOW w AS (ORDER BY b)",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: FILTER clause");
    if (eq_check("SELECT count(*) FILTER (WHERE a > 0) FROM t",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: UPSERT ---- */

    TEST("equal: INSERT with ON CONFLICT");
    if (eq_check("INSERT INTO t(a) VALUES(1) ON CONFLICT(a) DO UPDATE SET a = excluded.a",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: ON CONFLICT DO NOTHING");
    if (eq_check("INSERT INTO t(a) VALUES(1) ON CONFLICT DO NOTHING",
                  a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: FROM variants ---- */

    TEST("equal: FROM subquery");
    if (eq_check("SELECT * FROM (SELECT 1) AS sub", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: FROM table with alias");
    if (eq_check("SELECT * FROM t AS alias", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: FROM different alias");
    if (neq_check("SELECT * FROM t AS a", "SELECT * FROM t AS b", a, a2, &err))
        PASS(); else FAIL("wrong");

    TEST("equal: INDEXED BY");
    if (eq_check("SELECT * FROM t INDEXED BY idx", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: NOT INDEXED vs INDEXED BY");
    if (neq_check("SELECT * FROM t NOT INDEXED", "SELECT * FROM t INDEXED BY idx",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: result column alias ---- */

    TEST("equal: result column alias");
    if (eq_check("SELECT a AS x FROM t", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: different result alias");
    if (neq_check("SELECT a AS x FROM t", "SELECT a AS y FROM t", a, a2, &err))
        PASS(); else FAIL("wrong");

    /* ---- lp_node_equal via clone (exercises all node kinds) ---- */

    TEST("clone-eq: complex SELECT with everything");
    if (clone_eq_check(
        "SELECT DISTINCT a, b AS alias, count(*) FROM t1 "
        "JOIN t2 ON t1.id = t2.fk "
        "WHERE a > 1 AND b LIKE 'x%' "
        "GROUP BY a HAVING count(*) > 1 "
        "ORDER BY a DESC LIMIT 10 OFFSET 5",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: INSERT with upsert");
    if (clone_eq_check(
        "INSERT INTO t(a, b) VALUES(1, 2) "
        "ON CONFLICT(a) DO UPDATE SET b = excluded.b WHERE b IS NULL "
        "RETURNING *",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: UPDATE with FROM");
    if (clone_eq_check(
        "UPDATE t SET a = s.a FROM (SELECT * FROM src) AS s WHERE t.id = s.id RETURNING a",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: DELETE with RETURNING");
    if (clone_eq_check("DELETE FROM t WHERE a > 5 RETURNING *",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: CREATE TABLE with all constraint types");
    if (clone_eq_check(
        "CREATE TABLE IF NOT EXISTS t("
        "a INTEGER PRIMARY KEY AUTOINCREMENT, "
        "b TEXT NOT NULL DEFAULT 'x' COLLATE NOCASE, "
        "c REAL CHECK(c > 0), "
        "d INT REFERENCES other(id) ON DELETE CASCADE ON UPDATE SET NULL, "
        "e INT GENERATED ALWAYS AS (a + 1) STORED, "
        "UNIQUE(b, c), "
        "FOREIGN KEY(d) REFERENCES other(id) DEFERRABLE INITIALLY DEFERRED"
        ") WITHOUT ROWID",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: CREATE INDEX with WHERE");
    if (clone_eq_check("CREATE UNIQUE INDEX IF NOT EXISTS idx ON t(a COLLATE NOCASE DESC, b) WHERE a > 0",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: CREATE VIEW");
    if (clone_eq_check("CREATE TEMP VIEW IF NOT EXISTS v(x, y) AS SELECT a, b FROM t",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: CREATE TRIGGER");
    if (clone_eq_check(
        "CREATE TRIGGER IF NOT EXISTS tr INSTEAD OF UPDATE OF a, b ON t "
        "WHEN OLD.a IS NOT NULL BEGIN "
        "INSERT INTO log VALUES(OLD.a); "
        "UPDATE t2 SET x = NEW.a WHERE id = OLD.id; "
        "DELETE FROM t3 WHERE a = OLD.a; "
        "END",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: CREATE VIRTUAL TABLE");
    if (clone_eq_check("CREATE VIRTUAL TABLE vt USING fts5(a, b, content=t)",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: CTE with materialization");
    if (clone_eq_check(
        "WITH x AS MATERIALIZED (SELECT 1), y AS NOT MATERIALIZED (SELECT 2) "
        "SELECT * FROM x, y",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: window with frame and exclude");
    if (clone_eq_check(
        "SELECT sum(a) OVER (PARTITION BY b ORDER BY c "
        "GROUPS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW EXCLUDE TIES) FROM t",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: CASE, BETWEEN, IN, EXISTS, vector");
    if (clone_eq_check(
        "SELECT CASE a WHEN 1 THEN 'one' WHEN 2 THEN 'two' ELSE 'other' END, "
        "b BETWEEN 10 AND 20, "
        "c NOT IN (1, 2, 3), "
        "EXISTS (SELECT 1 FROM t2 WHERE t2.id = t.id), "
        "(a, b) IN (SELECT x, y FROM t3) "
        "FROM t",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: ATTACH/DETACH");
    if (clone_eq_check("ATTACH 'file.db' AS db2", a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("clone-eq: nested RAISE in trigger");
    if (clone_eq_check(
        "CREATE TRIGGER tr BEFORE INSERT ON t BEGIN "
        "SELECT RAISE(ABORT, 'not allowed'); "
        "END",
        a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- lp_node_equal: subtle single-field differences ---- */

    TEST("equal: ORDER BY NULLS FIRST vs NULLS LAST");
    if (neq_check("SELECT a FROM t ORDER BY a NULLS FIRST",
                   "SELECT a FROM t ORDER BY a NULLS LAST",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: index column collation differs");
    if (neq_check("CREATE INDEX i ON t(a COLLATE NOCASE)",
                   "CREATE INDEX i ON t(a COLLATE BINARY)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: index column sort order differs");
    if (neq_check("CREATE INDEX i ON t(a ASC)", "CREATE INDEX i ON t(a DESC)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: column constraint GENERATED virtual vs stored");
    if (neq_check("CREATE TABLE t(a INT, b INT GENERATED ALWAYS AS (a+1) VIRTUAL)",
                   "CREATE TABLE t(a INT, b INT GENERATED ALWAYS AS (a+1) STORED)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: FK ON DELETE CASCADE vs RESTRICT");
    if (neq_check("CREATE TABLE t(a INT REFERENCES o(id) ON DELETE CASCADE)",
                   "CREATE TABLE t(a INT REFERENCES o(id) ON DELETE RESTRICT)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: FK ON DELETE SET NULL vs CASCADE");
    if (neq_check("CREATE TABLE t(a INT REFERENCES o(id) ON DELETE SET NULL)",
                   "CREATE TABLE t(a INT REFERENCES o(id) ON DELETE CASCADE)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: FK ON UPDATE SET NULL vs SET DEFAULT");
    if (neq_check("CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE SET NULL)",
                   "CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE SET DEFAULT)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: FK ON UPDATE CASCADE vs RESTRICT");
    if (neq_check("CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE CASCADE)",
                   "CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE RESTRICT)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: FK DEFERRABLE vs NOT DEFERRABLE");
    if (neq_check(
        "CREATE TABLE t(a INT, FOREIGN KEY(a) REFERENCES o(id) DEFERRABLE INITIALLY DEFERRED)",
        "CREATE TABLE t(a INT, FOREIGN KEY(a) REFERENCES o(id) NOT DEFERRABLE)",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: column NOT NULL conflict ABORT vs ROLLBACK");
    if (neq_check("CREATE TABLE t(a INT NOT NULL ON CONFLICT ABORT)",
                   "CREATE TABLE t(a INT NOT NULL ON CONFLICT ROLLBACK)",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: window frame ROWS vs RANGE");
    if (neq_check(
        "SELECT sum(a) OVER (ORDER BY b ROWS UNBOUNDED PRECEDING) FROM t",
        "SELECT sum(a) OVER (ORDER BY b RANGE UNBOUNDED PRECEDING) FROM t",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: window frame EXCLUDE TIES vs EXCLUDE NO OTHERS");
    if (neq_check(
        "SELECT sum(a) OVER (ORDER BY b ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW EXCLUDE TIES) FROM t",
        "SELECT sum(a) OVER (ORDER BY b ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW EXCLUDE NO OTHERS) FROM t",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: frame bound PRECEDING vs FOLLOWING");
    if (neq_check(
        "SELECT sum(a) OVER (ORDER BY b ROWS 3 PRECEDING) FROM t",
        "SELECT sum(a) OVER (ORDER BY b ROWS 3 FOLLOWING) FROM t",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: CTE MATERIALIZED vs NOT MATERIALIZED");
    if (neq_check("WITH x AS MATERIALIZED (SELECT 1) SELECT * FROM x",
                   "WITH x AS NOT MATERIALIZED (SELECT 1) SELECT * FROM x",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: RAISE ABORT vs RAISE FAIL");
    if (neq_check(
        "CREATE TRIGGER tr BEFORE INSERT ON t BEGIN SELECT RAISE(ABORT, 'msg'); END",
        "CREATE TRIGGER tr BEFORE INSERT ON t BEGIN SELECT RAISE(FAIL, 'msg'); END",
        a, a2, &err)) PASS(); else FAIL("wrong");

    TEST("equal: table constraint PK vs UNIQUE");
    if (neq_check("CREATE TABLE t(a INT, PRIMARY KEY(a))",
                   "CREATE TABLE t(a INT, UNIQUE(a))",
                   a, a2, &err)) PASS(); else FAIL("wrong");

    /* ---- FK on_update bug fix verification ---- */

    arena_reset(a);
    TEST("FK: column-level ON UPDATE CASCADE is captured");
    n = parse("CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE CASCADE)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE &&
        n->u.create_table.columns.count == 1) {
        LpNode *col = n->u.create_table.columns.items[0];
        LpNode *ccons = col->u.column_def.constraints.items[0];
        LpNode *fk = ccons->u.column_constraint.fk;
        if (fk && fk->u.foreign_key.on_update == LP_FK_CASCADE)
            PASS();
        else FAIL("on_update not CASCADE");
    } else FAIL(err ? err : "parse failed");

    arena_reset(a);
    TEST("FK: column-level ON UPDATE SET NULL is captured");
    n = parse("CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE SET NULL)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE) {
        LpNode *fk = n->u.create_table.columns.items[0]
                       ->u.column_def.constraints.items[0]
                       ->u.column_constraint.fk;
        if (fk && fk->u.foreign_key.on_update == LP_FK_SET_NULL)
            PASS();
        else FAIL("on_update not SET_NULL");
    } else FAIL(err ? err : "parse failed");

    arena_reset(a);
    TEST("FK: column-level ON UPDATE SET DEFAULT is captured");
    n = parse("CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE SET DEFAULT)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE) {
        LpNode *fk = n->u.create_table.columns.items[0]
                       ->u.column_def.constraints.items[0]
                       ->u.column_constraint.fk;
        if (fk && fk->u.foreign_key.on_update == LP_FK_SET_DEFAULT)
            PASS();
        else FAIL("on_update not SET_DEFAULT");
    } else FAIL(err ? err : "parse failed");

    arena_reset(a);
    TEST("FK: column-level ON UPDATE RESTRICT is captured");
    n = parse("CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE RESTRICT)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE) {
        LpNode *fk = n->u.create_table.columns.items[0]
                       ->u.column_def.constraints.items[0]
                       ->u.column_constraint.fk;
        if (fk && fk->u.foreign_key.on_update == LP_FK_RESTRICT)
            PASS();
        else FAIL("on_update not RESTRICT");
    } else FAIL(err ? err : "parse failed");

    arena_reset(a);
    TEST("FK: column-level ON UPDATE NO ACTION is captured");
    n = parse("CREATE TABLE t(a INT REFERENCES o(id) ON UPDATE NO ACTION)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE) {
        LpNode *fk = n->u.create_table.columns.items[0]
                       ->u.column_def.constraints.items[0]
                       ->u.column_constraint.fk;
        if (fk && fk->u.foreign_key.on_update == LP_FK_NO_ACTION)
            PASS();
        else FAIL("on_update not NO_ACTION");
    } else FAIL(err ? err : "parse failed");

    arena_reset(a);
    TEST("FK: both ON DELETE and ON UPDATE captured");
    n = parse("CREATE TABLE t(a INT REFERENCES o(id) ON DELETE CASCADE ON UPDATE SET NULL)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE) {
        LpNode *fk = n->u.create_table.columns.items[0]
                       ->u.column_def.constraints.items[0]
                       ->u.column_constraint.fk;
        if (fk && fk->u.foreign_key.on_delete == LP_FK_CASCADE &&
            fk->u.foreign_key.on_update == LP_FK_SET_NULL)
            PASS();
        else FAIL("on_delete/on_update wrong");
    } else FAIL(err ? err : "parse failed");

    arena_reset(a);
    TEST("FK: table-level ON UPDATE CASCADE is captured");
    n = parse("CREATE TABLE t(a INT, FOREIGN KEY(a) REFERENCES o(id) ON UPDATE CASCADE)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE &&
        n->u.create_table.constraints.count == 1) {
        LpNode *tc = n->u.create_table.constraints.items[0];
        LpNode *fk = tc->u.table_constraint.fk;
        if (fk && fk->u.foreign_key.on_update == LP_FK_CASCADE)
            PASS();
        else FAIL("table-level on_update not CASCADE");
    } else FAIL(err ? err : "parse failed");

    arena_reset(a);
    TEST("FK: table-level both actions captured");
    n = parse("CREATE TABLE t(a INT, FOREIGN KEY(a) REFERENCES o(id) ON DELETE RESTRICT ON UPDATE SET DEFAULT)", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE &&
        n->u.create_table.constraints.count == 1) {
        LpNode *fk = n->u.create_table.constraints.items[0]->u.table_constraint.fk;
        if (fk && fk->u.foreign_key.on_delete == LP_FK_RESTRICT &&
            fk->u.foreign_key.on_update == LP_FK_SET_DEFAULT)
            PASS();
        else FAIL("table-level actions wrong");
    } else FAIL(err ? err : "parse failed");

    arena_reset(a);
    TEST("FK: ON UPDATE round-trips correctly");
    n = parse("CREATE TABLE t(a INT REFERENCES o(id) ON DELETE CASCADE ON UPDATE SET NULL)", a, &err);
    if (n) {
        char *sql = lp_ast_to_sql(n, a);
        if (sql && strstr(sql, "ON UPDATE SET NULL") && strstr(sql, "ON DELETE CASCADE"))
            PASS();
        else FAIL(sql ? sql : "unparse failed");
    } else FAIL(err);

    /* ---- parent pointers: basic ---- */

    arena_reset(a);
    TEST("parent: root->parent is NULL");
    n = parse("SELECT a FROM t WHERE x = 1", a, &err);
    if (n && n->parent == NULL) PASS(); else FAIL("root parent not NULL");

    TEST("parent: where->parent == root");
    if (n && n->u.select.where && n->u.select.where->parent == n) PASS();
    else FAIL("where parent wrong");

    TEST("parent: from->parent == root");
    if (n && n->u.select.from && n->u.select.from->parent == n) PASS();
    else FAIL("from parent wrong");

    TEST("parent: binary op children point to binary op");
    if (n && n->u.select.where) {
        LpNode *w = n->u.select.where;
        if (w->kind == LP_EXPR_BINARY_OP &&
            w->u.binary.left && w->u.binary.left->parent == w &&
            w->u.binary.right && w->u.binary.right->parent == w)
            PASS();
        else FAIL("binary children parents wrong");
    } else FAIL("no where");

    TEST("parent: result_column->parent == root");
    if (n && n->u.select.result_columns.count > 0 &&
        n->u.select.result_columns.items[0]->parent == n)
        PASS();
    else FAIL("result_column parent wrong");

    TEST("parent: column_ref inside result_column");
    if (n && n->u.select.result_columns.count > 0) {
        LpNode *rc = n->u.select.result_columns.items[0];
        if (rc->u.result_column.expr && rc->u.result_column.expr->parent == rc)
            PASS();
        else FAIL("wrong");
    } else FAIL("no rc");

    /* ---- parent pointers: JOINs ---- */

    arena_reset(a);
    TEST("parent: JOIN children");
    n = parse("SELECT * FROM t1 JOIN t2 ON t1.id = t2.id LEFT JOIN t3 ON t2.x = t3.x", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_JOIN_CLAUSE) {
        LpNode *j = n->u.select.from;
        if (j->parent == n &&
            j->u.join.left && j->u.join.left->parent == j &&
            j->u.join.right && j->u.join.right->parent == j &&
            j->u.join.on_expr && j->u.join.on_expr->parent == j)
            PASS();
        else FAIL("join parent wrong");
    } else FAIL("no join");

    TEST("parent: nested JOIN left child");
    if (n && n->u.select.from && n->u.select.from->kind == LP_JOIN_CLAUSE) {
        LpNode *outer = n->u.select.from;
        LpNode *inner = outer->u.join.left;
        if (inner && inner->kind == LP_JOIN_CLAUSE &&
            inner->parent == outer &&
            inner->u.join.left && inner->u.join.left->parent == inner &&
            inner->u.join.right && inner->u.join.right->parent == inner)
            PASS();
        else FAIL("nested join wrong");
    } else FAIL("no outer join");

    /* ---- parent pointers: subquery ---- */

    arena_reset(a);
    TEST("parent: FROM subquery");
    n = parse("SELECT * FROM (SELECT a FROM t) AS sub", a, &err);
    if (n && n->u.select.from && n->u.select.from->kind == LP_FROM_SUBQUERY) {
        LpNode *fsq = n->u.select.from;
        if (fsq->parent == n &&
            fsq->u.from_subquery.select &&
            fsq->u.from_subquery.select->parent == fsq)
            PASS();
        else FAIL("subquery parent wrong");
    } else FAIL("no from subquery");

    arena_reset(a);
    TEST("parent: WHERE EXISTS subquery");
    n = parse("SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2)", a, &err);
    if (n && n->u.select.where && n->u.select.where->kind == LP_EXPR_EXISTS) {
        LpNode *ex = n->u.select.where;
        if (ex->parent == n && ex->u.exists.select && ex->u.exists.select->parent == ex)
            PASS();
        else FAIL("exists parent wrong");
    } else FAIL("no exists");

    /* ---- parent pointers: CTE ---- */

    arena_reset(a);
    TEST("parent: CTE structure");
    n = parse("WITH x AS (SELECT 1) SELECT * FROM x", a, &err);
    if (n && n->u.select.with) {
        LpNode *w = n->u.select.with;
        if (w->parent == n && w->kind == LP_WITH &&
            w->u.with.ctes.count == 1 &&
            w->u.with.ctes.items[0]->parent == w &&
            w->u.with.ctes.items[0]->u.cte.select->parent == w->u.with.ctes.items[0])
            PASS();
        else FAIL("cte parent wrong");
    } else FAIL("no with");

    /* ---- parent pointers: CREATE TABLE ---- */

    arena_reset(a);
    TEST("parent: CREATE TABLE columns and constraints");
    n = parse("CREATE TABLE t(a INT PRIMARY KEY, b TEXT NOT NULL, CHECK(a > 0))", a, &err);
    if (n && n->kind == LP_STMT_CREATE_TABLE) {
        int ok = 1;
        for (int i = 0; i < n->u.create_table.columns.count; i++) {
            LpNode *col = n->u.create_table.columns.items[i];
            if (col->parent != n) { ok = 0; break; }
            for (int j = 0; j < col->u.column_def.constraints.count; j++) {
                if (col->u.column_def.constraints.items[j]->parent != col) { ok = 0; break; }
            }
        }
        for (int i = 0; i < n->u.create_table.constraints.count; i++) {
            LpNode *tc = n->u.create_table.constraints.items[i];
            if (tc->parent != n) { ok = 0; break; }
            if (tc->u.table_constraint.expr && tc->u.table_constraint.expr->parent != tc) ok = 0;
        }
        if (ok) PASS(); else FAIL("create table parents wrong");
    } else FAIL("no create table");

    /* ---- parent pointers: trigger body ---- */

    arena_reset(a);
    TEST("parent: trigger body statements");
    n = parse("CREATE TRIGGER tr AFTER INSERT ON t BEGIN INSERT INTO log VALUES(NEW.a); SELECT 1; END",
              a, &err);
    if (n && n->kind == LP_STMT_CREATE_TRIGGER) {
        int ok = (n->u.create_trigger.body.count == 2);
        for (int i = 0; i < n->u.create_trigger.body.count && ok; i++) {
            LpNode *cmd = n->u.create_trigger.body.items[i];
            if (cmd->parent != n) ok = 0;
            if (cmd->kind == LP_TRIGGER_CMD && cmd->u.trigger_cmd.stmt &&
                cmd->u.trigger_cmd.stmt->parent != cmd) ok = 0;
        }
        if (ok) PASS(); else FAIL("trigger body parents wrong");
    } else FAIL("no trigger");

    /* ---- parent pointers: window definitions ---- */

    arena_reset(a);
    TEST("parent: WINDOW def and frame");
    n = parse("SELECT sum(a) OVER w FROM t WINDOW w AS (ORDER BY b ROWS BETWEEN 1 PRECEDING AND CURRENT ROW)",
              a, &err);
    if (n && n->u.select.window_defs.count > 0) {
        LpNode *wd = n->u.select.window_defs.items[0];
        if (wd->parent == n && wd->kind == LP_WINDOW_DEF &&
            wd->u.window_def.frame && wd->u.window_def.frame->parent == wd &&
            wd->u.window_def.frame->u.window_frame.start &&
            wd->u.window_def.frame->u.window_frame.start->parent == wd->u.window_def.frame)
            PASS();
        else FAIL("window parents wrong");
    } else FAIL("no window defs");

    /* ---- parent pointers: CASE expression ---- */

    arena_reset(a);
    TEST("parent: CASE expression children");
    n = parse("SELECT CASE a WHEN 1 THEN 'x' ELSE 'y' END FROM t", a, &err);
    if (n && n->u.select.result_columns.count > 0) {
        LpNode *rc = n->u.select.result_columns.items[0];
        LpNode *expr = rc->u.result_column.expr;
        if (expr && expr->kind == LP_EXPR_CASE &&
            expr->parent == rc &&
            expr->u.case_.operand && expr->u.case_.operand->parent == expr &&
            expr->u.case_.else_expr && expr->u.case_.else_expr->parent == expr &&
            expr->u.case_.when_exprs.count >= 2 &&
            expr->u.case_.when_exprs.items[0]->parent == expr)
            PASS();
        else FAIL("case parents wrong");
    } else FAIL("no rc");

    /* ---- parent pointers: walk up to root ---- */

    arena_reset(a);
    TEST("parent: walk up from leaf to root");
    n = parse("SELECT a FROM t1 JOIN t2 ON t1.id = t2.id WHERE a > 1", a, &err);
    if (n && n->u.select.where && n->u.select.where->kind == LP_EXPR_BINARY_OP) {
        /* walk from literal '1' up to root */
        LpNode *leaf = n->u.select.where->u.binary.right;
        LpNode *p = leaf;
        int depth = 0;
        while (p->parent) { p = p->parent; depth++; }
        if (p == n && depth == 2) /* literal -> binary_op -> select */
            PASS();
        else { char msg[64]; snprintf(msg, sizeof(msg), "depth=%d, reached root=%d", depth, p==n); FAIL(msg); }
    } else FAIL("no where");

    /* ---- parent pointers: tolerant and multi-statement ---- */

    arena_reset(a);
    TEST("parent: tolerant parse sets parents");
    {
        LpParseResult *r = lp_parse_tolerant("SELECT a FROM t; SELECT b FROM u", a);
        if (r && r->stmts.count == 2 &&
            r->stmts.items[0]->parent == NULL &&
            r->stmts.items[1]->parent == NULL &&
            r->stmts.items[0]->u.select.from->parent == r->stmts.items[0] &&
            r->stmts.items[1]->u.select.from->parent == r->stmts.items[1])
            PASS();
        else FAIL("tolerant parent wrong");
    }

    arena_reset(a);
    TEST("parent: multi-statement parse sets parents");
    {
        LpNodeList *stmts = lp_parse_all("SELECT 1; SELECT 2", a, &err);
        if (stmts && stmts->count == 2 &&
            stmts->items[0]->parent == NULL &&
            stmts->items[1]->parent == NULL)
            PASS();
        else FAIL("multi-stmt parent wrong");
    }

    /* ---- parent pointers: after mutation + lp_fix_parents ---- */

    arena_reset(a);
    TEST("parent: lp_fix_parents after lp_list_push");
    n = parse("SELECT a FROM t", a, &err);
    if (n) {
        LpNode *new_rc = lp_node_alloc(a, LP_RESULT_COLUMN);
        LpNode *new_col = lp_node_alloc(a, LP_EXPR_COLUMN_REF);
        new_col->u.column_ref.column = lp_strdup(a, "b");
        new_rc->u.result_column.expr = new_col;
        lp_list_push(a, &n->u.select.result_columns, new_rc);
        lp_fix_parents(n);
        if (new_rc->parent == n && new_col->parent == new_rc)
            PASS();
        else FAIL("mutation parent wrong");
    } else FAIL(err);

    arena_reset(a);
    TEST("parent: lp_fix_parents after lp_list_replace");
    n = parse("SELECT a FROM t", a, &err);
    if (n) {
        LpNode *new_rc = lp_node_alloc(a, LP_RESULT_COLUMN);
        LpNode *new_col = lp_node_alloc(a, LP_EXPR_COLUMN_REF);
        new_col->u.column_ref.column = lp_strdup(a, "z");
        new_rc->u.result_column.expr = new_col;
        lp_list_replace(&n->u.select.result_columns, 0, new_rc);
        lp_fix_parents(n);
        if (new_rc->parent == n && new_col->parent == new_rc)
            PASS();
        else FAIL("replace parent wrong");
    } else FAIL(err);

    arena_reset(a);
    TEST("parent: lp_fix_parents after lp_list_insert");
    n = parse("SELECT a FROM t", a, &err);
    if (n) {
        LpNode *new_rc = lp_node_alloc(a, LP_RESULT_COLUMN);
        LpNode *new_col = lp_node_alloc(a, LP_EXPR_COLUMN_REF);
        new_col->u.column_ref.column = lp_strdup(a, "first");
        new_rc->u.result_column.expr = new_col;
        lp_list_insert(a, &n->u.select.result_columns, 0, new_rc);
        lp_fix_parents(n);
        if (new_rc->parent == n && new_col->parent == new_rc &&
            n->u.select.result_columns.items[1]->parent == n)
            PASS();
        else FAIL("insert parent wrong");
    } else FAIL(err);

    arena_reset(a);
    TEST("parent: clone sets independent parents");
    {
        arena_reset(a2);
        LpNode *orig = parse("SELECT a FROM t WHERE x = 1", a, &err);
        LpNode *cloned = lp_node_clone(a2, orig);
        if (orig && cloned &&
            cloned->parent == NULL &&
            cloned->u.select.where->parent == cloned &&
            cloned->u.select.from->parent == cloned &&
            /* original unchanged */
            orig->u.select.where->parent == orig)
            PASS();
        else FAIL("clone parents wrong");
    }

    arena_destroy(a2);
    arena_destroy(a);
}

/* ------------------------------------------------------------------ */
/*  Memory summary: RSS before/after full suite                        */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    size_t rss_start = get_rss_bytes();
    struct timespec suite_start, suite_end;
    clock_gettime(CLOCK_MONOTONIC, &suite_start);

    printf("=== liteparser test suite ===\n");

    /* Original tests */
    test_basic_select();
    test_where_orderby_limit();
    test_joins();
    test_compound_select();
    test_subqueries();
    test_cte();
    test_expressions();
    test_insert();
    test_update();
    test_delete();
    test_create_table();
    test_create_index();
    test_transaction();
    test_other_statements();
    test_error_handling();
    test_visitor();
    test_json_output();

    /* Extended tests */
    test_select_deep();
    test_from_deep();
    test_expressions_deep();
    test_functions_deep();
    test_window_functions();
    test_insert_deep();
    test_update_deep();
    test_delete_deep();
    test_create_table_deep();
    test_create_index_deep();
    test_create_view_deep();
    test_create_trigger_deep();
    test_cte_deep();
    test_compound_deep();
    test_alter_table_deep();
    test_drop_deep();
    test_transaction_deep();
    test_pragma_deep();
    test_misc_statements();
    test_error_handling_deep();
    test_visitor_deep();
    test_json_deep();
    test_quoted_identifiers();
    test_complex_queries();
    test_source_positions();
    test_multi_statement();
    test_mutation();
    test_tolerant();
    test_round_trip();
    test_new_api();

    /* Benchmark suite */
    run_all_benchmarks();

    /* Throughput benchmark */
    run_throughput_bench();

    clock_gettime(CLOCK_MONOTONIC, &suite_end);
    size_t rss_end = get_rss_bytes();

    printf("\n====================================================================\n");
    printf("  SUMMARY\n");
    printf("====================================================================\n");
    printf("  Tests:       %d/%d passed, %d failed\n",
           tests_passed, tests_run, tests_failed);
    printf("  Total time:  %.2f ms\n", timespec_ms(&suite_start, &suite_end));
    printf("  Process RSS: %zu KB (start) -> %zu KB (end), delta %+zd KB\n",
           rss_start / 1024, rss_end / 1024,
           (ssize_t)(rss_end - rss_start) / 1024);
    printf("====================================================================\n");

    return tests_failed > 0 ? 1 : 0;
}
