/*
** fuzz_parse.c — Fuzz harness for liteparser
**
** Exercises all parser modes, JSON serialization, SQL unparsing,
** AST walking, node counting, equality checking, and cloning.
** Designed for use with libFuzzer (clang -fsanitize=fuzzer) or
** as a standalone harness with a built-in mutator.
*/

#include "liteparser.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Core fuzz target — called by libFuzzer or standalone driver        */
/* ------------------------------------------------------------------ */

static int fuzz_one_input(const uint8_t *data, size_t size) {
    /* Limit input size to avoid timeouts on huge inputs */
    if (size > 64 * 1024) return 0;

    /* Null-terminate the input */
    char *sql = malloc(size + 1);
    if (!sql) return 0;
    memcpy(sql, data, size);
    sql[size] = '\0';

    arena_t *arena = arena_create(256 * 1024);
    if (!arena) { free(sql); return 0; }

    /* --- 1. Strict single-statement parse --- */
    {
        const char *err = NULL;
        LpNode *node = lp_parse(sql, arena, &err);
        if (node) {
            /* JSON serialization (compact and pretty) */
            char *json = lp_ast_to_json(node, arena, 0);
            (void)json;
            char *json_pretty = lp_ast_to_json(node, arena, 1);
            (void)json_pretty;

            /* SQL unparsing */
            char *sql_out = lp_ast_to_sql(node, arena);
            (void)sql_out;

            /* Node counting */
            int count = lp_node_count(node);
            (void)count;

            /* Self-equality */
            int eq = lp_node_equal(node, node);
            (void)eq;

            /* Cloning and equality with clone */
            LpNode *clone = lp_node_clone(arena, node);
            if (clone) {
                int eq2 = lp_node_equal(node, clone);
                (void)eq2;
            }

            /* Fix parents */
            lp_fix_parents(node);

            /* Walk with NULL visitor callbacks (tests walker traversal) */
            LpVisitor visitor;
            memset(&visitor, 0, sizeof(visitor));
            lp_ast_walk(node, &visitor);
        }
    }

    arena_reset(arena);

    /* --- 2. Strict multi-statement parse --- */
    {
        const char *err = NULL;
        LpNodeList *stmts = lp_parse_all(sql, arena, &err);
        if (stmts) {
            for (int i = 0; i < stmts->count; i++) {
                LpNode *node = stmts->items[i];
                if (node) {
                    char *json = lp_ast_to_json(node, arena, 0);
                    (void)json;
                    char *sql_out = lp_ast_to_sql(node, arena);
                    (void)sql_out;
                }
            }
        }
    }

    arena_reset(arena);

    /* --- 3. Tolerant parse (error recovery) --- */
    {
        LpParseResult *result = lp_parse_tolerant(sql, arena);
        if (result) {
            /* Serialize result with errors to JSON */
            char *json = lp_parse_result_to_json(result, arena, 0);
            (void)json;
            char *json_pretty = lp_parse_result_to_json(result, arena, 1);
            (void)json_pretty;

            /* Process each statement */
            for (int i = 0; i < result->stmts.count; i++) {
                LpNode *node = result->stmts.items[i];
                if (node) {
                    char *sql_out = lp_ast_to_sql(node, arena);
                    (void)sql_out;
                    lp_fix_parents(node);
                    int count = lp_node_count(node);
                    (void)count;
                }
            }
        }
    }

    /* --- 4. Re-parse unparsed SQL (roundtrip) --- */
    arena_reset(arena);
    {
        const char *err = NULL;
        LpNode *node = lp_parse(sql, arena, &err);
        if (node) {
            char *sql2 = lp_ast_to_sql(node, arena);
            if (sql2) {
                const char *err2 = NULL;
                LpNode *node2 = lp_parse(sql2, arena, &err2);
                if (node2) {
                    lp_node_equal(node, node2);
                }
            }
        }
    }

    arena_destroy(arena);
    free(sql);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  libFuzzer entry point                                              */
/* ------------------------------------------------------------------ */

#ifdef USE_LIBFUZZER

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return fuzz_one_input(data, size);
}

#else

/* ------------------------------------------------------------------ */
/*  Standalone driver with built-in mutation                           */
/* ------------------------------------------------------------------ */

/* SQL fragments for mutation */
static const char *SQL_KEYWORDS[] = {
    "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE", "SET",
    "DELETE", "CREATE", "TABLE", "INDEX", "VIEW", "TRIGGER", "DROP", "ALTER",
    "ADD", "COLUMN", "RENAME", "TO", "BEGIN", "COMMIT", "ROLLBACK", "SAVEPOINT",
    "RELEASE", "EXPLAIN", "QUERY", "PLAN", "ANALYZE", "VACUUM", "REINDEX",
    "ATTACH", "DETACH", "PRAGMA", "WITH", "RECURSIVE", "AS", "UNION", "ALL",
    "INTERSECT", "EXCEPT", "ORDER", "BY", "GROUP", "HAVING", "LIMIT", "OFFSET",
    "JOIN", "INNER", "LEFT", "RIGHT", "FULL", "OUTER", "CROSS", "NATURAL",
    "ON", "USING", "AND", "OR", "NOT", "IN", "EXISTS", "BETWEEN", "LIKE",
    "GLOB", "MATCH", "REGEXP", "IS", "NULL", "CASE", "WHEN", "THEN", "ELSE",
    "END", "CAST", "COLLATE", "ASC", "DESC", "DISTINCT", "UNIQUE", "PRIMARY",
    "KEY", "FOREIGN", "REFERENCES", "CHECK", "DEFAULT", "CONSTRAINT", "IF",
    "NOT", "EXISTS", "CONFLICT", "REPLACE", "ABORT", "FAIL", "IGNORE",
    "ROLLBACK", "AUTOINCREMENT", "ROWID", "INDEXED", "WINDOW", "OVER",
    "PARTITION", "RANGE", "ROWS", "GROUPS", "UNBOUNDED", "PRECEDING",
    "FOLLOWING", "CURRENT", "ROW", "EXCLUDE", "NO", "OTHERS", "TIES",
    "FILTER", "RETURNING", "GENERATED", "ALWAYS", "STORED", "VIRTUAL",
    "INSTEAD", "OF", "FOR", "EACH", "BEFORE", "AFTER", "RAISE",
    "DEFERRED", "IMMEDIATE", "EXCLUSIVE", "TEMP", "TEMPORARY",
    "WITHOUT", "STRICT", "DO", "NOTHING", "UPSERT",
};
#define NUM_KEYWORDS (sizeof(SQL_KEYWORDS) / sizeof(SQL_KEYWORDS[0]))

static const char *SQL_EXPRS[] = {
    "1", "0", "-1", "42", "3.14", "1e10", "0x1F",
    "'hello'", "'it''s'", "''", "X'DEADBEEF'",
    "NULL", "TRUE", "FALSE", "CURRENT_TIME", "CURRENT_DATE", "CURRENT_TIMESTAMP",
    "*", "t.*", "a.b.c", "\"quoted\"",
    "1 + 2", "a * b", "x / 0", "a || b",
    "a IS NULL", "b IS NOT NULL", "c ISNULL", "d NOTNULL",
    "a BETWEEN 1 AND 10", "x IN (1,2,3)", "y IN (SELECT 1)",
    "CAST(x AS INTEGER)", "CAST(y AS TEXT)",
    "CASE x WHEN 1 THEN 'a' ELSE 'b' END",
    "CASE WHEN x > 0 THEN 'pos' WHEN x < 0 THEN 'neg' ELSE 'zero' END",
    "count(*)", "sum(x)", "avg(y)", "min(z)", "max(w)",
    "group_concat(a, ',')", "coalesce(a, b, c)",
    "json_extract(x, '$.key')", "json_array(1,2,3)",
    "(SELECT 1)", "EXISTS (SELECT 1)",
    "x -> '$.key'", "x ->> '$.key'",
    "x LIKE '%pat%'", "x GLOB '*'", "x LIKE '%' ESCAPE '\\'",
    "x COLLATE NOCASE", "x COLLATE BINARY",
    "count(*) OVER ()", "sum(x) OVER (PARTITION BY y ORDER BY z)",
    "row_number() OVER w",
    "+x", "-x", "~x", "NOT x",
    "likelihood(x, 0.5)", "unlikely(x)", "likely(x)",
    "typeof(x)", "length(x)", "abs(x)", "unicode(x)",
    "substr(x, 1, 3)", "replace(x, 'a', 'b')",
    "printf('%d', x)", "zeroblob(100)",
    "iif(x, y, z)", "nullif(a, b)", "ifnull(a, b)",
};
#define NUM_EXPRS (sizeof(SQL_EXPRS) / sizeof(SQL_EXPRS[0]))

/* Seed SQL statements covering many grammar paths */
static const char *SEEDS[] = {
    /* Basic DML */
    "SELECT 1",
    "SELECT a, b, c FROM t WHERE x > 0 ORDER BY a LIMIT 10",
    "SELECT DISTINCT a FROM t GROUP BY a HAVING count(*) > 1",
    "SELECT * FROM t1 JOIN t2 ON t1.id = t2.id",
    "SELECT * FROM t1 LEFT OUTER JOIN t2 USING (id)",
    "SELECT * FROM t1 NATURAL JOIN t2",
    "SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t2.id = t3.id",
    "INSERT INTO t (a, b) VALUES (1, 2)",
    "INSERT INTO t DEFAULT VALUES",
    "INSERT INTO t SELECT * FROM t2",
    "INSERT OR REPLACE INTO t (a) VALUES (1)",
    "INSERT INTO t (a) VALUES (1) ON CONFLICT (a) DO UPDATE SET a = excluded.a",
    "INSERT INTO t (a) VALUES (1) ON CONFLICT DO NOTHING",
    "INSERT INTO t (a) VALUES (1) RETURNING *",
    "UPDATE t SET a = 1 WHERE id = 2",
    "UPDATE OR IGNORE t SET a = 1",
    "UPDATE t SET a = 1, b = 2 RETURNING a, b",
    "DELETE FROM t WHERE id = 1",
    "DELETE FROM t RETURNING *",
    "REPLACE INTO t (a) VALUES (1)",

    /* Subqueries */
    "SELECT * FROM (SELECT 1 AS a) sub",
    "SELECT (SELECT max(id) FROM t)",
    "SELECT * FROM t WHERE id IN (SELECT id FROM t2)",
    "SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2 WHERE t2.id = t.id)",

    /* CTEs */
    "WITH cte AS (SELECT 1) SELECT * FROM cte",
    "WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<10) SELECT x FROM cnt",
    "WITH a AS (SELECT 1), b AS (SELECT 2) SELECT * FROM a, b",

    /* Set operations */
    "SELECT 1 UNION SELECT 2",
    "SELECT 1 UNION ALL SELECT 2",
    "SELECT 1 INTERSECT SELECT 2",
    "SELECT 1 EXCEPT SELECT 2",
    "SELECT 1 UNION SELECT 2 UNION ALL SELECT 3",

    /* Window functions */
    "SELECT row_number() OVER (ORDER BY id) FROM t",
    "SELECT sum(x) OVER (PARTITION BY g ORDER BY id ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) FROM t",
    "SELECT avg(x) OVER w FROM t WINDOW w AS (PARTITION BY g)",
    "SELECT count(*) OVER (GROUPS BETWEEN 1 PRECEDING AND 1 FOLLOWING EXCLUDE TIES) FROM t",
    "SELECT ntile(4) OVER (ORDER BY id) FROM t",
    "SELECT lag(x, 1, 0) OVER (ORDER BY id) FROM t",
    "SELECT count(*) FILTER (WHERE x > 0) OVER (ORDER BY id) FROM t",

    /* DDL */
    "CREATE TABLE t (a INTEGER PRIMARY KEY, b TEXT NOT NULL, c REAL DEFAULT 0.0)",
    "CREATE TABLE IF NOT EXISTS t (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT COLLATE NOCASE)",
    "CREATE TABLE t (a INT, b INT, UNIQUE(a, b), CHECK(a > 0))",
    "CREATE TABLE t (a INT REFERENCES other(id) ON DELETE CASCADE ON UPDATE SET NULL)",
    "CREATE TABLE t (a INT, b INT, FOREIGN KEY (a, b) REFERENCES other(x, y))",
    "CREATE TABLE t (a INT, b INT, PRIMARY KEY (a, b) ON CONFLICT ROLLBACK)",
    "CREATE TABLE t (a INT GENERATED ALWAYS AS (b + 1) STORED)",
    "CREATE TABLE t (a INT GENERATED ALWAYS AS (b + 1) VIRTUAL)",
    "CREATE TEMP TABLE t (a INT)",
    "CREATE TABLE t (a INT) WITHOUT ROWID",
    "CREATE TABLE t (a INT) STRICT",
    "CREATE TABLE t (a INT) WITHOUT ROWID, STRICT",
    "CREATE TABLE t AS SELECT * FROM other",
    "CREATE INDEX idx ON t (a)",
    "CREATE UNIQUE INDEX IF NOT EXISTS idx ON t (a, b DESC) WHERE a > 0",
    "CREATE VIEW v AS SELECT * FROM t",
    "CREATE VIEW IF NOT EXISTS v (a, b) AS SELECT 1, 2",
    "CREATE TRIGGER tr BEFORE INSERT ON t BEGIN SELECT 1; END",
    "CREATE TRIGGER tr AFTER UPDATE OF a, b ON t FOR EACH ROW WHEN NEW.a > 0 BEGIN UPDATE t SET b = 1; END",
    "CREATE TRIGGER tr INSTEAD OF DELETE ON v BEGIN SELECT 1; END",
    "CREATE VIRTUAL TABLE t USING fts5(a, b, content='')",

    /* ALTER / DROP */
    "ALTER TABLE t ADD COLUMN c TEXT",
    "ALTER TABLE t RENAME TO t2",
    "ALTER TABLE t RENAME COLUMN a TO b",
    "ALTER TABLE t DROP COLUMN c",
    "DROP TABLE t",
    "DROP TABLE IF EXISTS t",
    "DROP INDEX idx",
    "DROP VIEW IF EXISTS v",
    "DROP TRIGGER tr",

    /* Transaction control */
    "BEGIN",
    "BEGIN DEFERRED",
    "BEGIN IMMEDIATE",
    "BEGIN EXCLUSIVE",
    "COMMIT",
    "END",
    "ROLLBACK",
    "ROLLBACK TO sp1",
    "SAVEPOINT sp1",
    "RELEASE sp1",
    "RELEASE SAVEPOINT sp1",

    /* EXPLAIN */
    "EXPLAIN SELECT 1",
    "EXPLAIN QUERY PLAN SELECT * FROM t",

    /* PRAGMA */
    "PRAGMA table_info(t)",
    "PRAGMA journal_mode = WAL",
    "PRAGMA foreign_keys",

    /* ATTACH / DETACH */
    "ATTACH DATABASE 'file.db' AS db2",
    "DETACH DATABASE db2",

    /* Other */
    "ANALYZE",
    "ANALYZE t",
    "VACUUM",
    "VACUUM INTO 'backup.db'",
    "REINDEX",
    "REINDEX t",

    /* Multi-statement */
    "SELECT 1; SELECT 2; SELECT 3",
    "BEGIN; INSERT INTO t VALUES(1); COMMIT",
    "CREATE TABLE t(a); INSERT INTO t VALUES(1); SELECT * FROM t; DROP TABLE t",

    /* Complex expressions */
    "SELECT CAST(x AS INTEGER), CAST(y AS TEXT), CAST(z AS REAL) FROM t",
    "SELECT CASE WHEN a > 0 THEN 'pos' WHEN a < 0 THEN 'neg' ELSE 'zero' END FROM t",
    "SELECT a BETWEEN 1 AND 10, b NOT BETWEEN 5 AND 15 FROM t",
    "SELECT a IN (1,2,3), b NOT IN (SELECT id FROM t2) FROM t",
    "SELECT a IS NULL, b IS NOT NULL, c ISNULL, d NOTNULL FROM t",
    "SELECT a LIKE '%test%', b GLOB '*.txt', c LIKE '%x%' ESCAPE '\\' FROM t",
    "SELECT a COLLATE NOCASE, b COLLATE BINARY FROM t",
    "SELECT json_extract(data, '$.key'), data -> '$.key', data ->> '$.key' FROM t",
    "SELECT coalesce(a, b, c), ifnull(x, y), nullif(a, b) FROM t",
    "SELECT typeof(x), length(x), abs(x), unicode(x) FROM t",
    "SELECT substr(x, 1, 3), replace(x, 'a', 'b'), printf('%d', x) FROM t",
    "SELECT iif(x > 0, 'yes', 'no') FROM t",
    "SELECT likelihood(x, 0.5), unlikely(y), likely(z) FROM t",
    "SELECT +a, -b, ~c, NOT d FROM t",
    "SELECT 1 + 2 * 3 - 4 / 5 % 6",
    "SELECT a & b, a | b, a << b, a >> b",

    /* Edge cases */
    "SELECT 1 WHERE 1",
    "SELECT 1 ORDER BY 1",
    "SELECT 1 LIMIT -1",
    "SELECT 1 LIMIT 10 OFFSET 20",
    "SELECT 1 LIMIT 10, 20",
    "SELECT x AS [weird name] FROM t",
    "SELECT x AS \"also weird\" FROM t",
    "SELECT `backtick` FROM t",
    "SELECT 'string with ''quotes''', X'ABCD', 0xFF FROM t",
    "SELECT 1e10, 1.5e-3, .5, 5.",

    /* Deeply nested */
    "SELECT ((((((1))))))",
    "SELECT * FROM (SELECT * FROM (SELECT * FROM (SELECT 1)))",
    "SELECT * FROM t WHERE a IN (SELECT b FROM t2 WHERE c IN (SELECT d FROM t3))",
    "WITH a AS (WITH b AS (SELECT 1) SELECT * FROM b) SELECT * FROM a",
};
#define NUM_SEEDS (sizeof(SEEDS) / sizeof(SEEDS[0]))

static uint32_t rng_state;

static uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int rng_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (unsigned)(hi - lo));
}

/* Mutate a buffer in-place. Returns new length. */
static size_t mutate(char *buf, size_t len, size_t cap) {
    if (len == 0) {
        /* Pick a random seed */
        const char *seed = SEEDS[rng_next() % NUM_SEEDS];
        size_t slen = strlen(seed);
        if (slen >= cap) slen = cap - 1;
        memcpy(buf, seed, slen);
        return slen;
    }

    int op = rng_range(0, 12);

    switch (op) {
    case 0: /* Flip a random byte */
        if (len > 0) buf[rng_next() % len] ^= (1 << rng_range(0, 8));
        break;

    case 1: /* Insert a random byte */
        if (len < cap - 1) {
            size_t pos = rng_next() % (len + 1);
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos] = (char)(rng_next() & 0xFF);
            len++;
        }
        break;

    case 2: /* Delete a random byte */
        if (len > 1) {
            size_t pos = rng_next() % len;
            memmove(buf + pos, buf + pos + 1, len - pos - 1);
            len--;
        }
        break;

    case 3: /* Replace a random range with a keyword */
    case 4: {
        const char *kw = SQL_KEYWORDS[rng_next() % NUM_KEYWORDS];
        size_t kwlen = strlen(kw);
        if (len > 0 && kwlen + 1 < cap) {
            size_t pos = rng_next() % len;
            size_t del = rng_range(0, (int)(len - pos));
            if (del > 8) del = 8;
            size_t need = kwlen + 1; /* keyword + space */
            size_t newlen = len - del + need;
            if (newlen >= cap) break;
            memmove(buf + pos + need, buf + pos + del, len - pos - del);
            memcpy(buf + pos, kw, kwlen);
            buf[pos + kwlen] = ' ';
            len = newlen;
        }
        break;
    }

    case 5: /* Insert an expression fragment */
    case 6: {
        const char *expr = SQL_EXPRS[rng_next() % NUM_EXPRS];
        size_t elen = strlen(expr);
        if (elen + 2 < cap - len) {
            size_t pos = rng_next() % (len + 1);
            size_t need = elen + 2;
            memmove(buf + pos + need, buf + pos, len - pos);
            buf[pos] = ' ';
            memcpy(buf + pos + 1, expr, elen);
            buf[pos + 1 + elen] = ' ';
            len += need;
        }
        break;
    }

    case 7: /* Insert a semicolon (multi-statement) */
        if (len < cap - 2) {
            size_t pos = rng_next() % (len + 1);
            memmove(buf + pos + 2, buf + pos, len - pos);
            buf[pos] = ';';
            buf[pos + 1] = ' ';
            len += 2;
        }
        break;

    case 8: /* Duplicate a random chunk */
        if (len > 2 && len * 2 < cap) {
            size_t pos = rng_next() % len;
            size_t chunk = rng_range(1, (int)(len - pos));
            if (chunk > 32) chunk = 32;
            if (len + chunk < cap) {
                /* Copy chunk to temp buffer first to avoid overlap */
                char tmp[32];
                memcpy(tmp, buf + pos, chunk);
                size_t ins = rng_next() % (len + 1);
                memmove(buf + ins + chunk, buf + ins, len - ins);
                memcpy(buf + ins, tmp, chunk);
                len += chunk;
            }
        }
        break;

    case 9: /* Crossover with a seed */
    {
        const char *seed = SEEDS[rng_next() % NUM_SEEDS];
        size_t slen = strlen(seed);
        size_t from = rng_next() % slen;
        size_t chunk = rng_range(1, (int)(slen - from));
        if (chunk > 40) chunk = 40;
        if (len > 0 && len + chunk < cap) {
            size_t pos = rng_next() % len;
            size_t del = rng_range(0, (int)(len - pos));
            if (del > chunk) del = chunk;
            size_t newlen = len - del + chunk;
            if (newlen >= cap) break;
            memmove(buf + pos + chunk, buf + pos + del, len - pos - del);
            memcpy(buf + pos, seed + from, chunk);
            len = newlen;
        }
        break;
    }

    case 10: /* Replace with entirely new seed */
    {
        const char *seed = SEEDS[rng_next() % NUM_SEEDS];
        size_t slen = strlen(seed);
        if (slen >= cap) slen = cap - 1;
        memcpy(buf, seed, slen);
        len = slen;
        break;
    }

    case 11: /* Insert special characters */
    {
        static const char specials[] = "();,.'\"[]{}+-*/<>=!@#$%^&|~`?\\\n\r\t\0";
        if (len < cap - 1) {
            size_t pos = rng_next() % (len + 1);
            char ch = specials[rng_next() % (sizeof(specials) - 1)];
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos] = ch;
            len++;
        }
        break;
    }
    }

    return len;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  --iterations N    Number of iterations (default: 1000000)\n"
        "  --seed N          RNG seed (default: time-based)\n"
        "  --max-len N       Max input length in bytes (default: 4096)\n"
        "  --print-every N   Print progress every N iterations (default: 10000)\n"
        "  --corpus DIR      Read initial corpus from directory\n"
        "  --help            Show this help\n",
        prog);
}

int main(int argc, char **argv) {
    int iterations = 1000000;
    uint32_t seed = (uint32_t)time(NULL);
    size_t max_len = 4096;
    int print_every = 10000;
    const char *corpus_dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint32_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--max-len") == 0 && i + 1 < argc) {
            max_len = (size_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--print-every") == 0 && i + 1 < argc) {
            print_every = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--corpus") == 0 && i + 1 < argc) {
            corpus_dir = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    printf("Fuzz testing liteparser\n");
    printf("  Seed: %u\n", seed);
    printf("  Iterations: %d\n", iterations);
    printf("  Max input length: %zu bytes\n", max_len);
    printf("  Seeds: %d statement templates\n", (int)NUM_SEEDS);
    rng_state = seed;

    /* Allocate mutation buffer */
    char *buf = calloc(1, max_len + 1);
    if (!buf) { perror("calloc"); return 1; }
    size_t buf_len = 0;

    /* First run all seeds directly */
    printf("\n[Phase 1] Running %d seed inputs...\n", (int)NUM_SEEDS);
    for (int i = 0; i < (int)NUM_SEEDS; i++) {
        fuzz_one_input((const uint8_t *)SEEDS[i], strlen(SEEDS[i]));
    }
    printf("  Done. All seeds processed without crashes.\n");

    /* Read corpus files if provided */
    int corpus_count = 0;
    if (corpus_dir) {
        printf("\n[Phase 2] Reading corpus from %s...\n", corpus_dir);
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "ls '%s' 2>/dev/null", corpus_dir);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char fname[512];
            while (fgets(fname, sizeof(fname), fp)) {
                fname[strcspn(fname, "\n")] = '\0';
                char path[1024];
                snprintf(path, sizeof(path), "%s/%s", corpus_dir, fname);
                FILE *f = fopen(path, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (fsize > 0 && (size_t)fsize <= max_len) {
                        char *data = malloc((size_t)fsize);
                        if (data && fread(data, 1, (size_t)fsize, f) == (size_t)fsize) {
                            fuzz_one_input((const uint8_t *)data, (size_t)fsize);
                            corpus_count++;
                        }
                        free(data);
                    }
                    fclose(f);
                }
            }
            pclose(fp);
        }
        printf("  Processed %d corpus files.\n", corpus_count);
    }

    /* Mutation-based fuzzing */
    printf("\n[Phase 3] Mutation-based fuzzing (%d iterations)...\n", iterations);

    int parse_ok = 0, parse_err = 0;
    int tolerant_stmts = 0, tolerant_errs = 0;

    for (int i = 0; i < iterations; i++) {
        /* Apply 1-3 mutations */
        int num_mutations = rng_range(1, 4);
        for (int m = 0; m < num_mutations; m++) {
            buf_len = mutate(buf, buf_len, max_len);
        }
        buf[buf_len] = '\0';

        /* Run the fuzz target */
        fuzz_one_input((const uint8_t *)buf, buf_len);

        /* Collect stats (lightweight) */
        arena_t *a = arena_create(64 * 1024);
        const char *err = NULL;
        LpNode *n = lp_parse(buf, a, &err);
        if (n) parse_ok++; else parse_err++;

        LpParseResult *r = lp_parse_tolerant(buf, a);
        if (r) {
            tolerant_stmts += r->stmts.count;
            tolerant_errs += r->errors.count;
        }
        arena_destroy(a);

        if (print_every > 0 && (i + 1) % print_every == 0) {
            printf("  [%d/%d] parsed_ok=%d parsed_err=%d tolerant_stmts=%d tolerant_errs=%d\n",
                   i + 1, iterations, parse_ok, parse_err, tolerant_stmts, tolerant_errs);
        }

        /* Occasionally reset to a seed to maintain diversity */
        if (rng_next() % 100 == 0) {
            buf_len = 0;
        }
    }

    printf("\nFuzzing complete.\n");
    printf("  Total iterations: %d\n", iterations);
    printf("  Successful parses: %d\n", parse_ok);
    printf("  Parse errors (expected): %d\n", parse_err);
    printf("  Tolerant-mode statements: %d\n", tolerant_stmts);
    printf("  Tolerant-mode errors: %d\n", tolerant_errs);
    printf("  No crashes detected.\n");

    free(buf);
    return 0;
}

#endif /* USE_LIBFUZZER */
