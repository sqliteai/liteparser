/*
** main.c — CLI tool: SQL → JSON AST or SQL round-trip
**
** Usage: ./sqlparse [OPTIONS] [SQL]
**        echo "SELECT 1" | ./sqlparse
*/
#include "liteparser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    fprintf(stderr,
        "Usage: sqlparse [OPTIONS] [SQL]\n"
        "\n"
        "Parse SQLite SQL and output a JSON AST or round-tripped SQL.\n"
        "If no SQL argument is given, reads from stdin.\n"
        "\n"
        "Options:\n"
        "  --tolerant   Continue past syntax errors (IDE/linter mode)\n"
        "  --unparse    Output reconstructed SQL instead of JSON\n"
        "  --compact    Output compact JSON (no indentation)\n"
        "  --help       Show this help message\n"
        "  --version    Show version information\n"
    );
}

static char *read_stdin(void) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    while (!feof(stdin)) {
        size_t n = fread(buf + len, 1, cap - len - 1, stdin);
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
    }
    buf[len] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    const char *sql = NULL;
    char *stdin_buf = NULL;
    int tolerant = 0;
    int unparse = 0;
    int pretty = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tolerant") == 0) {
            tolerant = 1;
        } else if (strcmp(argv[i], "--unparse") == 0) {
            unparse = 1;
        } else if (strcmp(argv[i], "--compact") == 0) {
            pretty = 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("sqlparse %s (liteparser)\n", LITEPARSER_VERSION);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        } else if (!sql) {
            sql = argv[i];
        }
    }

    if (!sql) {
        stdin_buf = read_stdin();
        if (!stdin_buf) {
            fprintf(stderr, "Error: failed to read stdin\n");
            return 1;
        }
        sql = stdin_buf;
    }

    arena_t *arena = arena_create(64 * 1024);
    if (!arena) {
        fprintf(stderr, "Error: failed to create arena\n");
        free(stdin_buf);
        return 1;
    }

    if (tolerant) {
        LpParseResult *result = lp_parse_tolerant(sql, arena);
        if (unparse) {
            for (int i = 0; i < result->stmts.count; i++) {
                char *s = lp_ast_to_sql(result->stmts.items[i], arena);
                if (s) printf("%s;\n", s);
            }
            if (result->errors.count > 0) {
                fprintf(stderr, "%d error(s):\n", result->errors.count);
                for (int i = 0; i < result->errors.count; i++)
                    fprintf(stderr, "  %s\n", result->errors.items[i].message);
            }
        } else {
            char *json = lp_parse_result_to_json(result, arena, pretty);
            if (json) printf("%s", json);
        }
    } else {
        const char *err = NULL;
        LpNodeList *stmts = lp_parse_all(sql, arena, &err);

        if (!stmts) {
            fprintf(stderr, "Error: %s\n", err ? err : "unknown parse error");
            arena_destroy(arena);
            free(stdin_buf);
            return 1;
        }

        if (unparse) {
            for (int i = 0; i < stmts->count; i++) {
                char *s = lp_ast_to_sql(stmts->items[i], arena);
                if (s) printf("%s;\n", s);
            }
        } else if (stmts->count == 1) {
            char *json = lp_ast_to_json(stmts->items[0], arena, pretty);
            if (json) printf("%s", json);
        } else {
            printf(pretty ? "[\n" : "[");
            for (int i = 0; i < stmts->count; i++) {
                if (i > 0) printf(",");
                if (pretty) printf("\n  ");
                char *json = lp_ast_to_json(stmts->items[i], arena, pretty);
                if (json) printf("%s", json);
            }
            printf(pretty ? "\n]\n" : "]");
        }
        if (!unparse) printf("\n");
    }

    arena_destroy(arena);
    free(stdin_buf);
    return 0;
}
