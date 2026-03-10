import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { createLiteParser, type LiteParser, type AstNode } from '../src/index.js';

let parser: LiteParser;

beforeAll(async () => {
  parser = await createLiteParser();
});

afterAll(() => {
  parser?.destroy();
});

describe('version', () => {
  it('returns a version string', () => {
    expect(parser.version()).toMatch(/^\d+\.\d+\.\d+$/);
  });
});

describe('basic SELECT', () => {
  it('parses SELECT 1', () => {
    const ast = parser.parse('SELECT 1');
    expect(ast).toBeDefined();
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses SELECT a, b FROM t', () => {
    const ast = parser.parse('SELECT a, b FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses SELECT * FROM users', () => {
    const ast = parser.parse('SELECT * FROM users');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses SELECT DISTINCT', () => {
    const ast = parser.parse('SELECT DISTINCT a FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
    expect(ast.distinct).toBe(true);
  });

  it('parses SELECT with alias', () => {
    const ast = parser.parse('SELECT a AS alias FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
  });
});

describe('WHERE / ORDER BY / LIMIT', () => {
  it('parses WHERE clause', () => {
    const ast = parser.parse('SELECT * FROM t WHERE x > 5');
    expect(ast.where).toBeDefined();
  });

  it('parses ORDER BY', () => {
    const ast = parser.parse('SELECT * FROM t ORDER BY x ASC');
    expect(ast.order_by).toBeDefined();
  });

  it('parses LIMIT', () => {
    const ast = parser.parse('SELECT * FROM t LIMIT 10');
    expect(ast.limit).toBeDefined();
  });

  it('parses LIMIT OFFSET', () => {
    const ast = parser.parse('SELECT * FROM t LIMIT 10 OFFSET 5');
    expect(ast.limit).toBeDefined();
  });

  it('parses GROUP BY HAVING', () => {
    const ast = parser.parse('SELECT * FROM t GROUP BY a HAVING count(*) > 1');
    expect(ast.group_by).toBeDefined();
    expect(ast.having).toBeDefined();
  });
});

describe('JOINs', () => {
  it('parses implicit join', () => {
    const ast = parser.parse('SELECT * FROM a, b');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses INNER JOIN', () => {
    const ast = parser.parse('SELECT * FROM a JOIN b ON a.id = b.id');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses LEFT JOIN', () => {
    const ast = parser.parse('SELECT * FROM a LEFT JOIN b ON a.id = b.id');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses CROSS JOIN', () => {
    const ast = parser.parse('SELECT * FROM a CROSS JOIN b');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses NATURAL JOIN', () => {
    const ast = parser.parse('SELECT * FROM a NATURAL JOIN b');
    expect(ast.kind).toBe('STMT_SELECT');
  });
});

describe('compound SELECT', () => {
  it('parses UNION', () => {
    const ast = parser.parse('SELECT 1 UNION SELECT 2');
    expect(ast.kind).toBe('COMPOUND_SELECT');
  });

  it('parses UNION ALL', () => {
    const ast = parser.parse('SELECT 1 UNION ALL SELECT 2');
    expect(ast.kind).toBe('COMPOUND_SELECT');
  });

  it('parses INTERSECT', () => {
    const ast = parser.parse('SELECT 1 INTERSECT SELECT 2');
    expect(ast.kind).toBe('COMPOUND_SELECT');
  });

  it('parses EXCEPT', () => {
    const ast = parser.parse('SELECT 1 EXCEPT SELECT 2');
    expect(ast.kind).toBe('COMPOUND_SELECT');
  });
});

describe('subqueries', () => {
  it('parses subquery in FROM', () => {
    const ast = parser.parse('SELECT * FROM (SELECT 1)');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses scalar subquery', () => {
    const ast = parser.parse('SELECT (SELECT 1)');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses IN subquery', () => {
    const ast = parser.parse('SELECT * FROM t WHERE x IN (SELECT y FROM t2)');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses EXISTS subquery', () => {
    const ast = parser.parse('SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2)');
    expect(ast.kind).toBe('STMT_SELECT');
  });
});

describe('CTEs', () => {
  it('parses WITH clause', () => {
    const ast = parser.parse('WITH x AS (SELECT 1) SELECT * FROM x');
    expect(ast.kind).toBe('STMT_SELECT');
    expect(ast.with).toBeDefined();
  });

  it('parses RECURSIVE CTE', () => {
    const ast = parser.parse(
      'WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<10) SELECT x FROM cnt'
    );
    expect(ast.kind).toBe('STMT_SELECT');
    expect(ast.with).toBeDefined();
  });
});

describe('expressions', () => {
  it('parses arithmetic', () => {
    const ast = parser.parse('SELECT 1 + 2 * 3');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses CASE', () => {
    const ast = parser.parse("SELECT CASE WHEN x > 0 THEN 'pos' ELSE 'neg' END FROM t");
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses CAST', () => {
    const ast = parser.parse('SELECT CAST(x AS INTEGER) FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses BETWEEN', () => {
    const ast = parser.parse('SELECT x BETWEEN 1 AND 10 FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses IN list', () => {
    const ast = parser.parse('SELECT x IN (1, 2, 3) FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses IS NULL', () => {
    const ast = parser.parse('SELECT x IS NULL FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses LIKE', () => {
    const ast = parser.parse("SELECT x LIKE '%test%' FROM t");
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses COLLATE', () => {
    const ast = parser.parse('SELECT x COLLATE NOCASE FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses aggregate functions', () => {
    const ast = parser.parse('SELECT count(*), sum(x) FROM t');
    expect(ast.kind).toBe('STMT_SELECT');
  });
});

describe('INSERT', () => {
  it('parses INSERT VALUES', () => {
    const ast = parser.parse('INSERT INTO t VALUES(1, 2, 3)');
    expect(ast.kind).toBe('STMT_INSERT');
  });

  it('parses INSERT with columns', () => {
    const ast = parser.parse('INSERT INTO t(a, b) VALUES(1, 2)');
    expect(ast.kind).toBe('STMT_INSERT');
  });

  it('parses INSERT OR REPLACE', () => {
    const ast = parser.parse('INSERT OR REPLACE INTO t VALUES(1)');
    expect(ast.kind).toBe('STMT_INSERT');
  });

  it('parses REPLACE INTO', () => {
    const ast = parser.parse('REPLACE INTO t VALUES(1)');
    expect(ast.kind).toBe('STMT_INSERT');
  });

  it('parses INSERT DEFAULT VALUES', () => {
    const ast = parser.parse('INSERT INTO t DEFAULT VALUES');
    expect(ast.kind).toBe('STMT_INSERT');
  });
});

describe('UPDATE', () => {
  it('parses basic UPDATE', () => {
    const ast = parser.parse('UPDATE t SET a = 1');
    expect(ast.kind).toBe('STMT_UPDATE');
  });

  it('parses UPDATE with WHERE', () => {
    const ast = parser.parse('UPDATE t SET a = 1, b = 2 WHERE id = 3');
    expect(ast.kind).toBe('STMT_UPDATE');
  });
});

describe('DELETE', () => {
  it('parses basic DELETE', () => {
    const ast = parser.parse('DELETE FROM t');
    expect(ast.kind).toBe('STMT_DELETE');
  });

  it('parses DELETE with WHERE', () => {
    const ast = parser.parse('DELETE FROM t WHERE id = 1');
    expect(ast.kind).toBe('STMT_DELETE');
  });
});

describe('CREATE TABLE', () => {
  it('parses basic CREATE TABLE', () => {
    const ast = parser.parse('CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)');
    expect(ast.kind).toBe('STMT_CREATE_TABLE');
  });

  it('parses IF NOT EXISTS', () => {
    const ast = parser.parse('CREATE TABLE IF NOT EXISTS t (id INTEGER)');
    expect(ast.kind).toBe('STMT_CREATE_TABLE');
  });

  it('parses CREATE TABLE AS SELECT', () => {
    const ast = parser.parse('CREATE TABLE t AS SELECT * FROM other');
    expect(ast.kind).toBe('STMT_CREATE_TABLE');
  });
});

describe('CREATE INDEX', () => {
  it('parses CREATE INDEX', () => {
    const ast = parser.parse('CREATE INDEX idx ON t(a, b)');
    expect(ast.kind).toBe('STMT_CREATE_INDEX');
  });

  it('parses CREATE UNIQUE INDEX', () => {
    const ast = parser.parse('CREATE UNIQUE INDEX idx ON t(a)');
    expect(ast.kind).toBe('STMT_CREATE_INDEX');
  });
});

describe('CREATE VIEW', () => {
  it('parses CREATE VIEW', () => {
    const ast = parser.parse('CREATE VIEW v AS SELECT * FROM t');
    expect(ast.kind).toBe('STMT_CREATE_VIEW');
  });
});

describe('DROP', () => {
  it('parses DROP TABLE', () => {
    const ast = parser.parse('DROP TABLE t');
    expect(ast.kind).toBe('STMT_DROP');
  });

  it('parses DROP TABLE IF EXISTS', () => {
    const ast = parser.parse('DROP TABLE IF EXISTS t');
    expect(ast.kind).toBe('STMT_DROP');
  });
});

describe('transactions', () => {
  it('parses BEGIN', () => {
    const ast = parser.parse('BEGIN');
    expect(ast.kind).toBe('STMT_BEGIN');
  });

  it('parses COMMIT', () => {
    const ast = parser.parse('COMMIT');
    expect(ast.kind).toBe('STMT_COMMIT');
  });

  it('parses ROLLBACK', () => {
    const ast = parser.parse('ROLLBACK');
    expect(ast.kind).toBe('STMT_ROLLBACK');
  });
});

describe('other statements', () => {
  it('parses PRAGMA', () => {
    const ast = parser.parse('PRAGMA table_info(t)');
    expect(ast.kind).toBe('STMT_PRAGMA');
  });

  it('parses VACUUM', () => {
    const ast = parser.parse('VACUUM');
    expect(ast.kind).toBe('STMT_VACUUM');
  });

  it('parses EXPLAIN', () => {
    const ast = parser.parse('EXPLAIN SELECT 1');
    expect(ast.kind).toBe('STMT_EXPLAIN');
  });

  it('parses ALTER TABLE', () => {
    const ast = parser.parse('ALTER TABLE t ADD COLUMN c TEXT');
    expect(ast.kind).toBe('STMT_ALTER');
  });
});

describe('parseAll', () => {
  it('parses multiple statements', () => {
    const stmts = parser.parseAll('SELECT 1; SELECT 2; SELECT 3');
    expect(stmts).toHaveLength(3);
    for (const s of stmts) {
      expect(s.kind).toBe('STMT_SELECT');
    }
  });
});

describe('parseTolerant', () => {
  it('returns errors for invalid SQL', () => {
    const result = parser.parseTolerant('SELECT 1; GARBAGE; SELECT 2');
    expect(result.stmts.length).toBeGreaterThanOrEqual(2);
    expect(result.errors.length).toBeGreaterThanOrEqual(1);
    expect(result.errors[0].code).toBeDefined();
  });

  it('returns no errors for valid SQL', () => {
    const result = parser.parseTolerant('SELECT 1');
    expect(result.stmts).toHaveLength(1);
    expect(result.errors).toHaveLength(0);
  });
});

describe('parseToJson', () => {
  it('returns valid JSON string', () => {
    const json = parser.parseToJson('SELECT 1');
    const parsed = JSON.parse(json);
    expect(parsed.kind).toBe('STMT_SELECT');
  });

  it('supports pretty printing', () => {
    const json = parser.parseToJson('SELECT 1', { pretty: true });
    expect(json).toContain('\n');
  });
});

describe('unparse', () => {
  it('round-trips a SELECT', () => {
    const sql = parser.unparse('SELECT a, b FROM t WHERE x > 1');
    expect(sql.toUpperCase()).toContain('SELECT');
    expect(sql.toUpperCase()).toContain('FROM');
    expect(sql.toUpperCase()).toContain('WHERE');
  });

  it('round-trips an INSERT', () => {
    const sql = parser.unparse('INSERT INTO t(a, b) SELECT 1, 2');
    expect(sql.toUpperCase()).toContain('INSERT');
    expect(sql.toUpperCase()).toContain('SELECT');
  });

  it('round-trip parse produces equivalent AST', () => {
    const original = 'SELECT a, b FROM t WHERE x > 1 ORDER BY a';
    const unparsed = parser.unparse(original);
    const ast1 = parser.parseToJson(original);
    const ast2 = parser.parseToJson(unparsed);
    // Compare JSON without position info (positions will differ)
    const strip = (json: string) => json.replace(/"pos":\{[^}]+\}/g, '"pos":{}');
    expect(strip(ast2)).toBe(strip(ast1));
  });
});

describe('error handling', () => {
  it('throws SyntaxError for invalid SQL', () => {
    expect(() => parser.parse('NOT VALID SQL AT ALL !!!')).toThrow(SyntaxError);
  });

  it('throws SyntaxError for empty input', () => {
    expect(() => parser.parse('')).toThrow();
  });
});

describe('complex queries', () => {
  it('parses window functions', () => {
    const ast = parser.parse(
      'SELECT row_number() OVER (PARTITION BY a ORDER BY b) FROM t'
    );
    expect(ast.kind).toBe('STMT_SELECT');
  });

  it('parses INSERT ... ON CONFLICT', () => {
    const ast = parser.parse(
      'INSERT INTO t(a) VALUES(1) ON CONFLICT(a) DO UPDATE SET a = excluded.a'
    );
    expect(ast.kind).toBe('STMT_INSERT');
  });

  it('parses CREATE TRIGGER', () => {
    const ast = parser.parse(
      'CREATE TRIGGER tr AFTER INSERT ON t BEGIN SELECT 1; END'
    );
    expect(ast.kind).toBe('STMT_CREATE_TRIGGER');
  });

  it('parses nested subqueries', () => {
    const ast = parser.parse(
      'SELECT * FROM (SELECT * FROM (SELECT 1))'
    );
    expect(ast.kind).toBe('STMT_SELECT');
  });
});
