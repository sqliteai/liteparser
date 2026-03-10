import type { AstNode, TolerantResult, ParseError, JsonOptions, LiteParserModule } from './types.js';

// @ts-expect-error — Emscripten-generated module, no types
import createModule from '../dist/liteparser.mjs';

const ARENA_BLOCK_SIZE = 64 * 1024;

/** Allocate a C string from a JS string, returning the pointer. Caller must free. */
function allocCString(mod: LiteParserModule, str: string): number {
  const len = mod.lengthBytesUTF8(str) + 1;
  const ptr = mod._malloc(len);
  if (ptr === 0) throw new Error('WASM malloc failed');
  mod.stringToUTF8(str, ptr, len);
  return ptr;
}

/** Allocate a zeroed pointer-sized slot for error output. Caller must free. */
function allocErrPtr(mod: LiteParserModule): number {
  const ptr = mod._malloc(4);
  if (ptr === 0) throw new Error('WASM malloc failed');
  mod.HEAPU32[ptr >>> 2] = 0;
  return ptr;
}

/** Read the error message from an error pointer, or return a default. */
function readErrMsg(mod: LiteParserModule, errPtr: number): string {
  const errMsgPtr = mod.HEAPU32[errPtr >>> 2];
  return errMsgPtr ? mod.UTF8ToString(errMsgPtr) : 'Parse error';
}

export interface LiteParser {
  /** Parse a single SQL statement, return AST as a JS object */
  parse(sql: string): AstNode;

  /** Parse multiple semicolon-separated statements */
  parseAll(sql: string): AstNode[];

  /** Tolerant parse: returns statements + errors (never throws) */
  parseTolerant(sql: string): TolerantResult;

  /** Parse a single statement and return raw JSON string */
  parseToJson(sql: string, options?: JsonOptions): string;

  /** Parse tolerant and return raw JSON string */
  parseTolerantToJson(sql: string, options?: JsonOptions): string;

  /** Parse SQL and convert AST back to SQL (round-trip) */
  unparse(sql: string): string;

  /** Get library version string */
  version(): string;

  /** Free all WASM resources. Instance is unusable after this. */
  destroy(): void;
}

class LiteParserImpl implements LiteParser {
  private mod: LiteParserModule;
  private arena: number;

  constructor(mod: LiteParserModule) {
    this.mod = mod;
    this.arena = mod._arena_create(ARENA_BLOCK_SIZE);
    if (this.arena === 0) throw new Error('Failed to create arena');
  }

  parse(sql: string): AstNode {
    const json = this.parseToJson(sql);
    return JSON.parse(json) as AstNode;
  }

  parseAll(sql: string): AstNode[] {
    const mod = this.mod;
    mod._arena_reset(this.arena);

    const sqlPtr = allocCString(mod, sql);
    try {
      // Use tolerant parse to get multi-statement JSON
      // (lp_parse_all returns LpNodeList* which can't be serialized directly)
      const resultPtr = mod._lp_parse_tolerant(sqlPtr, this.arena);
      const jsonPtr = mod._lp_parse_result_to_json(resultPtr, this.arena, 0);
      if (jsonPtr === 0) throw new Error('JSON serialization failed');

      const result = JSON.parse(mod.UTF8ToString(jsonPtr)) as { statements: AstNode[]; errors: ParseError[] };
      if (result.errors && result.errors.length > 0) {
        throw new SyntaxError(result.errors[0].message);
      }
      return result.statements;
    } finally {
      mod._free(sqlPtr);
    }
  }

  parseTolerant(sql: string): TolerantResult {
    const json = this.parseTolerantToJson(sql);
    const raw = JSON.parse(json) as { statements: AstNode[]; errors: ParseError[] };
    return { stmts: raw.statements, errors: raw.errors };
  }

  parseToJson(sql: string, options?: JsonOptions): string {
    const mod = this.mod;
    mod._arena_reset(this.arena);

    const sqlPtr = allocCString(mod, sql);
    const errPtr = allocErrPtr(mod);

    try {
      const nodePtr = mod._lp_parse(sqlPtr, this.arena, errPtr);
      if (nodePtr === 0) {
        throw new SyntaxError(readErrMsg(mod, errPtr));
      }

      const pretty = options?.pretty ? 1 : 0;
      const jsonPtr = mod._lp_ast_to_json(nodePtr, this.arena, pretty);
      if (jsonPtr === 0) throw new Error('JSON serialization failed');

      return mod.UTF8ToString(jsonPtr);
    } finally {
      mod._free(sqlPtr);
      mod._free(errPtr);
    }
  }

  parseTolerantToJson(sql: string, options?: JsonOptions): string {
    const mod = this.mod;
    mod._arena_reset(this.arena);

    const sqlPtr = allocCString(mod, sql);
    try {
      const resultPtr = mod._lp_parse_tolerant(sqlPtr, this.arena);
      if (resultPtr === 0) throw new Error('Tolerant parse failed');

      const pretty = options?.pretty ? 1 : 0;
      const jsonPtr = mod._lp_parse_result_to_json(resultPtr, this.arena, pretty);
      if (jsonPtr === 0) throw new Error('JSON serialization failed');

      return mod.UTF8ToString(jsonPtr);
    } finally {
      mod._free(sqlPtr);
    }
  }

  unparse(sql: string): string {
    const mod = this.mod;
    mod._arena_reset(this.arena);

    const sqlPtr = allocCString(mod, sql);
    const errPtr = allocErrPtr(mod);

    try {
      const nodePtr = mod._lp_parse(sqlPtr, this.arena, errPtr);
      if (nodePtr === 0) {
        throw new SyntaxError(readErrMsg(mod, errPtr));
      }

      const sqlOutPtr = mod._lp_ast_to_sql(nodePtr, this.arena);
      if (sqlOutPtr === 0) throw new Error('Unparse failed');

      return mod.UTF8ToString(sqlOutPtr);
    } finally {
      mod._free(sqlPtr);
      mod._free(errPtr);
    }
  }

  version(): string {
    return this.mod.UTF8ToString(this.mod._lp_version());
  }

  destroy(): void {
    if (this.arena !== 0) {
      this.mod._arena_destroy(this.arena);
      this.arena = 0;
    }
  }
}

/**
 * Create a new LiteParser instance.
 * Loads the WASM module and returns a ready-to-use parser.
 */
export async function createLiteParser(): Promise<LiteParser> {
  const mod = await createModule() as LiteParserModule;
  return new LiteParserImpl(mod);
}
