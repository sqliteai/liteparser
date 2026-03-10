/** Source position in the SQL input */
export interface SrcPos {
  offset: number;
  line: number;
  col: number;
}

/** A single parse error */
export interface ParseError {
  code: 'syntax' | 'illegal_token' | 'incomplete' | 'stack_overflow';
  message: string;
  pos: SrcPos;
  end_pos: SrcPos;
}

/** Result from tolerant parsing */
export interface TolerantResult {
  stmts: AstNode[];
  errors: ParseError[];
}

/** A node in the AST (JSON representation) */
export type AstNode = Record<string, unknown>;

/** Options for JSON output */
export interface JsonOptions {
  pretty?: boolean;
}

/** Emscripten module interface (subset we use) */
export interface LiteParserModule {
  _arena_create(block_size: number): number;
  _arena_destroy(arena: number): void;
  _arena_reset(arena: number): void;

  _lp_parse(sql: number, arena: number, err: number): number;
  _lp_parse_all(sql: number, arena: number, err: number): number;
  _lp_parse_tolerant(sql: number, arena: number): number;

  _lp_ast_to_json(node: number, arena: number, pretty: number): number;
  _lp_ast_to_sql(node: number, arena: number): number;
  _lp_parse_result_to_json(result: number, arena: number, pretty: number): number;

  _lp_version(): number;

  _malloc(size: number): number;
  _free(ptr: number): void;

  UTF8ToString(ptr: number): string;
  stringToUTF8(str: string, ptr: number, maxBytes: number): void;
  lengthBytesUTF8(str: string): number;
  HEAPU32: Uint32Array;
}
