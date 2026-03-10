# liteparser API Reference

Complete reference for the liteparser public API. All functions, types, and constants are declared in `liteparser.h` and `arena.h`.

## Table of Contents

- [Memory Management](#memory-management)
- [Parsing](#parsing)
- [JSON Serialization](#json-serialization)
- [SQL Unparsing](#sql-unparsing)
- [AST Walking](#ast-walking)
- [AST Mutation](#ast-mutation)
- [AST Comparison & Counting](#ast-comparison--counting)
- [Parent Pointers](#parent-pointers)
- [Introspection](#introspection)
- [Error Diagnostics](#error-diagnostics)
- [Node Types](#node-types)
- [Constants & Enums](#constants--enums)

---

## Memory Management

All AST nodes and strings are allocated from an arena. The arena uses bump-pointer allocation for speed and frees all memory at once with `arena_destroy()`. No individual `free()` calls are needed.

### `arena_create`

```c
arena_t *arena_create(size_t block_size);
```

Create a new arena with the given initial block size. Returns an opaque arena pointer. Typical block size: `64 * 1024` (64 KB).

```c
arena_t *arena = arena_create(64 * 1024);
```

### `arena_destroy`

```c
void arena_destroy(arena_t *arena);
```

Free all memory owned by the arena, including all AST nodes, strings, and the arena itself. After this call, all pointers obtained from this arena are invalid.

```c
arena_destroy(arena);  // frees everything in one call
```

### `arena_reset`

```c
void arena_reset(arena_t *arena);
```

Reset the arena for reuse. Keeps the first memory block allocated, drops all others. After reset, all previously allocated pointers are invalid but the arena can be reused without creating a new one.

```c
arena_reset(arena);  // reuse the arena for another parse
LpNode *node = lp_parse("SELECT 2", arena, &err);
```

### `arena_alloc`

```c
void *arena_alloc(arena_t *arena, size_t size);
```

Allocate `size` bytes from the arena with default alignment. Returns a pointer to uninitialized memory.

```c
char *buf = arena_alloc(arena, 256);
```

### `arena_zeroalloc`

```c
void *arena_zeroalloc(arena_t *arena, size_t size);
```

Allocate `size` bytes from the arena, zero-initialized.

```c
int *counters = arena_zeroalloc(arena, 10 * sizeof(int));
// counters[0..9] are all zero
```

### `arena_strdup`

```c
char *arena_strdup(arena_t *arena, const char *s);
```

Duplicate a null-terminated string into the arena. Returns an arena-owned copy.

```c
char *name = arena_strdup(arena, "users");
```

### `arena_stats`

```c
void arena_stats(const arena_t *arena, size_t *used, size_t *capacity, size_t *blocks);
```

Query arena memory statistics. Sets `used` (bytes allocated), `capacity` (total bytes available), and `blocks` (number of memory blocks).

```c
size_t used, cap, blks;
arena_stats(arena, &used, &cap, &blks);
printf("Using %zu / %zu bytes in %zu blocks\n", used, cap, blks);
```

### `arena_debug`

```c
void arena_debug(const arena_t *arena);
```

Print arena statistics to stdout (for debugging).

---

## Parsing

Three parsing modes for different use cases: strict single-statement, strict multi-statement, and tolerant (IDE/linter).

### `lp_parse`

```c
LpNode *lp_parse(const char *sql, arena_t *arena, const char **error_msg);
```

Parse a single SQL statement. Returns the root AST node on success, `NULL` on error. If the input contains multiple statements, only the first is parsed.

On error, `*error_msg` is set to an arena-allocated error string.

```c
arena_t *arena = arena_create(64 * 1024);
const char *err = NULL;

LpNode *node = lp_parse("SELECT * FROM users WHERE id = 1", arena, &err);
if (!node) {
    fprintf(stderr, "Parse error: %s\n", err);
}

arena_destroy(arena);
```

### `lp_parse_all`

```c
LpNodeList *lp_parse_all(const char *sql, arena_t *arena, const char **error_msg);
```

Parse a SQL string containing multiple semicolon-separated statements. Returns an arena-allocated `LpNodeList` of statement nodes. On error, returns `NULL` and `*error_msg` describes the first error.

```c
const char *err = NULL;
LpNodeList *stmts = lp_parse_all("SELECT 1; SELECT 2; SELECT 3", arena, &err);
if (!stmts) {
    fprintf(stderr, "Error: %s\n", err);
} else {
    for (int i = 0; i < stmts->count; i++) {
        printf("Statement %d: kind=%s\n", i, lp_node_kind_name(stmts->items[i]->kind));
    }
}
```

### `lp_parse_tolerant`

```c
LpParseResult *lp_parse_tolerant(const char *sql, arena_t *arena);
```

Tolerant parse for IDE and linter use. Continues past syntax errors by skipping to the next semicolon, collecting all errors and all successfully parsed statements. **Never returns NULL.**

```c
LpParseResult *result = lp_parse_tolerant(
    "SELECT 1; INVALID GARBAGE; SELECT 2", arena);

printf("Parsed %d statements, %d errors\n",
       result->stmts.count, result->errors.count);

for (int i = 0; i < result->errors.count; i++) {
    LpError *e = &result->errors.items[i];
    printf("Error at line %u col %u: [%s] %s\n",
           e->pos.line, e->pos.col,
           lp_error_code_name(e->code), e->message);
}
```

---

## JSON Serialization

Convert AST nodes or full parse results to JSON strings.

### `lp_ast_to_json`

```c
char *lp_ast_to_json(LpNode *root, arena_t *arena, int pretty);
```

Serialize an AST node (and all its children) to a JSON string. Set `pretty` to non-zero for indented output, or zero for compact JSON. Returns an arena-allocated string, or `NULL` on error.

```c
LpNode *node = lp_parse("SELECT a, b FROM t", arena, &err);

// Pretty-printed JSON
char *json = lp_ast_to_json(node, arena, 1);
printf("%s\n", json);

// Compact JSON
char *compact = lp_ast_to_json(node, arena, 0);
```

### `lp_parse_result_to_json`

```c
char *lp_parse_result_to_json(LpParseResult *result, arena_t *arena, int pretty);
```

Serialize a tolerant parse result (statements + errors) to JSON. The output includes both the `"stmts"` array and the `"errors"` array. Returns an arena-allocated string, or `NULL` on error.

```c
LpParseResult *result = lp_parse_tolerant("SELECT 1; BAD; SELECT 2", arena);
char *json = lp_parse_result_to_json(result, arena, 1);
printf("%s\n", json);
```

---

## SQL Unparsing

Convert an AST back to SQL text. The unparsed SQL is semantically equivalent to the original -parse, unparse, reparse produces an identical AST.

### `lp_ast_to_sql`

```c
char *lp_ast_to_sql(LpNode *root, arena_t *arena);
```

Convert an AST node back to a SQL string. Returns an arena-allocated string, or `NULL` on error. Handles proper identifier quoting, operator spacing, and keyword formatting.

```c
LpNode *node = lp_parse("select a,b from t where x>5", arena, &err);
char *sql = lp_ast_to_sql(node, arena);
printf("%s\n", sql);
// Output: SELECT a, b FROM t WHERE x > 5
```

---

## AST Walking

Traverse the AST depth-first with enter/leave callbacks. Useful for analysis, transformation, code generation, and linting.

### `LpVisitor`

```c
struct LpVisitor {
    void *user_data;
    int (*enter)(LpVisitor *v, LpNode *node);
    int (*leave)(LpVisitor *v, LpNode *node);
};
```

The visitor struct. Set `user_data` to any context pointer. Callback return values:

| Return | `enter()` | `leave()` |
|--------|-----------|-----------|
| `0` | Continue -visit children | Continue |
| `1` | Skip children (don't descend) | *(unused)* |
| `2` | Abort traversal immediately | Abort traversal |

### `lp_ast_walk`

```c
void lp_ast_walk(LpNode *root, LpVisitor *visitor);
```

Walk the AST depth-first, calling `enter()` before visiting children and `leave()` after. Either callback can be `NULL`.

```c
// Count all column references in a query
typedef struct { int count; } CountCtx;

int count_enter(LpVisitor *v, LpNode *node) {
    if (node->kind == LP_EXPR_COLUMN_REF) {
        ((CountCtx *)v->user_data)->count++;
    }
    return 0;  // continue
}

CountCtx ctx = {0};
LpVisitor visitor = { .user_data = &ctx, .enter = count_enter, .leave = NULL };
lp_ast_walk(node, &visitor);
printf("Found %d column references\n", ctx.count);
```

```c
// Extract all table names from FROM clauses
int table_enter(LpVisitor *v, LpNode *node) {
    if (node->kind == LP_FROM_TABLE) {
        printf("Table: %s\n", node->u.from_table.name);
    }
    return 0;
}

LpVisitor visitor = { .user_data = NULL, .enter = table_enter };
lp_ast_walk(node, &visitor);
```

---

## AST Mutation

Programmatically create, modify, and clone AST nodes. All mutations are arena-allocated.

### `lp_node_alloc`

```c
LpNode *lp_node_alloc(arena_t *arena, LpNodeKind kind);
```

Allocate a new zero-initialized node of the given kind. The caller fills in the union fields.

```c
// Create a literal integer node
LpNode *lit = lp_node_alloc(arena, LP_EXPR_LITERAL_INT);
lit->u.literal.value = lp_strdup(arena, "42");
```

### `lp_node_clone`

```c
LpNode *lp_node_clone(arena_t *arena, const LpNode *node);
```

Deep-copy an AST node and all its children into the given arena. Useful for duplicating subtrees or moving nodes between arenas.

```c
// Clone a WHERE clause into a new arena
arena_t *arena2 = arena_create(64 * 1024);
LpNode *where_copy = lp_node_clone(arena2, node->u.select.where);
// where_copy is independent -original arena can be destroyed
```

### `lp_strdup`

```c
char *lp_strdup(arena_t *arena, const char *s);
```

Duplicate a string into the arena. Use when setting string fields on nodes.

```c
LpNode *tbl = lp_node_alloc(arena, LP_FROM_TABLE);
tbl->u.from_table.name = lp_strdup(arena, "users");
tbl->u.from_table.alias = lp_strdup(arena, "u");
```

### `lp_list_push`

```c
void lp_list_push(arena_t *arena, LpNodeList *list, LpNode *item);
```

Append a node to the end of a node list. The list grows automatically using arena-allocated arrays.

```c
// Add a result column to a SELECT
LpNode *col = lp_node_alloc(arena, LP_RESULT_COLUMN);
col->u.result_column.expr = my_expr;
col->u.result_column.alias = lp_strdup(arena, "total");
lp_list_push(arena, &node->u.select.result_columns, col);
```

### `lp_list_insert`

```c
void lp_list_insert(arena_t *arena, LpNodeList *list, int index, LpNode *item);
```

Insert a node at position `index`. Items at `index..count-1` are shifted right. Index is clamped to `[0, count]`.

```c
// Insert a new column at the beginning of the SELECT list
lp_list_insert(arena, &node->u.select.result_columns, 0, new_col);
```

### `lp_list_replace`

```c
LpNode *lp_list_replace(LpNodeList *list, int index, LpNode *new_item);
```

Replace the node at position `index`. Returns the old node, or `NULL` if index is out of bounds.

```c
// Replace the first result column
LpNode *old = lp_list_replace(&node->u.select.result_columns, 0, new_col);
```

### `lp_list_remove`

```c
LpNode *lp_list_remove(LpNodeList *list, int index);
```

Remove the node at position `index`, shifting subsequent items left. Returns the removed node, or `NULL` if index is out of bounds. The removed node's memory remains in the arena.

```c
// Remove the second ORDER BY term
LpNode *removed = lp_list_remove(&node->u.select.order_by, 1);
```

---

## AST Comparison & Counting

### `lp_version`

```c
const char *lp_version(void);
```

Return the library version string (e.g., `"1.0.0"`). Useful for runtime version checks and FFI bindings where the `LITEPARSER_VERSION` macro is not accessible.

```c
printf("liteparser %s\n", lp_version());
```

### `lp_node_count`

```c
int lp_node_count(const LpNode *node);
```

Count the total number of nodes in the AST subtree rooted at `node`. Returns 0 if `node` is NULL. Uses depth-first traversal.

```c
LpNode *ast = lp_parse("SELECT a, b FROM t WHERE x > 1", arena, &err);
printf("Node count: %d\n", lp_node_count(ast));
// Output: Node count: 9
```

### `lp_node_equal`

```c
int lp_node_equal(const LpNode *a, const LpNode *b);
```

Deep structural equality comparison. Returns 1 if `a` and `b` have identical kinds and all child nodes/fields match recursively. Source positions (`pos`) and parent pointers are **ignored** — only structural content is compared. Both `NULL` returns 1. One `NULL` returns 0.

```c
arena_t *a1 = arena_create(64 * 1024);
arena_t *a2 = arena_create(64 * 1024);
LpNode *n1 = lp_parse("SELECT a FROM t", a1, &err);
LpNode *n2 = lp_parse("SELECT a FROM t", a2, &err);
printf("Equal: %d\n", lp_node_equal(n1, n2));  // 1

LpNode *n3 = lp_parse("SELECT b FROM t", a2, &err);
printf("Equal: %d\n", lp_node_equal(n1, n3));  // 0

// Clone equals original
LpNode *cloned = lp_node_clone(a2, n1);
printf("Equal: %d\n", lp_node_equal(n1, cloned));  // 1
```

---

## Parent Pointers

Every `LpNode` has a `parent` field that points to its owning node in the AST. Root nodes have `parent == NULL`.

Parent pointers are set automatically by the parse functions (`lp_parse`, `lp_parse_all`, `lp_parse_tolerant`) and by `lp_node_clone`. After using the mutation API (`lp_list_push`, `lp_list_insert`, etc.), call `lp_fix_parents` to refresh parent pointers.

### `LpNode.parent`

```c
struct LpNode {
    LpNodeKind kind;
    LpSrcPos   pos;
    LpNode    *parent;   // NULL for root nodes
    union { ... } u;
};
```

```c
LpNode *ast = lp_parse("SELECT a FROM t WHERE x = 1", arena, &err);
assert(ast->parent == NULL);                          // root
assert(ast->u.select.where->parent == ast);           // WHERE -> SELECT
assert(ast->u.select.from->parent == ast);            // FROM -> SELECT

// Walk up to root
LpNode *leaf = ast->u.select.where->u.binary.left;   // column ref 'x'
LpNode *p = leaf;
while (p->parent) p = p->parent;
assert(p == ast);  // reached root
```

### `lp_fix_parents`

```c
void lp_fix_parents(LpNode *root);
```

Set parent pointers throughout an AST. Walks the tree depth-first, setting each child's `parent` to its owning node. The root's `parent` is set to `NULL`.

Called automatically after parsing and cloning. Call manually after mutation:

```c
// After modifying the AST with mutation API
lp_list_push(arena, &ast->u.select.result_columns, new_col);
lp_fix_parents(ast);  // refresh all parent pointers
assert(new_col->parent == ast);
```

---

## Introspection

Query node metadata: kind names, operator names, and source positions.

### `lp_node_kind_name`

```c
const char *lp_node_kind_name(LpNodeKind kind);
```

Return the string name for a node kind (e.g., `"STMT_SELECT"`, `"EXPR_FUNCTION"`).

```c
printf("Node type: %s\n", lp_node_kind_name(node->kind));
// Output: Node type: STMT_SELECT
```

### `lp_binop_name`

```c
const char *lp_binop_name(LpBinOp op);
```

Return the string name for a binary operator (e.g., `"ADD"`, `"AND"`, `"LIKE"`).

```c
if (node->kind == LP_EXPR_BINARY_OP) {
    printf("Operator: %s\n", lp_binop_name(node->u.binary.op));
}
```

### `lp_unaryop_name`

```c
const char *lp_unaryop_name(LpUnaryOp op);
```

Return the string name for a unary operator (e.g., `"MINUS"`, `"NOT"`, `"BITNOT"`).

```c
if (node->kind == LP_EXPR_UNARY_OP) {
    printf("Unary op: %s\n", lp_unaryop_name(node->u.unary.op));
}
```

### `lp_node_source`

```c
const char *lp_node_source(const LpNode *node, const char *sql, unsigned int *len);
```

Return a pointer into the original SQL string at the position of the node's leading token. Sets `*len` to the token length. Returns `NULL` if position info is unavailable.

```c
const char *sql = "SELECT foo FROM bar";
LpNode *node = lp_parse(sql, arena, &err);

// Get source text of the FROM table
unsigned int len;
const char *src = lp_node_source(node->u.select.from, sql, &len);
printf("Source: %.*s\n", len, src);  // Output: bar
```

---

## Error Diagnostics

Structured error information from tolerant parsing.

### `LpErrorCode`

```c
typedef enum {
    LP_ERR_SYNTAX,          // grammar-level syntax error
    LP_ERR_ILLEGAL_TOKEN,   // unrecognized token from lexer
    LP_ERR_INCOMPLETE,      // unexpected end of input
    LP_ERR_STACK_OVERFLOW   // parser stack overflow
} LpErrorCode;
```

### `LpError`

```c
typedef struct {
    LpSrcPos    pos;        // start of error range
    LpSrcPos    end_pos;    // end of error range (exclusive)
    LpErrorCode code;       // error category
    const char *message;    // arena-allocated error message
} LpError;
```

### `lp_error_code_name`

```c
const char *lp_error_code_name(LpErrorCode code);
```

Return the string name of an error code (e.g., `"syntax"`, `"illegal_token"`, `"incomplete"`, `"stack_overflow"`).

```c
LpParseResult *result = lp_parse_tolerant("SELECT; DROP", arena);
for (int i = 0; i < result->errors.count; i++) {
    LpError *e = &result->errors.items[i];
    printf("[%s] line %u:%u -%s\n",
           lp_error_code_name(e->code),
           e->pos.line, e->pos.col, e->message);
}
```

### `LpParseResult`

```c
typedef struct {
    LpNodeList  stmts;      // successfully parsed statements
    LpErrorList errors;     // errors encountered
} LpParseResult;
```

Returned by `lp_parse_tolerant()`. Both fields may be non-empty simultaneously (some statements parsed, some failed).

---

## Node Types

### `LpNodeKind` -All Node Kinds

**Statements:**

| Kind | Description |
|------|-------------|
| `LP_STMT_SELECT` | SELECT statement |
| `LP_STMT_INSERT` | INSERT statement |
| `LP_STMT_UPDATE` | UPDATE statement |
| `LP_STMT_DELETE` | DELETE statement |
| `LP_STMT_CREATE_TABLE` | CREATE TABLE |
| `LP_STMT_CREATE_INDEX` | CREATE INDEX |
| `LP_STMT_CREATE_VIEW` | CREATE VIEW |
| `LP_STMT_CREATE_TRIGGER` | CREATE TRIGGER |
| `LP_STMT_CREATE_VTABLE` | CREATE VIRTUAL TABLE |
| `LP_STMT_DROP` | DROP TABLE/INDEX/VIEW/TRIGGER |
| `LP_STMT_BEGIN` | BEGIN TRANSACTION |
| `LP_STMT_COMMIT` | COMMIT / END |
| `LP_STMT_ROLLBACK` | ROLLBACK |
| `LP_STMT_SAVEPOINT` | SAVEPOINT |
| `LP_STMT_RELEASE` | RELEASE SAVEPOINT |
| `LP_STMT_ROLLBACK_TO` | ROLLBACK TO SAVEPOINT |
| `LP_STMT_PRAGMA` | PRAGMA |
| `LP_STMT_VACUUM` | VACUUM |
| `LP_STMT_REINDEX` | REINDEX |
| `LP_STMT_ANALYZE` | ANALYZE |
| `LP_STMT_ATTACH` | ATTACH DATABASE |
| `LP_STMT_DETACH` | DETACH DATABASE |
| `LP_STMT_ALTER` | ALTER TABLE |
| `LP_STMT_EXPLAIN` | EXPLAIN / EXPLAIN QUERY PLAN |

**Expressions:**

| Kind | Description |
|------|-------------|
| `LP_EXPR_LITERAL_INT` | Integer literal |
| `LP_EXPR_LITERAL_FLOAT` | Float literal |
| `LP_EXPR_LITERAL_STRING` | String literal |
| `LP_EXPR_LITERAL_BLOB` | Blob literal (X'...') |
| `LP_EXPR_LITERAL_NULL` | NULL |
| `LP_EXPR_LITERAL_BOOL` | TRUE / FALSE |
| `LP_EXPR_COLUMN_REF` | Column reference (with optional table/schema) |
| `LP_EXPR_BINARY_OP` | Binary operation |
| `LP_EXPR_UNARY_OP` | Unary operation |
| `LP_EXPR_FUNCTION` | Function call (including aggregate, CURRENT_TIME, etc.) |
| `LP_EXPR_CAST` | CAST expression |
| `LP_EXPR_COLLATE` | COLLATE expression |
| `LP_EXPR_BETWEEN` | BETWEEN expression |
| `LP_EXPR_IN` | IN expression |
| `LP_EXPR_EXISTS` | EXISTS subquery |
| `LP_EXPR_SUBQUERY` | Scalar subquery |
| `LP_EXPR_CASE` | CASE expression |
| `LP_EXPR_RAISE` | RAISE function (triggers) |
| `LP_EXPR_VARIABLE` | Bind variable (?, :name, @name, $name) |
| `LP_EXPR_STAR` | Star expression (*, table.*) |
| `LP_EXPR_VECTOR` | Row value / vector expression |

**Clauses & Sub-structures:**

| Kind | Description |
|------|-------------|
| `LP_COMPOUND_SELECT` | UNION / INTERSECT / EXCEPT |
| `LP_RESULT_COLUMN` | Result column (expr AS alias) |
| `LP_FROM_TABLE` | Table reference in FROM |
| `LP_FROM_SUBQUERY` | Subquery in FROM |
| `LP_JOIN_CLAUSE` | JOIN clause |
| `LP_ORDER_TERM` | ORDER BY term |
| `LP_LIMIT` | LIMIT / OFFSET |
| `LP_COLUMN_DEF` | Column definition |
| `LP_COLUMN_CONSTRAINT` | Column constraint |
| `LP_TABLE_CONSTRAINT` | Table constraint |
| `LP_FOREIGN_KEY` | Foreign key clause |
| `LP_CTE` | Common table expression |
| `LP_WITH` | WITH clause |
| `LP_UPSERT` | ON CONFLICT (upsert) |
| `LP_RETURNING` | RETURNING clause |
| `LP_WINDOW_DEF` | Window definition |
| `LP_WINDOW_FRAME` | Window frame specification |
| `LP_FRAME_BOUND` | Frame bound (ROWS/RANGE/GROUPS) |
| `LP_SET_CLAUSE` | SET clause (UPDATE) |
| `LP_INDEX_COLUMN` | Index column definition |
| `LP_VALUES_ROW` | VALUES row |
| `LP_TRIGGER_CMD` | Trigger body command |

---

## Constants & Enums

### Transaction Types

| Constant | Value |
|----------|-------|
| `LP_TRANS_DEFERRED` | 0 |
| `LP_TRANS_IMMEDIATE` | 1 |
| `LP_TRANS_EXCLUSIVE` | 2 |

### Conflict Resolution

| Constant | Value |
|----------|-------|
| `LP_CONFLICT_NONE` | 0 |
| `LP_CONFLICT_ROLLBACK` | 1 |
| `LP_CONFLICT_ABORT` | 2 |
| `LP_CONFLICT_FAIL` | 3 |
| `LP_CONFLICT_IGNORE` | 4 |
| `LP_CONFLICT_REPLACE` | 5 |

### Sort Order

| Constant | Value |
|----------|-------|
| `LP_SORT_ASC` | 0 |
| `LP_SORT_DESC` | 1 |
| `LP_SORT_UNDEFINED` | -1 |

### Join Types (bitmask)

| Constant | Value |
|----------|-------|
| `LP_JOIN_INNER` | 0x01 |
| `LP_JOIN_CROSS` | 0x02 |
| `LP_JOIN_NATURAL` | 0x04 |
| `LP_JOIN_LEFT` | 0x08 |
| `LP_JOIN_RIGHT` | 0x10 |
| `LP_JOIN_OUTER` | 0x20 |
| `LP_JOIN_FULL` | 0x40 |

### Table Options (bitmask)

| Constant | Value |
|----------|-------|
| `LP_TBL_WITHOUT_ROWID` | 0x01 |
| `LP_TBL_STRICT` | 0x02 |

### Binary Operators (`LpBinOp`)

`LP_OP_ADD`, `LP_OP_SUB`, `LP_OP_MUL`, `LP_OP_DIV`, `LP_OP_MOD`, `LP_OP_AND`, `LP_OP_OR`, `LP_OP_EQ`, `LP_OP_NE`, `LP_OP_LT`, `LP_OP_LE`, `LP_OP_GT`, `LP_OP_GE`, `LP_OP_BITAND`, `LP_OP_BITOR`, `LP_OP_LSHIFT`, `LP_OP_RSHIFT`, `LP_OP_CONCAT`, `LP_OP_IS`, `LP_OP_ISNOT`, `LP_OP_LIKE`, `LP_OP_GLOB`, `LP_OP_MATCH`, `LP_OP_REGEXP`, `LP_OP_PTR`, `LP_OP_PTR2`

### Unary Operators (`LpUnaryOp`)

`LP_UOP_MINUS`, `LP_UOP_PLUS`, `LP_UOP_NOT`, `LP_UOP_BITNOT`

### ALTER TABLE Types (`LpAlterType`)

`LP_ALTER_RENAME_TABLE`, `LP_ALTER_ADD_COLUMN`, `LP_ALTER_DROP_COLUMN`, `LP_ALTER_RENAME_COLUMN`

### Drop Target Types (`LpDropType`)

`LP_DROP_TABLE`, `LP_DROP_INDEX`, `LP_DROP_VIEW`, `LP_DROP_TRIGGER`

### Column Constraint Types (`LpColConsType`)

`LP_CCONS_PRIMARY_KEY`, `LP_CCONS_NOT_NULL`, `LP_CCONS_UNIQUE`, `LP_CCONS_CHECK`, `LP_CCONS_DEFAULT`, `LP_CCONS_REFERENCES`, `LP_CCONS_COLLATE`, `LP_CCONS_GENERATED`, `LP_CCONS_NULL`

### Table Constraint Types (`LpTableConsType`)

`LP_TCONS_PRIMARY_KEY`, `LP_TCONS_UNIQUE`, `LP_TCONS_CHECK`, `LP_TCONS_FOREIGN_KEY`

### Foreign Key Actions (`LpFKAction`)

`LP_FK_NO_ACTION`, `LP_FK_SET_NULL`, `LP_FK_SET_DEFAULT`, `LP_FK_CASCADE`, `LP_FK_RESTRICT`

### Trigger Timing (`LpTriggerTime`)

`LP_TRIGGER_BEFORE`, `LP_TRIGGER_AFTER`, `LP_TRIGGER_INSTEAD_OF`

### Trigger Events (`LpTriggerEvent`)

`LP_TRIGGER_INSERT`, `LP_TRIGGER_UPDATE`, `LP_TRIGGER_DELETE`

### Window Frame Type (`LpFrameType`)

`LP_FRAME_ROWS`, `LP_FRAME_RANGE`, `LP_FRAME_GROUPS`

### Frame Bound Type (`LpBoundType`)

`LP_BOUND_CURRENT_ROW`, `LP_BOUND_UNBOUNDED_PRECEDING`, `LP_BOUND_UNBOUNDED_FOLLOWING`, `LP_BOUND_PRECEDING`, `LP_BOUND_FOLLOWING`

### Frame Exclude (`LpExcludeType`)

`LP_EXCLUDE_NONE`, `LP_EXCLUDE_NO_OTHERS`, `LP_EXCLUDE_CURRENT_ROW`, `LP_EXCLUDE_GROUP`, `LP_EXCLUDE_TIES`

### CTE Materialization (`LpMaterialized`)

`LP_MATERIALIZE_ANY`, `LP_MATERIALIZE_YES`, `LP_MATERIALIZE_NO`

### RAISE Type (`LpRaiseType`)

`LP_RAISE_IGNORE`, `LP_RAISE_ROLLBACK`, `LP_RAISE_ABORT`, `LP_RAISE_FAIL`

### Compound Operators (`LpCompoundOp`)

`LP_COMPOUND_UNION`, `LP_COMPOUND_UNION_ALL`, `LP_COMPOUND_INTERSECT`, `LP_COMPOUND_EXCEPT`
