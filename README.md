# liteparser

**liteparser** is a highly efficient and fully complete SQLite SQL parser. It parses any SQLite SQL into a fully visitable AST, with JSON serialization, SQL unparsing (AST to SQL), tolerant/IDE parsing, and AST mutation, all in pure C with zero dependencies.

Easily embeddable: a single static library (`libliteparser.a`) or shared library, with one header file. No external dependencies, no runtime allocations outside the arena. Drop it into any C, C++, or FFI-capable project.

## Features

- **Complete SQLite grammar**: SELECT, INSERT, UPDATE, DELETE, CREATE TABLE/INDEX/VIEW/TRIGGER, ALTER, DROP, PRAGMA, transactions, CTEs, window functions, virtual tables, UPSERT, RETURNING, and every other SQLite statement
- **Arena-allocated AST**: all nodes allocated from a single arena; one call frees everything. No individual `free()` calls, no memory leaks
- **JSON serialization**: convert any AST to JSON (`lp_ast_to_json`)
- **SQL unparsing**: convert AST back to SQL text with 100% round-trip fidelity (`lp_ast_to_sql`)
- **Tolerant parsing**: continues past syntax errors, collecting all errors and valid statements (`lp_parse_tolerant`), designed for IDE/linter integration
- **Structured error diagnostics**: error code, human-readable message, and precise source range (line, column, byte offset)
- **AST mutation**: allocate, clone, insert, remove, and replace nodes programmatically
- **Visitor pattern**: depth-first traversal with enter/leave callbacks and flow control (skip children, abort)
- **C++ compatible**: all headers wrapped in `extern "C"`

## Parser Origin

The parser grammar is extracted from the original SQLite parser (`parse.y`) with minimal modifications. It uses the same Lemon LALR(1) parser generator and a hand-written tokenizer derived from SQLite's own lexer. This ensures complete and accurate coverage of the SQLite SQL dialect.

## Performance

liteparser is built for speed. The Lemon-generated LALR(1) parser with a hand-written tokenizer produces minimal allocations through arena-based memory.

| Metric | Value |
|--------|-------|
| Parse throughput | **~424,000 statements/sec** |
| Parse + round-trip | **~99,000 statements/sec** |
| Full test suite (22,694 SQL) | **< 0.1 seconds** |
| Memory overhead | **~200 bytes per node** (arena-allocated, no per-node overhead) |

Benchmarked on Apple Silicon. Performance scales linearly with input size.

## Memory Model

All AST nodes, strings, and internal structures are allocated from an `arena_t`. The arena allocates memory in large blocks (default 64 KB) and hands out sub-allocations with bump-pointer speed. When you're done with the AST, a single `arena_destroy()` call frees everything instantly, no tree walking, no reference counting, no GC.

```c
arena_t *arena = arena_create(64 * 1024);  // 64 KB initial block

LpNode *ast = lp_parse("SELECT 1", arena, &err);
// ... use the AST ...

arena_destroy(arena);  // frees everything in one call
```

This model makes **liteparser** ideal for request-scoped parsing (parse, process, destroy) and ensures zero memory leaks regardless of the AST complexity.

## Testing

liteparser is tested against **SQLite's own test suite**, the most comprehensive SQL test corpus in existence:

| Metric | Value |
|--------|-------|
| Test files scanned | **1,227** SQLite `.test`, `.tcl`, and `.sql` files |
| SQL statements extracted | **22,694** unique statements |
| Round-trip success | **100%** (22,186 / 22,186 fully-parsed statements) |
| Crashes | **0** on any input |

Every statement that parses successfully also round-trips perfectly: parsing, unparsing, and reparsing produce an identical AST.

Additionally, a hand-written test suite of **485 unit tests** covers individual grammar rules, edge cases, error recovery, and the mutation API.

## Building

Requires a C compiler (tested with clang and gcc). No other dependencies.

```bash
make              # Build the sqlparse CLI tool (release, -O2)
make shared       # Build shared library (.dylib / .so)
make debug        # Debug build (-g -O0)
make test         # Build and run the unit test suite
make test-suite   # Run the full SQLite-extracted test suite
make clean        # Remove all build artifacts
```

To embed in your project, compile the source files in `src/` or link against `libliteparser.a`:

```bash
make libliteparser.a
cc -Isrc -o myapp myapp.c -L. -lliteparser
```

### Source Files

The library consists of the following files:

| File | Description |
|------|-------------|
| `src/liteparser.h` | Public API header (include this) |
| `src/liteparser_internal.h` | Internal types and builder prototypes |
| `src/arena.h` | Arena allocator API |
| `src/parse.h` | Generated parser tokens and declarations |
| `src/arena.c` | Arena allocator |
| `src/liteparser.c` | AST builder, JSON serializer, visitor, mutation API |
| `src/lp_tokenize.c` | Hand-written lexer |
| `src/lp_unparse.c` | AST-to-SQL unparsing |
| `src/parse.c` | Lemon-generated LALR parser |

**Note:** `parse.c` and `parse.h` are pre-generated from `lp_parse.y` by the Lemon parser generator and checked into the repo. You don't need the `sqlite-master/` directory unless you modify the grammar.

## CLI Usage

```bash
# Parse SQL to JSON AST
./sqlparse "SELECT * FROM users WHERE age > 18"

# Read from stdin
echo "SELECT 1; SELECT 2" | ./sqlparse

# Tolerant mode (continues past errors)
./sqlparse --tolerant "SELECT 1; GARBAGE; SELECT 2"

# Round-trip: parse and output reconstructed SQL
./sqlparse --unparse "SELECT a, b FROM t WHERE x > 5"

# Compact JSON (no indentation)
./sqlparse --compact "SELECT 1"

# Combine flags
./sqlparse --tolerant --compact "SELECT 1; BAD; SELECT 2"
```

## Quick Start

```c
#include "liteparser.h"

int main(void) {
    arena_t *arena = arena_create(64 * 1024);
    const char *err = NULL;

    // Parse a single statement
    LpNode *node = lp_parse("SELECT * FROM users WHERE age > 18", arena, &err);
    if (!node) { fprintf(stderr, "Error: %s\n", err); return 1; }

    // Serialize to JSON
    char *json = lp_ast_to_json(node, arena, 1);
    printf("%s\n", json);

    // Convert back to SQL
    char *sql = lp_ast_to_sql(node, arena);
    printf("SQL: %s\n", sql);

    arena_destroy(arena);
    return 0;
}
```

**Output:**

```json
{
  "kind": "STMT_SELECT",
  "distinct": false,
  "result_columns": [
    {
      "kind": "EXPR_STAR"
    }
  ],
  "from": {
    "kind": "FROM_TABLE",
    "pos": {"line": 1, "col": 15, "offset": 14},
    "name": "users"
  },
  "where": {
    "kind": "EXPR_BINARY_OP",
    "pos": {"line": 1, "col": 27, "offset": 26},
    "op": ">",
    "left": {
      "kind": "EXPR_COLUMN_REF",
      "pos": {"line": 1, "col": 27, "offset": 26},
      "column": "age"
    },
    "right": {
      "kind": "EXPR_LITERAL_INT",
      "pos": {"line": 1, "col": 33, "offset": 32},
      "value": "18"
    }
  }
}
SQL: SELECT * FROM users WHERE age > 18;
```

## API Reference

See [API.md](API.md) for the complete public API reference with descriptions and examples for every function.

## AST Structure

Every node is an `LpNode` tagged union keyed by `LpNodeKind`:

- **Statements** (`LP_STMT_*`): SELECT, INSERT, UPDATE, DELETE, CREATE TABLE, etc.
- **Expressions** (`LP_EXPR_*`): literals, column refs, binary/unary ops, functions, subqueries, CASE, etc.
- **Clauses**: JOIN, ORDER BY, LIMIT, CTE, window definitions, column/table constraints, foreign keys, etc.

Child nodes are `LpNode*` pointers or `LpNodeList` (growable array). See `liteparser.h` for complete type definitions.

## Error Diagnostics

In tolerant mode, each `LpError` includes:

| Field | Type | Description |
|-------|------|-------------|
| `code` | `LpErrorCode` | `LP_ERR_SYNTAX`, `LP_ERR_ILLEGAL_TOKEN`, `LP_ERR_INCOMPLETE`, `LP_ERR_STACK_OVERFLOW` |
| `message` | `const char*` | Human-readable error message |
| `pos` | `LpSrcPos` | Start of error (line, col, offset) |
| `end_pos` | `LpSrcPos` | End of error range (exclusive) |

## JSON Output

See [JSON_SCHEMA.md](JSON_SCHEMA.md) for the complete JSON schema documentation.

---

## License

MIT License. See [LICENSE](LICENSE) file.

---

## Part of the SQLite AI Ecosystem

This project is part of the **SQLite AI** ecosystem, a collection of extensions that bring modern AI capabilities to the world’s most widely deployed database. The goal is to make SQLite the default data and inference engine for Edge AI applications.

Other projects in the ecosystem include:

- **[SQLite-AI](https://github.com/sqliteai/sqlite-ai)** — On-device inference and embedding generation directly inside SQLite.
- **[SQLite-Memory](https://github.com/sqliteai/sqlite-memory)** — Markdown-based AI agent memory with semantic search.
- **[SQLite-Vector](https://github.com/sqliteai/sqlite-vector)** — Ultra-efficient vector search for embeddings stored as BLOBs in standard SQLite tables.
- **[SQLite-Sync](https://github.com/sqliteai/sqlite-sync)** — Local-first CRDT-based synchronization for seamless, conflict-free data sync and real-time collaboration across devices.
- **[SQLite-Agent](https://github.com/sqliteai/sqlite-agent)** — Run autonomous AI agents directly from within SQLite databases.
- **[SQLite-MCP](https://github.com/sqliteai/sqlite-mcp)** — Connect SQLite databases to MCP servers and invoke their tools.
- **[SQLite-JS](https://github.com/sqliteai/sqlite-js)** — Create custom SQLite functions using JavaScript.
- **[Liteparser](https://github.com/sqliteai/liteparser)** — A highly efficient and fully compliant SQLite SQL parser.

Learn more at **[SQLite AI](https://sqlite.ai)**.
