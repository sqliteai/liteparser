# JSON Schema Reference

This documents the JSON output of `lp_ast_to_json()` and `lp_parse_result_to_json()`.

## Common Structure

Every AST node serializes as a JSON object with:

```json
{
  "kind": "STMT_SELECT",
  "pos": {"line": 1, "col": 1, "offset": 0},
  ...
}
```

- `kind` (string): Node type name (e.g., `"STMT_SELECT"`, `"EXPR_LITERAL_INT"`)
- `pos` (object, optional): Source position — omitted if line is 0

Optional/NULL fields are omitted from output. Empty lists are omitted.

## Tolerant Parse Result

`lp_parse_result_to_json()` produces:

```json
{
  "statements": [ ... ],
  "errors": [
    {
      "code": "syntax",
      "message": "1:5: near \"GARBAGE\": syntax error",
      "pos": {"line": 1, "col": 5, "offset": 4},
      "end_pos": {"line": 1, "col": 12, "offset": 11}
    }
  ]
}
```

Error codes: `"syntax"`, `"illegal_token"`, `"incomplete"`, `"stack_overflow"`.

## Statements

### STMT_SELECT

```json
{
  "kind": "STMT_SELECT",
  "distinct": false,
  "result_columns": [...],
  "from": { ... },
  "where": { ... },
  "group_by": [...],
  "having": { ... },
  "window_defs": [...],
  "order_by": [...],
  "limit": { ... },
  "with": { ... }
}
```

### COMPOUND_SELECT

```json
{
  "kind": "COMPOUND_SELECT",
  "op": "UNION ALL",
  "left": { ... },
  "right": { ... }
}
```

Operators: `"UNION"`, `"UNION ALL"`, `"INTERSECT"`, `"EXCEPT"`.

### STMT_INSERT

```json
{
  "kind": "STMT_INSERT",
  "table": "users",
  "schema": "main",
  "alias": "u",
  "or_conflict": "REPLACE",
  "columns": [...],
  "source": { ... },
  "upsert": { ... },
  "returning": [...]
}
```

`or_conflict`: `"ABORT"`, `"FAIL"`, `"IGNORE"`, `"REPLACE"`, `"ROLLBACK"`, or omitted.

### STMT_UPDATE

```json
{
  "kind": "STMT_UPDATE",
  "table": "users",
  "schema": "main",
  "alias": "u",
  "or_conflict": "IGNORE",
  "set_clauses": [...],
  "from": { ... },
  "where": { ... },
  "order_by": [...],
  "limit": { ... },
  "returning": [...]
}
```

### STMT_DELETE

```json
{
  "kind": "STMT_DELETE",
  "table": "users",
  "schema": "main",
  "alias": "u",
  "where": { ... },
  "order_by": [...],
  "limit": { ... },
  "returning": [...]
}
```

### STMT_CREATE_TABLE

```json
{
  "kind": "STMT_CREATE_TABLE",
  "name": "users",
  "schema": "main",
  "if_not_exists": false,
  "temp": false,
  "options": 0,
  "columns": [...],
  "constraints": [...],
  "as_select": { ... }
}
```

`options`: bitmask — 1 = WITHOUT ROWID, 2 = STRICT.

### STMT_CREATE_INDEX

```json
{
  "kind": "STMT_CREATE_INDEX",
  "name": "idx_email",
  "schema": "main",
  "table": "users",
  "is_unique": true,
  "if_not_exists": false,
  "columns": [...],
  "where": { ... }
}
```

### STMT_CREATE_VIEW

```json
{
  "kind": "STMT_CREATE_VIEW",
  "name": "active_users",
  "schema": "main",
  "if_not_exists": false,
  "temp": false,
  "col_names": [...],
  "select": { ... }
}
```

### STMT_CREATE_TRIGGER

```json
{
  "kind": "STMT_CREATE_TRIGGER",
  "name": "trg_audit",
  "schema": "main",
  "if_not_exists": false,
  "temp": false,
  "time": "BEFORE",
  "event": "INSERT",
  "table_name": "users",
  "update_columns": [...],
  "when": { ... },
  "body": [...]
}
```

`time`: `"BEFORE"`, `"AFTER"`, `"INSTEAD OF"`.
`event`: `"INSERT"`, `"DELETE"`, `"UPDATE"`.

### STMT_CREATE_VTABLE

```json
{
  "kind": "STMT_CREATE_VTABLE",
  "name": "docs",
  "schema": "main",
  "if_not_exists": false,
  "module": "fts5",
  "module_args": "title, body, content='documents'"
}
```

### STMT_DROP

```json
{
  "kind": "STMT_DROP",
  "target": "TABLE",
  "name": "users",
  "schema": "main",
  "if_exists": true
}
```

`target`: `"TABLE"`, `"INDEX"`, `"VIEW"`, `"TRIGGER"`.

### STMT_BEGIN

```json
{"kind": "STMT_BEGIN", "trans_type": "IMMEDIATE"}
```

`trans_type`: `"DEFERRED"`, `"IMMEDIATE"`, `"EXCLUSIVE"`, or `""`.

### STMT_COMMIT, STMT_ROLLBACK

```json
{"kind": "STMT_COMMIT"}
{"kind": "STMT_ROLLBACK"}
```

### STMT_SAVEPOINT, STMT_RELEASE, STMT_ROLLBACK_TO

```json
{"kind": "STMT_SAVEPOINT", "name": "sp1"}
```

### STMT_PRAGMA

```json
{
  "kind": "STMT_PRAGMA",
  "name": "journal_mode",
  "schema": "main",
  "value": "WAL"
}
```

### STMT_VACUUM

```json
{"kind": "STMT_VACUUM", "schema": "main", "into": { ... }}
```

### STMT_REINDEX, STMT_ANALYZE

```json
{"kind": "STMT_REINDEX", "name": "users", "schema": "main"}
```

### STMT_ATTACH

```json
{
  "kind": "STMT_ATTACH",
  "filename": { ... },
  "dbname": { ... },
  "key": { ... }
}
```

### STMT_DETACH

```json
{"kind": "STMT_DETACH", "dbname": { ... }}
```

### STMT_ALTER

```json
{
  "kind": "STMT_ALTER",
  "alter_type": "RENAME_TABLE",
  "table_name": "users",
  "schema": "main",
  "new_name": "people",
  "column_name": "old_col",
  "column_def": { ... }
}
```

`alter_type`: `"RENAME_TABLE"`, `"ADD_COLUMN"`, `"DROP_COLUMN"`, `"RENAME_COLUMN"`.

### STMT_EXPLAIN

```json
{
  "kind": "STMT_EXPLAIN",
  "is_query_plan": true,
  "stmt": { ... }
}
```

## Expressions

### EXPR_LITERAL_INT, EXPR_LITERAL_FLOAT, EXPR_LITERAL_STRING, EXPR_LITERAL_BLOB, EXPR_LITERAL_BOOL

```json
{"kind": "EXPR_LITERAL_INT", "value": "42"}
{"kind": "EXPR_LITERAL_STRING", "value": "hello"}
```

### EXPR_LITERAL_NULL

```json
{"kind": "EXPR_LITERAL_NULL"}
```

### EXPR_COLUMN_REF

```json
{"kind": "EXPR_COLUMN_REF", "column": "id", "table": "users", "schema": "main"}
```

### EXPR_BINARY_OP

```json
{
  "kind": "EXPR_BINARY_OP",
  "op": "ADD",
  "left": { ... },
  "right": { ... },
  "escape": { ... }
}
```

`escape` is only present for LIKE/GLOB operations.

### EXPR_UNARY_OP

```json
{"kind": "EXPR_UNARY_OP", "op": "MINUS", "operand": { ... }}
```

### EXPR_FUNCTION

```json
{
  "kind": "EXPR_FUNCTION",
  "name": "COUNT",
  "distinct": false,
  "args": [...],
  "order_by": [...],
  "filter": { ... },
  "over": { ... }
}
```

### EXPR_CAST

```json
{"kind": "EXPR_CAST", "expr": { ... }, "type_name": "TEXT"}
```

### EXPR_COLLATE

```json
{"kind": "EXPR_COLLATE", "expr": { ... }, "collation": "NOCASE"}
```

### EXPR_BETWEEN

```json
{"kind": "EXPR_BETWEEN", "is_not": false, "expr": { ... }, "low": { ... }, "high": { ... }}
```

### EXPR_IN

```json
{"kind": "EXPR_IN", "is_not": false, "expr": { ... }, "values": [...], "select": { ... }}
```

Either `values` or `select` is present, not both.

### EXPR_EXISTS

```json
{"kind": "EXPR_EXISTS", "select": { ... }}
```

### EXPR_SUBQUERY

```json
{"kind": "EXPR_SUBQUERY", "select": { ... }}
```

### EXPR_CASE

```json
{
  "kind": "EXPR_CASE",
  "operand": { ... },
  "when_exprs": [...],
  "else_expr": { ... }
}
```

### EXPR_RAISE

```json
{"kind": "EXPR_RAISE", "type": "ABORT", "message": { ... }}
```

### EXPR_VARIABLE

```json
{"kind": "EXPR_VARIABLE", "name": "?1"}
```

### EXPR_STAR

```json
{"kind": "EXPR_STAR", "table": "t"}
```

### EXPR_VECTOR

```json
{"kind": "EXPR_VECTOR", "values": [...]}
```

## Clauses

### RESULT_COLUMN

```json
{"kind": "RESULT_COLUMN", "expr": { ... }, "alias": "total"}
```

### FROM_TABLE

```json
{
  "kind": "FROM_TABLE",
  "name": "users",
  "schema": "main",
  "alias": "u",
  "indexed_by": "idx_email",
  "not_indexed": true,
  "func_args": [...]
}
```

### FROM_SUBQUERY

```json
{"kind": "FROM_SUBQUERY", "select": { ... }, "alias": "sub"}
```

### JOIN_CLAUSE

```json
{
  "kind": "JOIN_CLAUSE",
  "join_type": "LEFT",
  "left": { ... },
  "right": { ... },
  "on_expr": { ... },
  "using_columns": [...]
}
```

`join_type`: `"INNER"`, `"LEFT"`, `"RIGHT"`, `"FULL"`, `"CROSS"`, `"NATURAL"`, etc.

### ORDER_TERM

```json
{"kind": "ORDER_TERM", "expr": { ... }, "direction": "DESC", "nulls": "FIRST"}
```

### LIMIT

```json
{"kind": "LIMIT", "count": { ... }, "offset": { ... }}
```

### COLUMN_DEF

```json
{"kind": "COLUMN_DEF", "name": "id", "type_name": "INTEGER", "constraints": [...]}
```

### COLUMN_CONSTRAINT

```json
{
  "kind": "COLUMN_CONSTRAINT",
  "constraint_type": "PRIMARY_KEY",
  "name": "pk_id",
  "sort_order": "ASC",
  "conflict_action": "ABORT",
  "is_autoinc": true,
  "expr": { ... },
  "fk": { ... },
  "collation": "NOCASE",
  "generated_type": "STORED"
}
```

### TABLE_CONSTRAINT

```json
{
  "kind": "TABLE_CONSTRAINT",
  "constraint_type": "PRIMARY_KEY",
  "name": "pk_composite",
  "columns": [...],
  "expr": { ... },
  "fk": { ... },
  "conflict_action": "ABORT",
  "is_autoinc": false
}
```

### FOREIGN_KEY

```json
{
  "kind": "FOREIGN_KEY",
  "table": "other",
  "columns": [...],
  "on_delete": "CASCADE",
  "on_update": "SET NULL",
  "deferrable": "DEFERRED"
}
```

### CTE

```json
{
  "kind": "CTE",
  "name": "cte1",
  "columns": [...],
  "select": { ... },
  "materialized": "MATERIALIZED"
}
```

### WITH

```json
{"kind": "WITH", "recursive": true, "ctes": [...]}
```

### UPSERT

```json
{
  "kind": "UPSERT",
  "conflict_target": [...],
  "conflict_where": { ... },
  "set_clauses": [...],
  "where": { ... },
  "next": { ... }
}
```

### RETURNING

```json
{"kind": "RETURNING", "columns": [...]}
```

### WINDOW_DEF

```json
{
  "kind": "WINDOW_DEF",
  "name": "w1",
  "base_name": "w_base",
  "partition_by": [...],
  "order_by": [...],
  "frame": { ... }
}
```

### WINDOW_FRAME

```json
{
  "kind": "WINDOW_FRAME",
  "frame_type": "ROWS",
  "start": { ... },
  "end": { ... },
  "exclude": "CURRENT ROW"
}
```

`frame_type`: `"ROWS"`, `"RANGE"`, `"GROUPS"`.

### FRAME_BOUND

```json
{"kind": "FRAME_BOUND", "bound_type": "PRECEDING", "expr": { ... }}
```

### SET_CLAUSE

```json
{"kind": "SET_CLAUSE", "column": "name", "columns": [...], "expr": { ... }}
```

### INDEX_COLUMN

```json
{"kind": "INDEX_COLUMN", "expr": { ... }, "collation": "NOCASE", "sort_order": "ASC"}
```

### VALUES_ROW

```json
{"kind": "VALUES_ROW", "values": [...]}
```

### TRIGGER_CMD

```json
{"kind": "TRIGGER_CMD", "stmt": { ... }}
```
