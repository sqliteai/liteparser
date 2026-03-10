/*
** test_sqlite_suite.c — Bulk parser stress test using SQL extracted from
** SQLite's own test suite.
**
** Reads NUL-delimited SQL blocks from a file, parses each one using
** lp_parse_tolerant(), and reports statistics.
**
** Usage: ./test_sqlite_suite [--roundtrip] [--verbose] <sql_file>
*/
#include "liteparser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static char **split_nul(char *buf, size_t len, int *out_count) {
    int count = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\0') count++;
    }
    char **stmts = (char **)malloc(count * sizeof(char *));
    int idx = 0;
    char *p = buf;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\0') {
            stmts[idx++] = p;
            p = buf + i + 1;
        }
    }
    *out_count = idx;
    return stmts;
}

typedef struct {
    int total;
    int parsed_ok;
    int fully_parsed;
    int parse_errors;
    int empty;
    int roundtrip_ok;
    int roundtrip_fail;
    int roundtrip_skip;
} Stats;

/* Test a single SQL block. Returns 1 if roundtrip failed, 0 otherwise. */
static int test_one(const char *sql, Stats *stats, int do_roundtrip, int verbose) {
    arena_t *arena = arena_create(512 * 1024);
    if (!arena) return 0;

    LpParseResult *result = lp_parse_tolerant(sql, arena);

    if (!result || (result->stmts.count == 0 && result->errors.count == 0)) {
        stats->empty++;
        arena_destroy(arena);
        return 0;
    }

    if (result->stmts.count > 0 && result->errors.count == 0) {
        stats->fully_parsed++;
        stats->parsed_ok++;
    } else if (result->stmts.count > 0) {
        stats->parsed_ok++;
        stats->parse_errors++;
    } else {
        stats->parse_errors++;
    }

    /* Round-trip: only test fully-parsed, reasonably-sized SQL */
    if (do_roundtrip && result->stmts.count > 0
        && result->errors.count == 0 && strlen(sql) < 2000) {
        for (int j = 0; j < result->stmts.count; j++) {
            char *unparsed = lp_ast_to_sql(result->stmts.items[j], arena);
            if (!unparsed) {
                stats->roundtrip_skip++;
                arena_destroy(arena);
                return 0;
            }
            /* Reparse in a fresh arena to avoid exhausting the first one */
            arena_t *arena2 = arena_create(512 * 1024);
            if (!arena2) {
                stats->roundtrip_skip++;
                arena_destroy(arena);
                return 0;
            }
            const char *err2 = NULL;
            LpNode *reparsed = lp_parse(unparsed, arena2, &err2);
            if (!reparsed) {
                if (verbose) {
                    fprintf(stderr, "  RT-FAIL: %.80s%s\n    -> %.200s\n    err: %s\n",
                            sql, strlen(sql) > 80 ? "..." : "",
                            unparsed, err2 ? err2 : "?");
                }
                stats->roundtrip_fail++;
                arena_destroy(arena2);
                arena_destroy(arena);
                return 1;
            }
            arena_destroy(arena2);
        }
        stats->roundtrip_ok++;
    }

    arena_destroy(arena);
    return 0;
}

int main(int argc, char **argv) {
    int do_roundtrip = 0;
    int verbose = 0;
    const char *input_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--roundtrip") == 0) do_roundtrip = 1;
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) verbose = 1;
        else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [--roundtrip] [-v] <sql_file>\n", argv[0]);
            return 0;
        } else input_file = argv[i];
    }
    if (!input_file) {
        fprintf(stderr, "Usage: %s [--roundtrip] [-v] <sql_file>\n", argv[0]);
        return 1;
    }

    size_t file_len;
    char *buf = read_file(input_file, &file_len);
    if (!buf) return 1;

    int count;
    char **stmts = split_nul(buf, file_len, &count);
    if (count == 0) {
        fprintf(stderr, "No SQL statements found\n");
        free(buf); free(stmts);
        return 1;
    }

    fprintf(stderr, "Testing parser with %d SQL blocks from SQLite test suite\n", count);

    Stats stats = {0};
    stats.total = count;
    int rt_fail_examples = 0;

    clock_t start = clock();

    for (int i = 0; i < count; i++) {
        /* Progress every 2% */
        if (count >= 100 && (i % (count / 50) == 0)) {
            fprintf(stderr, "\r  [%3d%%] %d / %d", (int)(100.0 * i / count), i, count);
            fflush(stderr);
        }

        const char *sql = stmts[i];
        if (!sql || !*sql) { stats.empty++; continue; }

        /* Only print first 20 RT failures in verbose mode */
        int show = verbose && rt_fail_examples < 20;
        int failed = test_one(sql, &stats, do_roundtrip, show);
        if (failed) rt_fail_examples++;
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    fprintf(stderr, "\r  [100%%] %d / %d\n\n", count, count);

    printf("=== SQLite Test Suite Parser Results ===\n\n");
    printf("Total SQL blocks:          %d\n", stats.total);
    printf("Fully parsed (no errors):  %d (%.1f%%)\n",
           stats.fully_parsed, 100.0 * stats.fully_parsed / stats.total);
    printf("Partially parsed:          %d\n",
           stats.parsed_ok - stats.fully_parsed);
    printf("Parse errors only:         %d\n",
           stats.parse_errors - (stats.parsed_ok - stats.fully_parsed));
    printf("Empty/skipped:             %d\n", stats.empty);

    if (do_roundtrip) {
        int rt_total = stats.roundtrip_ok + stats.roundtrip_fail + stats.roundtrip_skip;
        printf("\n--- Round-trip (unparse -> reparse) ---\n");
        printf("Tested:                    %d\n", rt_total);
        printf("Round-trip OK:             %d (%.1f%%)\n",
               stats.roundtrip_ok,
               rt_total > 0 ? 100.0 * stats.roundtrip_ok / rt_total : 0);
        printf("Round-trip FAIL:           %d\n", stats.roundtrip_fail);
        printf("Round-trip skip:           %d\n", stats.roundtrip_skip);
    }

    printf("\nTime: %.2f seconds\n", elapsed);
    printf("Speed: %.0f stmts/sec\n", stats.total / (elapsed > 0 ? elapsed : 0.001));

    free(stmts);
    free(buf);
    return 0;
}
