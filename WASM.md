# WebAssembly (WASM)

liteparser compiles to WebAssembly for use in browsers, Node.js, and any JavaScript runtime. The WASM build produces a self-contained ES module with the binary inlined (no separate `.wasm` file to load).

## Building the WASM Module

Requires [Emscripten](https://emscripten.org/):

```bash
make wasm          # Produces wasm/dist/liteparser.mjs
```

## Installing the npm Package

```bash
cd wasm
npm install        # Install dev dependencies
npm run build      # Build WASM + TypeScript wrapper
npm test           # Run test suite
```

## JavaScript API

```typescript
import { createLiteParser } from '@sqliteai/liteparser';

const parser = await createLiteParser();

// Parse a single statement → JS object
const ast = parser.parse('SELECT * FROM users WHERE age > 18');
console.log(ast.kind);       // "STMT_SELECT"
console.log(ast.where);      // { kind: "EXPR_BINARY_OP", op: ">", ... }

// Parse multiple statements
const stmts = parser.parseAll('SELECT 1; SELECT 2; SELECT 3');
console.log(stmts.length);   // 3

// Tolerant parse (IDE mode) — never throws, returns errors alongside valid statements
const result = parser.parseTolerant('SELECT 1; GARBAGE; SELECT 2');
console.log(result.stmts);   // 2 valid statements
console.log(result.errors);  // [{ code: "syntax", message: "...", pos: {...} }]

// Get raw JSON string (avoids JSON.parse overhead if forwarding)
const json = parser.parseToJson('SELECT 1', { pretty: true });

// Round-trip: parse SQL → AST → SQL
const sql = parser.unparse('SELECT  a ,  b   FROM  t');
console.log(sql);            // "SELECT a, b FROM t;"

// Library version
console.log(parser.version()); // "1.0.0"

// Free WASM resources when done
parser.destroy();
```

## API Summary

| Method | Returns | Description |
|--------|---------|-------------|
| `parse(sql)` | `AstNode` | Parse a single statement, throw on error |
| `parseAll(sql)` | `AstNode[]` | Parse multiple semicolon-separated statements |
| `parseTolerant(sql)` | `{ stmts, errors }` | Parse with error recovery (never throws) |
| `parseToJson(sql, opts?)` | `string` | Parse to raw JSON string |
| `parseTolerantToJson(sql, opts?)` | `string` | Tolerant parse to raw JSON string |
| `unparse(sql)` | `string` | Parse and reconstruct SQL from AST |
| `version()` | `string` | Library version |
| `destroy()` | `void` | Free all WASM resources |
