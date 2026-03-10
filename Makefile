CC      = cc
CFLAGS  = -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -O2
CFLAGS += -DNDEBUG
DEBUG_CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -g -O0

SRCDIR     = src
SQLITE_SRC = sqlite-master

# Headers
HDRS = $(SRCDIR)/liteparser.h $(SRCDIR)/liteparser_internal.h \
       $(SRCDIR)/arena.h $(SRCDIR)/parse.h

# Source files (library)
LIB_SRCS = $(SRCDIR)/arena.c $(SRCDIR)/liteparser.c $(SRCDIR)/lp_tokenize.c \
           $(SRCDIR)/lp_unparse.c $(SRCDIR)/parse.c
LIB_OBJS     = $(LIB_SRCS:.c=.o)
LIB_PIC_OBJS = $(LIB_SRCS:.c=.pic.o)

# Dynamic library
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  DYLIB = libliteparser.dylib
  DYLIB_FLAGS = -dynamiclib -install_name @rpath/$(DYLIB)
else
  DYLIB = libliteparser.so
  DYLIB_FLAGS = -shared
endif

# Lemon parser generator
LEMON     = ./lemon
LEMON_SRC = $(SQLITE_SRC)/tool/lemon.c

.PHONY: all clean test test-suite debug shared regen wasm

all: sqlparse

# --- Pattern rules for object files ---

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/%.pic.o: $(SRCDIR)/%.c $(HDRS)
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

# parse.c is generated — suppress warnings from Lemon output
$(SRCDIR)/parse.o: $(SRCDIR)/parse.c $(HDRS)
	$(CC) $(CFLAGS) -Wno-unused-variable -c -o $@ $<

$(SRCDIR)/parse.pic.o: $(SRCDIR)/parse.c $(HDRS)
	$(CC) $(CFLAGS) -Wno-unused-variable -fPIC -c -o $@ $<

# --- Static library ---

libliteparser.a: $(LIB_OBJS)
	ar rcs $@ $^

# --- Shared library ---

shared: $(DYLIB)

$(DYLIB): $(LIB_PIC_OBJS)
	$(CC) $(DYLIB_FLAGS) -o $@ $^

# --- CLI tool ---

sqlparse: $(SRCDIR)/main.c libliteparser.a
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $(SRCDIR)/main.c -L. -lliteparser

# --- Tests ---

test/test_runner: test/tests.c libliteparser.a
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $< -L. -lliteparser

test: test/test_runner
	./test/test_runner

test/sqlite_test.sql: test/extract_sql.py
	python3 test/extract_sql.py test/sqlite_test.sql

test/test_sqlite_suite: test/test_sqlite_suite.c libliteparser.a
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $< -L. -lliteparser

test-suite: test/test_sqlite_suite test/sqlite_test.sql
	./test/test_sqlite_suite --roundtrip test/sqlite_test.sql

# --- Debug build ---

debug: CFLAGS = $(DEBUG_CFLAGS)
debug: clean all

# --- Clean ---

clean:
	rm -f $(SRCDIR)/*.o libliteparser.a *.dylib *.so
	rm -f sqlparse lemon
	rm -f test/test_runner test/test_sqlite_suite

# --- WASM build (requires emscripten) ---

EMCC = emcc
WASM_DIR = wasm/dist
WASM_CFLAGS = -O2 -DNDEBUG -Wno-unused-variable

WASM_EXPORTED = '[ \
  "_arena_create","_arena_destroy","_arena_reset", \
  "_lp_parse","_lp_parse_all","_lp_parse_tolerant", \
  "_lp_ast_to_json","_lp_ast_to_sql","_lp_parse_result_to_json", \
  "_lp_node_kind_name","_lp_error_code_name","_lp_version", \
  "_lp_node_count","_lp_node_equal","_lp_fix_parents", \
  "_lp_node_alloc","_lp_strdup","_lp_list_push", \
  "_lp_list_insert","_lp_list_replace","_lp_list_remove", \
  "_lp_node_clone","_lp_binop_name","_lp_unaryop_name", \
  "_malloc","_free"]'

wasm: $(WASM_DIR)/liteparser.mjs

$(WASM_DIR)/liteparser.mjs: $(LIB_SRCS) $(HDRS)
	mkdir -p $(WASM_DIR)
	$(EMCC) $(WASM_CFLAGS) \
	  -s WASM=1 \
	  -s MODULARIZE=1 \
	  -s EXPORT_ES6=1 \
	  -s EXPORT_NAME=createLiteParserModule \
	  -s EXPORTED_FUNCTIONS=$(WASM_EXPORTED) \
	  -s "EXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU32','getValue']" \
	  -s ALLOW_MEMORY_GROWTH=1 \
	  -s INITIAL_MEMORY=1048576 \
	  -s STACK_SIZE=65536 \
	  -s SINGLE_FILE=1 \
	  -I$(SRCDIR) \
	  -o $@ \
	  $(LIB_SRCS)

# --- Regenerate parser (requires sqlite-master/) ---

$(LEMON): $(LEMON_SRC)
	$(CC) -O2 -o $@ $(LEMON_SRC)

regen: $(LEMON)
	cd $(SRCDIR) && ../$(LEMON) -Tlp_lempar.c lp_parse.y
	mv $(SRCDIR)/lp_parse.c $(SRCDIR)/parse.c
	mv $(SRCDIR)/lp_parse.h $(SRCDIR)/parse.h
	rm -f $(SRCDIR)/lp_parse.out
