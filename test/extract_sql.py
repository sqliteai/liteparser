#!/usr/bin/env python3
"""
Extract SQL statements from SQLite test files (.test/.tcl) in sqlite-master/test/.
Writes one SQL statement per record to a file, separated by NUL bytes.
"""

import os
import re
import sys
import time

TEST_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sqlite-master", "test")

# Tcl commands that take a SQL block in braces
SQL_CMD_RE = re.compile(
    r'\b(?:execsql|catchsql|do_execsql_test|do_catchsql_test|'
    r'do_eqp_test|execsql_test|errorsql_test|execsql_float_test)\s'
)


def extract_braced(text, start):
    """Extract a {}-delimited block. Returns (content, end_pos) or (None, start)."""
    if start >= len(text) or text[start] != '{':
        return None, start
    depth = 1
    i = start + 1
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
        elif ch == '\\':
            i += 1  # skip escaped char
        i += 1
    return None, start  # unmatched


def find_sql_blocks(content):
    """Find all SQL blocks in Tcl test file content."""
    results = []
    for m in SQL_CMD_RE.finditer(content):
        # From end of command name, skip to first '{'
        pos = m.end()
        limit = min(pos + 300, len(content))
        while pos < limit:
            if content[pos] == '{':
                block, _ = extract_braced(content, pos)
                if block:
                    results.append(block.strip())
                break
            elif content[pos] == '\n':
                break
            pos += 1
    return results


def clean_sql(sql):
    """Clean extracted SQL. Returns None if not SQL."""
    if not sql or len(sql) < 3:
        return None
    # Skip pure Tcl
    first_word = sql.split()[0].lower() if sql.split() else ''
    tcl_keywords = {'set', 'proc', 'if', 'return', 'error', 'foreach', 'for',
                    'while', 'puts', 'expr', 'list', 'lindex', 'lappend', 'incr',
                    'variable', 'namespace', 'package', 'catch', 'switch', 'array'}
    if first_word in tcl_keywords:
        return None

    # Replace Tcl $var with placeholder
    sql = re.sub(r'\$\{?\w+\}?', '1', sql)
    # Replace Tcl [cmd ...] with placeholder (non-nested only, fast)
    sql = re.sub(r'\[[^\]]{0,200}\]', '1', sql)

    sql = sql.strip()
    return sql if len(sql) >= 3 else None


def main():
    output_file = sys.argv[1] if len(sys.argv) > 1 else 'sqlite_test.sql'

    all_sql = []
    seen = set()
    file_count = 0
    start = time.time()

    files = sorted(f for f in os.listdir(TEST_DIR) if f.endswith(('.test', '.tcl')))
    print(f"Scanning {len(files)} test files...", file=sys.stderr)

    for i, fname in enumerate(files):
        filepath = os.path.join(TEST_DIR, fname)
        try:
            with open(filepath, 'r', errors='replace') as f:
                content = f.read()
        except Exception:
            continue

        file_count += 1
        blocks = find_sql_blocks(content)
        for sql in blocks:
            cleaned = clean_sql(sql)
            if cleaned and cleaned not in seen:
                seen.add(cleaned)
                all_sql.append(cleaned)

        if (i + 1) % 200 == 0:
            print(f"  {i+1}/{len(files)} files, {len(all_sql)} SQL blocks ({time.time()-start:.1f}s)",
                  file=sys.stderr)

    # Also extract from .sql files
    sql_files = sorted(f for f in os.listdir(TEST_DIR) if f.endswith('.sql'))
    for fname in sql_files:
        filepath = os.path.join(TEST_DIR, fname)
        try:
            with open(filepath, 'r', errors='replace') as f:
                content = f.read()
        except Exception:
            continue
        file_count += 1
        current = []
        for line in content.split('\n'):
            stripped = line.strip()
            if stripped.startswith('.') or stripped.startswith('#') or not stripped:
                if current:
                    sql = ' '.join(current).strip()
                    cleaned = clean_sql(sql)
                    if cleaned and cleaned not in seen:
                        seen.add(cleaned)
                        all_sql.append(cleaned)
                    current = []
                continue
            current.append(stripped)
        if current:
            sql = ' '.join(current).strip()
            cleaned = clean_sql(sql)
            if cleaned and cleaned not in seen:
                seen.add(cleaned)
                all_sql.append(cleaned)

    elapsed = time.time() - start
    print(f"Extracted {len(all_sql)} unique SQL blocks from {file_count} files in {elapsed:.1f}s",
          file=sys.stderr)

    with open(output_file, 'w') as f:
        for sql in all_sql:
            f.write(sql)
            f.write('\0')
    print(f"Written to {output_file}", file=sys.stderr)


if __name__ == '__main__':
    main()
