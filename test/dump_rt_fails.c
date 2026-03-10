/*
** dump_rt_fails.c — Dump all round-trip failures with full details.
*/
#include "liteparser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static char **split_nul(char *buf, size_t len, int *out_count) {
    int count = 0;
    for (size_t i = 0; i < len; i++) if (buf[i] == '\0') count++;
    char **stmts = malloc(count * sizeof(char *));
    int idx = 0; char *p = buf;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\0') { stmts[idx++] = p; p = buf + i + 1; }
    }
    *out_count = idx;
    return stmts;
}

int main(int argc, char **argv) {
    const char *input_file = argv[1];
    if (!input_file) { fprintf(stderr, "Usage: %s <file>\n", argv[0]); return 1; }

    size_t flen;
    char *buf = read_file(input_file, &flen);
    int count;
    char **stmts = split_nul(buf, flen, &count);

    int fail_num = 0;

    for (int i = 0; i < count; i++) {
        const char *sql = stmts[i];
        if (!sql || !*sql || strlen(sql) >= 2000) continue;

        arena_t *arena = arena_create(512 * 1024);
        LpParseResult *result = lp_parse_tolerant(sql, arena);

        if (!result || result->stmts.count == 0 || result->errors.count > 0) {
            arena_destroy(arena);
            continue;
        }

        for (int j = 0; j < result->stmts.count; j++) {
            char *unparsed = lp_ast_to_sql(result->stmts.items[j], arena);
            if (!unparsed) continue;

            arena_t *a2 = arena_create(512 * 1024);
            const char *err = NULL;
            LpNode *n2 = lp_parse(unparsed, a2, &err);
            if (!n2) {
                fail_num++;
                printf("=== FAIL #%d (idx=%d) ===\n", fail_num, i);
                printf("INPUT:    %s\n", sql);
                printf("UNPARSED: %s\n", unparsed);
                printf("ERROR:    %s\n\n", err ? err : "?");
            }
            arena_destroy(a2);
        }
        arena_destroy(arena);
    }

    printf("Total failures: %d\n", fail_num);
    free(stmts); free(buf);
    return 0;
}
