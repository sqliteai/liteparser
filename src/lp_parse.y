%include {
/*
** lp_parse.y — Lemon grammar for liteparser.
**
** Adapted from SQLite's parse.y. All SQLite-internal dependencies removed.
** Grammar actions build an AST using arena-allocated nodes.
*/
}

// Setup for the parser stack
%stack_size        50
%stack_size_limit  lp_parser_stack_limit
%realloc           lp_parser_stack_realloc
%free              lp_parser_stack_free

// Token prefix
%token_prefix TK_

// Token type
%token_type {LpToken}
%default_type {LpToken}

// Extra context
%extra_context {LpParseContext *ctx}

// Syntax error handler
%syntax_error {
  UNUSED_PARAMETER(yymajor);
  if( TOKEN.z[0] ){
    LpSrcPos end = TOKEN.pos;
    end.col += TOKEN.n;
    end.offset += TOKEN.n;
    lp_error(ctx, LP_ERR_SYNTAX, end, "%u:%u: near \"%.*s\": syntax error",
             TOKEN.pos.line, TOKEN.pos.col, TOKEN.n, TOKEN.z);
  }else{
    lp_error(ctx, LP_ERR_INCOMPLETE, ctx->cur_pos, "%u:%u: incomplete input",
             ctx->cur_pos.line, ctx->cur_pos.col);
  }
}

%stack_overflow {
  lp_error(ctx, LP_ERR_STACK_OVERFLOW, ctx->cur_pos, "parser stack overflow");
}

// Parser name
%name lp_Parser

// Includes
%include {
#include "liteparser.h"
#include "liteparser_internal.h"
#include "parse.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define UNUSED_PARAMETER(x) (void)(x)

#define YYNOERRORRECOVERY 1
#define yytestcase(X)
#define YYPARSEFREENEVERNULL 1
#define YYMALLOCARGTYPE size_t

static void *lp_parser_stack_realloc(void *pOld, size_t newSize, LpParseContext *ctx){
  (void)ctx;
  return realloc(pOld, newSize);
}
static void lp_parser_stack_free(void *pOld, LpParseContext *ctx){
  (void)ctx;
  free(pOld);
}
static int lp_parser_stack_limit(LpParseContext *ctx){
  (void)ctx;
  return 1000; /* reasonable parser depth limit */
}

/* Helper to convert token code to binary op */
static LpBinOp token_to_binop(int tk) {
  switch(tk) {
    case TK_PLUS:    return LP_OP_ADD;
    case TK_MINUS:   return LP_OP_SUB;
    case TK_STAR:    return LP_OP_MUL;
    case TK_SLASH:   return LP_OP_DIV;
    case TK_REM:     return LP_OP_MOD;
    case TK_EQ:      return LP_OP_EQ;
    case TK_NE:      return LP_OP_NE;
    case TK_LT:      return LP_OP_LT;
    case TK_LE:      return LP_OP_LE;
    case TK_GT:      return LP_OP_GT;
    case TK_GE:      return LP_OP_GE;
    case TK_BITAND:  return LP_OP_BITAND;
    case TK_BITOR:   return LP_OP_BITOR;
    case TK_LSHIFT:  return LP_OP_LSHIFT;
    case TK_RSHIFT:  return LP_OP_RSHIFT;
    case TK_CONCAT:  return LP_OP_CONCAT;
    case TK_IS:      return LP_OP_IS;
    case TK_ISNOT:   return LP_OP_ISNOT;
    default:         return LP_OP_EQ;
  }
}

} // end %include

// ----- Input is one or more SQL commands -----
input ::= cmdlist.
cmdlist ::= cmdlist ecmd.
cmdlist ::= ecmd.
ecmd ::= SEMI.
ecmd ::= cmdx SEMI. {
  if (ctx->result) {
    lp_list_append(ctx, &ctx->stmts, ctx->result);
    ctx->result = NULL;
  }
}
ecmd ::= explain cmdx SEMI.       {NEVER-REDUCE}
explain ::= EXPLAIN.              { ctx->explain = 1; }
explain ::= EXPLAIN QUERY PLAN.   { ctx->explain = 2; }
cmdx ::= cmd. {
  if( ctx->explain && ctx->result ){
    ctx->result = lp_make_explain(ctx, ctx->explain==2, ctx->result);
    ctx->explain = 0;
  }
}

/////////////////// Begin and end transactions /////////////////////////////

cmd ::= BEGIN transtype(Y) trans_opt.  { ctx->result = lp_make_begin(ctx, Y); }
trans_opt ::= .
trans_opt ::= TRANSACTION.
trans_opt ::= TRANSACTION nm.
%type transtype {int}
transtype(A) ::= .             {A = LP_TRANS_DEFERRED;}
transtype(A) ::= DEFERRED.     {A = LP_TRANS_DEFERRED;}
transtype(A) ::= IMMEDIATE.    {A = LP_TRANS_IMMEDIATE;}
transtype(A) ::= EXCLUSIVE.    {A = LP_TRANS_EXCLUSIVE;}
cmd ::= COMMIT|END trans_opt.  { ctx->result = lp_make_commit(ctx); }
cmd ::= ROLLBACK trans_opt.    { ctx->result = lp_make_rollback(ctx); }

savepoint_opt ::= SAVEPOINT.
savepoint_opt ::= .
cmd ::= SAVEPOINT nm(X). {
  ctx->result = lp_make_savepoint(ctx, &X);
}
cmd ::= RELEASE savepoint_opt nm(X). {
  ctx->result = lp_make_release(ctx, &X);
}
cmd ::= ROLLBACK trans_opt TO savepoint_opt nm(X). {
  ctx->result = lp_make_rollback_to(ctx, &X);
}

/////////////////// The CREATE TABLE statement /////////////////////////////

cmd ::= create_table create_table_args.
create_table ::= createkw temp(T) TABLE ifnotexists(E) nm(Y) dbnm(Z). {
  lp_begin_create_table(ctx, &Y, &Z, T, E);
}
createkw ::= CREATE.

%type ifnotexists {int}
ifnotexists(A) ::= .              {A = 0;}
ifnotexists(A) ::= IF NOT EXISTS. {A = 1;}
%type temp {int}
temp(A) ::= TEMP.  {A = 1;}
temp(A) ::= .      {A = 0;}
create_table_args ::= LP columnlist conslist_opt RP table_option_set(F). {
  lp_end_create_table(ctx, F);
}
create_table_args ::= AS select(S). {
  lp_create_table_as(ctx, S);
  lp_end_create_table(ctx, 0);
}
%type table_option_set {int}
%type table_option {int}
table_option_set(A) ::= .    {A = 0;}
table_option_set(A) ::= table_option(A).
table_option_set(A) ::= table_option_set(X) COMMA table_option(Y). {A = X|Y;}
table_option(A) ::= WITHOUT nm(X). {
  if( X.n==5 && strncasecmp(X.z,"rowid",5)==0 ){
    A = LP_TBL_WITHOUT_ROWID;
  }else{
    A = 0;
    lp_error(ctx, LP_ERR_SYNTAX, lp_token_end(&X), "%u:%u: unknown table option: %.*s", X.pos.line, X.pos.col, X.n, X.z);
  }
}
table_option(A) ::= nm(X). {
  if( X.n==6 && strncasecmp(X.z,"strict",6)==0 ){
    A = LP_TBL_STRICT;
  }else{
    A = 0;
    lp_error(ctx, LP_ERR_SYNTAX, lp_token_end(&X), "%u:%u: unknown table option: %.*s", X.pos.line, X.pos.col, X.n, X.z);
  }
}
columnlist ::= columnlist COMMA columnname carglist.
columnlist ::= columnname carglist.
columnname(A) ::= nm(A) typetoken(Y). { lp_add_column(ctx, &A, &Y); }

// Token declarations for ordering
%token ABORT ACTION AFTER ANALYZE ASC ATTACH BEFORE BEGIN BY CASCADE CAST.
%token CONFLICT DATABASE DEFERRED DESC DETACH EACH END EXCLUSIVE EXPLAIN FAIL.
%token OR AND NOT IS ISNOT MATCH LIKE_KW BETWEEN IN ISNULL NOTNULL NE EQ.
%token GT LE LT GE ESCAPE.

// Fallback tokens
%fallback ID
  ABORT ACTION AFTER ANALYZE ASC ATTACH BEFORE BEGIN BY CASCADE CAST COLUMNKW
  CONFLICT DATABASE DEFERRED DESC DETACH DO
  EACH END EXCLUSIVE EXPLAIN FAIL FOR
  IGNORE IMMEDIATE INITIALLY INSTEAD LIKE_KW MATCH NO PLAN
  QUERY KEY OF OFFSET PRAGMA RAISE RECURSIVE RELEASE REPLACE RESTRICT ROW ROWS
  ROLLBACK SAVEPOINT TEMP TRIGGER VACUUM VIEW VIRTUAL WITH WITHOUT
  NULLS FIRST LAST
  CURRENT FOLLOWING PARTITION PRECEDING RANGE UNBOUNDED
  EXCLUDE GROUPS OTHERS TIES
  WITHIN
  GENERATED ALWAYS
  MATERIALIZED
  REINDEX RENAME CTIME_KW IF
  .
%wildcard ANY.

// Operator precedence
%left OR.
%left AND.
%right NOT.
%left IS MATCH LIKE_KW BETWEEN IN ISNULL NOTNULL NE EQ.
%left GT LE LT GE.
%right ESCAPE.
%left BITAND BITOR LSHIFT RSHIFT.
%left PLUS MINUS.
%left STAR SLASH REM.
%left CONCAT PTR.
%left COLLATE.
%right BITNOT.
%nonassoc ON.

// Token classes
%token_class id  ID|INDEXED.
%token_class ids  ID|STRING.
%token_class idj  ID|INDEXED|JOIN_KW.

// Name
%type nm {LpToken}
nm(A) ::= idj(A).
nm(A) ::= STRING(A).

// Type token
%type typetoken {LpToken}
typetoken(A) ::= .   {A.n = 0; A.z = 0;}
typetoken(A) ::= typename(A).
typetoken(A) ::= typename(A) LP signed RP(Y). {
  A.n = (int)(&Y.z[Y.n] - A.z);
}
typetoken(A) ::= typename(A) LP signed COMMA signed RP(Y). {
  A.n = (int)(&Y.z[Y.n] - A.z);
}
%type typename {LpToken}
typename(A) ::= ids(A).
typename(A) ::= typename(A) ids(Y). {A.n=Y.n+(int)(Y.z-A.z);}
signed ::= plus_num.
signed ::= minus_num.

// Scanpt - captures position in input
%type scanpt {const char*}
scanpt(A) ::= . {
  assert( yyLookahead!=YYNOCODE );
  A = yyLookaheadToken.z;
}
scantok(A) ::= . {
  assert( yyLookahead!=YYNOCODE );
  A = yyLookaheadToken;
}

// Column constraints
carglist ::= carglist ccons.
carglist ::= .
ccons ::= CONSTRAINT nm(X). { lp_set_constraint_name(ctx, &X); }
ccons ::= DEFAULT scantok term(X).
                            { lp_add_column_constraint_default(ctx, X); }
ccons ::= DEFAULT LP expr(X) RP.
                            { lp_add_column_constraint_default(ctx, X); }
ccons ::= DEFAULT PLUS scantok term(X).
                            { lp_add_column_constraint_default(ctx, X); }
ccons ::= DEFAULT MINUS scantok term(X). {
  LpNode *p = lp_make_unary(ctx, LP_UOP_MINUS, X);
  lp_add_column_constraint_default(ctx, p);
}
ccons ::= DEFAULT scantok id(X). {
  lp_add_column_constraint_default_id(ctx, &X);
}
ccons ::= NULL onconf.            { lp_add_column_constraint_null(ctx); }
ccons ::= NOT NULL onconf(R).     { lp_add_column_constraint_notnull(ctx, R); }
ccons ::= PRIMARY KEY sortorder(Z) onconf(R) autoinc(I).
                                  { lp_add_column_constraint_pk(ctx, Z, R, I); }
ccons ::= UNIQUE onconf(R).       { lp_add_column_constraint_unique(ctx, R); }
ccons ::= CHECK LP expr(X) RP.    { lp_add_column_constraint_check(ctx, X); }
ccons ::= REFERENCES nm(T) eidlist_opt(TA) refargs(R). {
  LpNode *fk = lp_make_foreign_key(ctx, &T, TA, R);
  lp_add_column_constraint_references(ctx, fk);
}
ccons ::= defer_subclause(D).    { (void)D; /* deferred FK not tracked per-column */ }
ccons ::= COLLATE ids(C).        { lp_add_column_constraint_collate(ctx, &C); }
ccons ::= GENERATED ALWAYS AS generated.
ccons ::= AS generated.
generated ::= LP expr(E) RP.          { lp_add_column_constraint_generated(ctx, E, 0); }
generated ::= LP expr(E) RP ID(TYPE). {
  int stored = (TYPE.n==6 && strncasecmp(TYPE.z,"stored",6)==0) ? 1 : 0;
  lp_add_column_constraint_generated(ctx, E, stored);
}

%type autoinc {int}
autoinc(X) ::= .          {X = 0;}
autoinc(X) ::= AUTOINCR.  {X = 1;}

// Foreign key ref args
%type refargs {int}
refargs(A) ::= .                  { A = 0x0101 * LP_FK_NO_ACTION; }
refargs(A) ::= refargs(A) refarg(Y). { A = (A & ~Y.mask) | Y.value; }
%type refarg {struct {int value; int mask;}}
refarg(A) ::= MATCH nm.              { A.value = 0;     A.mask = 0x000000; }
refarg(A) ::= ON INSERT refact.      { A.value = 0;     A.mask = 0x000000; }
refarg(A) ::= ON DELETE refact(X).   { A.value = X;     A.mask = 0x0000ff; }
refarg(A) ::= ON UPDATE refact(X).   { A.value = X<<8;  A.mask = 0x00ff00; }
%type refact {int}
refact(A) ::= SET NULL.              { A = LP_FK_SET_NULL; }
refact(A) ::= SET DEFAULT.           { A = LP_FK_SET_DEFAULT; }
refact(A) ::= CASCADE.               { A = LP_FK_CASCADE; }
refact(A) ::= RESTRICT.              { A = LP_FK_RESTRICT; }
refact(A) ::= NO ACTION.             { A = LP_FK_NO_ACTION; }
%type defer_subclause {int}
defer_subclause(A) ::= NOT DEFERRABLE init_deferred_pred_opt.     {A = 0;}
defer_subclause(A) ::= DEFERRABLE init_deferred_pred_opt(X).      {A = X;}
%type init_deferred_pred_opt {int}
init_deferred_pred_opt(A) ::= .                       {A = 0;}
init_deferred_pred_opt(A) ::= INITIALLY DEFERRED.     {A = 1;}
init_deferred_pred_opt(A) ::= INITIALLY IMMEDIATE.    {A = 0;}

conslist_opt(A) ::= .                         {A.n = 0; A.z = 0;}
conslist_opt(A) ::= COMMA(A) conslist.
conslist ::= conslist tconscomma tcons.
conslist ::= tcons.
tconscomma ::= COMMA.          { ctx->cur_constraint_name = 0; }
tconscomma ::= .
tcons ::= CONSTRAINT nm(X).   { lp_set_constraint_name(ctx, &X); }
tcons ::= PRIMARY KEY LP sortlist(X) autoinc(I) RP onconf(R). {
  lp_add_table_constraint_pk(ctx, X, R, I);
}
tcons ::= UNIQUE LP sortlist(X) RP onconf(R). {
  lp_add_table_constraint_unique(ctx, X, R);
}
tcons ::= CHECK LP expr(E) RP onconf(R). {
  lp_add_table_constraint_check(ctx, E, R);
}
tcons ::= FOREIGN KEY LP eidlist(FA) RP
          REFERENCES nm(T) eidlist_opt(TA) refargs(R) defer_subclause_opt(D). {
  LpNode *fk = lp_make_foreign_key(ctx, &T, TA, R);
  lp_add_table_constraint_fk(ctx, FA, fk, D);
}
%type defer_subclause_opt {int}
defer_subclause_opt(A) ::= .                    {A = 0;}
defer_subclause_opt(A) ::= defer_subclause(A).

// Conflict resolution
%type onconf {int}
%type orconf {int}
%type resolvetype {int}
onconf(A) ::= .                              {A = LP_CONFLICT_NONE;}
onconf(A) ::= ON CONFLICT resolvetype(X).    {A = X;}
orconf(A) ::= .                              {A = LP_CONFLICT_NONE;}
orconf(A) ::= OR resolvetype(X).             {A = X;}
resolvetype(A) ::= raisetype(A).
resolvetype(A) ::= IGNORE.                   {A = LP_CONFLICT_IGNORE;}
resolvetype(A) ::= REPLACE.                  {A = LP_CONFLICT_REPLACE;}

/////////////////// DROP TABLE /////////////////////////////

cmd ::= DROP TABLE ifexists(E) fullname(X). {
  ctx->result = lp_make_drop(ctx, LP_DROP_TABLE, &X.name, &X.schema, E);
}
%type ifexists {int}
ifexists(A) ::= IF EXISTS.   {A = 1;}
ifexists(A) ::= .            {A = 0;}

/////////////////// CREATE VIEW /////////////////////////////

cmd ::= createkw temp(T) VIEW ifnotexists(E) nm(Y) dbnm(Z) eidlist_opt(C)
          AS select(S). {
  ctx->result = lp_make_create_view(ctx, &Y, &Z, C, S, T, E);
}
cmd ::= DROP VIEW ifexists(E) fullname(X). {
  ctx->result = lp_make_drop(ctx, LP_DROP_VIEW, &X.name, &X.schema, E);
}

/////////////////// The SELECT statement /////////////////////////////

cmd ::= select(X).  { ctx->result = X; }

%type select {LpNode*}
%type selectnowith {LpNode*}
%type oneselect {LpNode*}

%include {
  /* Helper for compound select double-linking.
  ** In our AST, compounds are represented as binary trees, not linked lists.
  ** No special handling needed beyond what the grammar provides. */
}

select(A) ::= WITH wqlist(W) selectnowith(X). {
  LpNode *w = lp_make_with(ctx, 0, W);
  A = lp_attach_with(ctx, X, w);
}
select(A) ::= WITH RECURSIVE wqlist(W) selectnowith(X). {
  LpNode *w = lp_make_with(ctx, 1, W);
  A = lp_attach_with(ctx, X, w);
}
select(A) ::= selectnowith(A).

selectnowith(A) ::= oneselect(A).
selectnowith(A) ::= selectnowith(A) multiselect_op(Y) oneselect(Z). {
  A = lp_make_compound(ctx, Y, A, Z);
}
%type multiselect_op {int}
multiselect_op(A) ::= UNION.             {A = LP_COMPOUND_UNION;}
multiselect_op(A) ::= UNION ALL.         {A = LP_COMPOUND_UNION_ALL;}
multiselect_op(A) ::= EXCEPT.            {A = LP_COMPOUND_EXCEPT;}
multiselect_op(A) ::= INTERSECT.         {A = LP_COMPOUND_INTERSECT;}

oneselect(A) ::= SELECT distinct(D) selcollist(W) from(X) where_opt(Y)
                 groupby_opt(P) having_opt(Q)
                 orderby_opt(Z) limit_opt(L). {
  A = lp_make_select(ctx, D, W, X, Y, P, Q, Z, L);
}
oneselect(A) ::= SELECT distinct(D) selcollist(W) from(X) where_opt(Y)
                 groupby_opt(P) having_opt(Q) window_clause(R)
                 orderby_opt(Z) limit_opt(L). {
  A = lp_make_select_with_window(ctx, D, W, X, Y, P, Q, R, Z, L);
}

// VALUES clause
%type values {LpNode*}
oneselect(A) ::= values(A).
values(A) ::= VALUES LP nexprlist(X) RP. {
  A = lp_make_values(ctx, X);
}

// Multiple VALUES rows
%type mvalues {LpNode*}
oneselect(A) ::= mvalues(A).
mvalues(A) ::= values(A) COMMA LP nexprlist(Y) RP. {
  /* Build compound: A UNION ALL (VALUES Y) */
  LpNode *row = lp_make_values(ctx, Y);
  A = lp_make_compound(ctx, LP_COMPOUND_UNION_ALL, A, row);
}
mvalues(A) ::= mvalues(A) COMMA LP nexprlist(Y) RP. {
  LpNode *row = lp_make_values(ctx, Y);
  A = lp_make_compound(ctx, LP_COMPOUND_UNION_ALL, A, row);
}

%type distinct {int}
distinct(A) ::= DISTINCT.   {A = 1;}
distinct(A) ::= ALL.        {A = 0;}
distinct(A) ::= .           {A = 0;}

// Result column list
%type selcollist {LpNodeList*}
%type sclp {LpNodeList*}
sclp(A) ::= selcollist(A) COMMA.
sclp(A) ::= .   {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
}
selcollist(A) ::= sclp(A) scanpt expr(X) scanpt as(Y). {
  LpNode *rc = lp_make_result_column(ctx, X, Y.n>0 ? &Y : 0);
  lp_list_append(ctx, A, rc);
}
selcollist(A) ::= sclp(A) scanpt STAR. {
  LpNode *rc = lp_make_result_star(ctx);
  lp_list_append(ctx, A, rc);
}
selcollist(A) ::= sclp(A) scanpt nm(X) DOT STAR. {
  LpNode *rc = lp_make_result_table_star(ctx, &X);
  lp_list_append(ctx, A, rc);
}

// AS alias
%type as {LpToken}
as(X) ::= AS nm(Y).    {X = Y;}
as(X) ::= ids(X).
as(X) ::= .            {X.n = 0; X.z = 0;}

// FROM clause
%type seltablist {LpNode*}
%type stl_prefix {LpNode*}
%type from {LpNode*}

from(A) ::= .                {A = 0;}
from(A) ::= FROM seltablist(X). { A = X; }

stl_prefix(A) ::= seltablist(A) joinop(Y). {
  /* Y is the join type for the NEXT table */
  /* Store it temporarily - will be used when the next table is appended */
  /* We store Y in a hack: create a join node with just the type, to be filled in */
  LpNode *j = lp_node_new(ctx, LP_JOIN_CLAUSE);
  j->u.join.left = A;
  j->u.join.join_type = Y;
  A = j;
}
stl_prefix(A) ::= .   {A = 0;}

seltablist(A) ::= stl_prefix(A) nm(Y) dbnm(D) as(Z) on_using(N). {
  LpNode *t = lp_make_from_table(ctx, &Y, D.n>0?&D:0, Z.n>0?&Z:0);
  if( A && A->kind==LP_JOIN_CLAUSE ){
    A->u.join.right = t;
    A->u.join.on_expr = N.pOn;
    if( N.pUsing ) A->u.join.using_columns = *N.pUsing;
  }else if( A ){
    LpNode *j = lp_node_new(ctx, LP_JOIN_CLAUSE);
    j->u.join.left = A;
    j->u.join.right = t;
    j->u.join.join_type = LP_JOIN_INNER;
    j->u.join.on_expr = N.pOn;
    if( N.pUsing ) j->u.join.using_columns = *N.pUsing;
    A = j;
  }else{
    A = t;
  }
}
seltablist(A) ::= stl_prefix(A) nm(Y) dbnm(D) as(Z) indexed_by(I) on_using(N). {
  LpNode *t = lp_make_from_table(ctx, &Y, D.n>0?&D:0, Z.n>0?&Z:0);
  if( I.n==1 && I.z==0 ) lp_from_table_set_not_indexed(ctx, t);
  else if( I.n>0 ) lp_from_table_set_indexed(ctx, t, &I);
  if( A && A->kind==LP_JOIN_CLAUSE ){
    A->u.join.right = t;
    A->u.join.on_expr = N.pOn;
    if( N.pUsing ) A->u.join.using_columns = *N.pUsing;
  }else if( A ){
    LpNode *j = lp_node_new(ctx, LP_JOIN_CLAUSE);
    j->u.join.left = A;
    j->u.join.right = t;
    j->u.join.join_type = LP_JOIN_INNER;
    j->u.join.on_expr = N.pOn;
    if( N.pUsing ) j->u.join.using_columns = *N.pUsing;
    A = j;
  }else{
    A = t;
  }
}
seltablist(A) ::= stl_prefix(A) nm(Y) dbnm(D) LP exprlist(E) RP as(Z) on_using(N). {
  LpNode *t = lp_make_from_table(ctx, &Y, D.n>0?&D:0, Z.n>0?&Z:0);
  if( E ) lp_from_table_set_args(ctx, t, E);
  if( A && A->kind==LP_JOIN_CLAUSE ){
    A->u.join.right = t;
    A->u.join.on_expr = N.pOn;
    if( N.pUsing ) A->u.join.using_columns = *N.pUsing;
  }else if( A ){
    LpNode *j = lp_node_new(ctx, LP_JOIN_CLAUSE);
    j->u.join.left = A;
    j->u.join.right = t;
    j->u.join.join_type = LP_JOIN_INNER;
    j->u.join.on_expr = N.pOn;
    if( N.pUsing ) j->u.join.using_columns = *N.pUsing;
    A = j;
  }else{
    A = t;
  }
}
seltablist(A) ::= stl_prefix(A) LP select(S) RP as(Z) on_using(N). {
  LpNode *t = lp_make_from_subquery(ctx, S, Z.n>0?&Z:0);
  if( A && A->kind==LP_JOIN_CLAUSE ){
    A->u.join.right = t;
    A->u.join.on_expr = N.pOn;
    if( N.pUsing ) A->u.join.using_columns = *N.pUsing;
  }else if( A ){
    LpNode *j = lp_node_new(ctx, LP_JOIN_CLAUSE);
    j->u.join.left = A;
    j->u.join.right = t;
    j->u.join.join_type = LP_JOIN_INNER;
    j->u.join.on_expr = N.pOn;
    if( N.pUsing ) j->u.join.using_columns = *N.pUsing;
    A = j;
  }else{
    A = t;
  }
}
seltablist(A) ::= stl_prefix(A) LP seltablist(F) RP as(Z) on_using(N). {
  /* Parenthesized FROM list - treat as subquery or pass through */
  LpNode *t;
  if( Z.n>0 || N.pOn || N.pUsing ){
    t = lp_make_from_subquery(ctx, F, Z.n>0?&Z:0);
  }else{
    t = F;
  }
  if( A && A->kind==LP_JOIN_CLAUSE ){
    A->u.join.right = t;
    A->u.join.on_expr = N.pOn;
    if( N.pUsing ) A->u.join.using_columns = *N.pUsing;
  }else if( A ){
    LpNode *j = lp_node_new(ctx, LP_JOIN_CLAUSE);
    j->u.join.left = A;
    j->u.join.right = t;
    j->u.join.join_type = LP_JOIN_INNER;
    j->u.join.on_expr = N.pOn;
    if( N.pUsing ) j->u.join.using_columns = *N.pUsing;
    A = j;
  }else{
    A = t;
  }
}

%type dbnm {LpToken}
dbnm(A) ::= .          {A.z=0; A.n=0;}
dbnm(A) ::= DOT nm(X). {A = X;}

// fullname returns a struct with name and schema tokens
%type fullname {struct {LpToken name; LpToken schema;}}
fullname(A) ::= nm(X).  {
  A.name = X; A.schema.z = 0; A.schema.n = 0;
}
fullname(A) ::= nm(X) DOT nm(Y). {
  A.schema = X; A.name = Y;
}

%type xfullname {struct {LpToken name; LpToken schema; LpToken alias;}}
xfullname(A) ::= nm(X).  {
  A.name = X; A.schema.z=0; A.schema.n=0; A.alias.z=0; A.alias.n=0;
}
xfullname(A) ::= nm(X) DOT nm(Y). {
  A.schema = X; A.name = Y; A.alias.z=0; A.alias.n=0;
}
xfullname(A) ::= nm(X) AS nm(Z). {
  A.name = X; A.schema.z=0; A.schema.n=0; A.alias = Z;
}
xfullname(A) ::= nm(X) DOT nm(Y) AS nm(Z). {
  A.schema = X; A.name = Y; A.alias = Z;
}

// Join operator
%type joinop {int}
joinop(X) ::= COMMA|JOIN.              { X = LP_JOIN_INNER; }
joinop(X) ::= JOIN_KW(A) JOIN.
                  {X = lp_parse_join_type(ctx,&A,0,0);}
joinop(X) ::= JOIN_KW(A) nm(B) JOIN.
                  {X = lp_parse_join_type(ctx,&A,&B,0);}
joinop(X) ::= JOIN_KW(A) nm(B) nm(C) JOIN.
                  {X = lp_parse_join_type(ctx,&A,&B,&C);}

// ON / USING
%type on_using {struct {LpNode *pOn; LpNodeList *pUsing;}}
on_using(N) ::= ON expr(E).            {N.pOn = E; N.pUsing = 0;}
on_using(N) ::= USING LP idlist(L) RP. {N.pOn = 0; N.pUsing = L;}
on_using(N) ::= .                 [OR] {N.pOn = 0; N.pUsing = 0;}

// INDEXED BY
%type indexed_opt {LpToken}
%type indexed_by  {LpToken}
indexed_opt(A) ::= .                 {A.z=0; A.n=0;}
indexed_opt(A) ::= indexed_by(A).
indexed_by(A)  ::= INDEXED BY nm(X). {A = X;}
indexed_by(A)  ::= NOT INDEXED.      {A.z=0; A.n=1;}

// ORDER BY
%type orderby_opt {LpNodeList*}
%type sortlist {LpNodeList*}

orderby_opt(A) ::= .                          {A = 0;}
orderby_opt(A) ::= ORDER BY sortlist(X).      {A = X;}
sortlist(A) ::= sortlist(A) COMMA expr(Y) sortorder(Z) nulls(X). {
  LpNode *t = lp_make_order_term(ctx, Y, Z, X);
  lp_list_append(ctx, A, t);
}
sortlist(A) ::= expr(Y) sortorder(Z) nulls(X). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  LpNode *t = lp_make_order_term(ctx, Y, Z, X);
  lp_list_append(ctx, A, t);
}

%type sortorder {int}
sortorder(A) ::= ASC.           {A = LP_SORT_ASC;}
sortorder(A) ::= DESC.          {A = LP_SORT_DESC;}
sortorder(A) ::= .              {A = LP_SORT_UNDEFINED;}

%type nulls {int}
nulls(A) ::= NULLS FIRST.       {A = LP_SORT_ASC;}
nulls(A) ::= NULLS LAST.        {A = LP_SORT_DESC;}
nulls(A) ::= .                  {A = LP_SORT_UNDEFINED;}

// GROUP BY / HAVING
%type groupby_opt {LpNodeList*}
groupby_opt(A) ::= .                      {A = 0;}
groupby_opt(A) ::= GROUP BY nexprlist(X).  {A = X;}

%type having_opt {LpNode*}
having_opt(A) ::= .                {A = 0;}
having_opt(A) ::= HAVING expr(X).  {A = X;}

// LIMIT
%type limit_opt {LpNode*}
limit_opt(A) ::= .       {A = 0;}
limit_opt(A) ::= LIMIT expr(X).
                         {A = lp_make_limit(ctx, X, 0);}
limit_opt(A) ::= LIMIT expr(X) OFFSET expr(Y).
                         {A = lp_make_limit(ctx, X, Y);}
limit_opt(A) ::= LIMIT expr(X) COMMA expr(Y).
                         {A = lp_make_limit(ctx, Y, X);}

/////////////////// DELETE /////////////////////////////

cmd ::= with DELETE FROM xfullname(X) indexed_opt(I) where_opt_ret(W). {
  LpToken *s = X.schema.n>0 ? &X.schema : 0;
  LpToken *a = X.alias.n>0 ? &X.alias : 0;
  ctx->result = lp_make_delete(ctx, &X.name, s, a, W.where, 0, 0, W.ret);
  (void)I;
}

%type where_opt {LpNode*}
%type where_opt_ret {struct {LpNode *where; LpNodeList *ret;}}

where_opt(A) ::= .                    {A = 0;}
where_opt(A) ::= WHERE expr(X).       {A = X;}
where_opt_ret(A) ::= .               {A.where = 0; A.ret = 0;}
where_opt_ret(A) ::= WHERE expr(X).   {A.where = X; A.ret = 0;}
where_opt_ret(A) ::= RETURNING selcollist(X).
       {A.where = 0; A.ret = X;}
where_opt_ret(A) ::= WHERE expr(X) RETURNING selcollist(Y).
       {A.where = X; A.ret = Y;}

/////////////////// UPDATE /////////////////////////////

cmd ::= with UPDATE orconf(R) xfullname(X) indexed_opt(I) SET setlist(Y) from(F)
        where_opt_ret(W). {
  LpToken *s = X.schema.n>0 ? &X.schema : 0;
  LpToken *a = X.alias.n>0 ? &X.alias : 0;
  ctx->result = lp_make_update(ctx, R, &X.name, s, a, Y, F, W.where, 0, 0, W.ret);
  (void)I;
}

%type setlist {LpNodeList*}
setlist(A) ::= setlist(A) COMMA nm(X) EQ expr(Y). {
  LpNode *sc = lp_make_set_clause(ctx, &X, Y);
  lp_list_append(ctx, A, sc);
}
setlist(A) ::= setlist(A) COMMA LP idlist(X) RP EQ expr(Y). {
  LpNode *sc = lp_make_set_clause_multi(ctx, X, Y);
  lp_list_append(ctx, A, sc);
}
setlist(A) ::= nm(X) EQ expr(Y). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  LpNode *sc = lp_make_set_clause(ctx, &X, Y);
  lp_list_append(ctx, A, sc);
}
setlist(A) ::= LP idlist(X) RP EQ expr(Y). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  LpNode *sc = lp_make_set_clause_multi(ctx, X, Y);
  lp_list_append(ctx, A, sc);
}

/////////////////// INSERT /////////////////////////////

cmd ::= with insert_cmd(R) INTO xfullname(X) idlist_opt(F) select(S)
        upsert(U). {
  LpToken *s = X.schema.n>0 ? &X.schema : 0;
  LpToken *a = X.alias.n>0 ? &X.alias : 0;
  LpNodeList *ret = U && U->kind==LP_RETURNING ? &U->u.returning.columns : 0;
  LpNode *ups = (U && U->kind==LP_UPSERT) ? U : 0;
  ctx->result = lp_make_insert(ctx, R, &X.name, s, a, F, S, ups, ret);
}
cmd ::= with insert_cmd(R) INTO xfullname(X) idlist_opt(F) DEFAULT VALUES returning(RET). {
  LpToken *s = X.schema.n>0 ? &X.schema : 0;
  LpToken *a = X.alias.n>0 ? &X.alias : 0;
  ctx->result = lp_make_insert(ctx, R, &X.name, s, a, F, 0, 0, RET);
}

%type upsert {LpNode*}
upsert(A) ::= . { A = 0; }
upsert(A) ::= RETURNING selcollist(X). {
  A = lp_make_returning(ctx, X);
}
upsert(A) ::= ON CONFLICT LP sortlist(T) RP where_opt(TW)
              DO UPDATE SET setlist(Z) where_opt(W) upsert(N). {
  A = lp_make_upsert(ctx, T, TW, Z, W, N);
}
upsert(A) ::= ON CONFLICT LP sortlist(T) RP where_opt(TW) DO NOTHING upsert(N). {
  A = lp_make_upsert(ctx, T, TW, 0, 0, N);
}
upsert(A) ::= ON CONFLICT DO NOTHING returning(RET). {
  A = lp_make_upsert(ctx, 0, 0, 0, 0, 0);
  (void)RET;
}
upsert(A) ::= ON CONFLICT DO UPDATE SET setlist(Z) where_opt(W) returning(RET). {
  A = lp_make_upsert(ctx, 0, 0, Z, W, 0);
  (void)RET;
}

%type returning {LpNodeList*}
returning(A) ::= RETURNING selcollist(X). { A = X; }
returning(A) ::= .                        { A = 0; }

%type insert_cmd {int}
insert_cmd(A) ::= INSERT orconf(R).   {A = R;}
insert_cmd(A) ::= REPLACE.            {A = LP_CONFLICT_REPLACE;}

// ID list
%type idlist_opt {LpNodeList*}
%type idlist {LpNodeList*}

idlist_opt(A) ::= .                       {A = 0;}
idlist_opt(A) ::= LP idlist(X) RP.    {A = X;}
idlist(A) ::= idlist(A) COMMA nm(Y). {
  LpNode *n = lp_make_id_node(ctx, &Y);
  lp_list_append(ctx, A, n);
}
idlist(A) ::= nm(Y). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  LpNode *n = lp_make_id_node(ctx, &Y);
  lp_list_append(ctx, A, n);
}

/////////////////// Expressions /////////////////////////////

%type expr {LpNode*}
%type term {LpNode*}

expr(A) ::= term(A).
expr(A) ::= LP expr(X) RP. {A = X;}
expr(A) ::= idj(X).          {A = lp_make_column_ref(ctx, &X);}
expr(A) ::= nm(X) DOT nm(Y). {
  A = lp_make_column_ref2(ctx, &X, &Y);
}
expr(A) ::= nm(X) DOT nm(Y) DOT nm(Z). {
  A = lp_make_column_ref3(ctx, &X, &Y, &Z);
}
term(A) ::= NULL(X). {A = lp_make_literal_null(ctx); (void)X;}
term(A) ::= FLOAT(X). {A = lp_make_literal_float(ctx, &X);}
term(A) ::= BLOB(X).  {A = lp_make_literal_blob(ctx, &X);}
term(A) ::= STRING(X). {A = lp_make_literal_string(ctx, &X);}
term(A) ::= INTEGER(X). {A = lp_make_literal_int(ctx, &X);}
expr(A) ::= VARIABLE(X). { A = lp_make_variable(ctx, &X); }
expr(A) ::= expr(A) COLLATE ids(C). {
  A = lp_make_collate(ctx, A, &C);
}
expr(A) ::= CAST LP expr(E) AS typetoken(T) RP. {
  A = lp_make_cast(ctx, E, &T);
}

// Function calls
expr(A) ::= idj(X) LP distinct(D) exprlist(Y) RP. {
  A = lp_make_function(ctx, &X, Y, D);
}
expr(A) ::= idj(X) LP distinct(D) exprlist(Y) ORDER BY sortlist(O) RP. {
  A = lp_make_function(ctx, &X, Y, D);
  if(A) A->u.function.order_by = *O;
}
expr(A) ::= idj(X) LP STAR RP. {
  A = lp_make_function_star(ctx, &X);
}

// Window function syntax
expr(A) ::= idj(X) LP distinct(D) exprlist(Y) RP filter_over(Z). {
  A = lp_make_function(ctx, &X, Y, D);
  if(ctx->pending_filter) { lp_attach_filter(ctx, A, ctx->pending_filter); ctx->pending_filter = 0; }
  if(Z) lp_attach_window(ctx, A, Z);
}
expr(A) ::= idj(X) LP distinct(D) exprlist(Y) ORDER BY sortlist(O) RP filter_over(Z). {
  A = lp_make_function(ctx, &X, Y, D);
  if(A) A->u.function.order_by = *O;
  if(ctx->pending_filter) { lp_attach_filter(ctx, A, ctx->pending_filter); ctx->pending_filter = 0; }
  if(Z) lp_attach_window(ctx, A, Z);
}
expr(A) ::= idj(X) LP STAR RP filter_over(Z). {
  A = lp_make_function_star(ctx, &X);
  if(ctx->pending_filter) { lp_attach_filter(ctx, A, ctx->pending_filter); ctx->pending_filter = 0; }
  if(Z) lp_attach_window(ctx, A, Z);
}

term(A) ::= CTIME_KW(OP). {
  A = lp_make_function(ctx, &OP, 0, 0);
  if(A) A->u.function.is_ctime_kw = 1;
}

expr(A) ::= LP nexprlist(X) COMMA expr(Y) RP. {
  LpNodeList *list = X;
  lp_list_append(ctx, list, Y);
  A = lp_make_vector(ctx, list);
}

// Binary operators
expr(A) ::= expr(A) AND expr(Y).        {A = lp_make_binary(ctx, LP_OP_AND, A, Y);}
expr(A) ::= expr(A) OR expr(Y).         {A = lp_make_binary(ctx, LP_OP_OR, A, Y);}
expr(A) ::= expr(A) LT|GT|GE|LE(OP) expr(Y).
                                        {A = lp_make_binary(ctx, token_to_binop(@OP), A, Y);}
expr(A) ::= expr(A) EQ|NE(OP) expr(Y).  {A = lp_make_binary(ctx, token_to_binop(@OP), A, Y);}
expr(A) ::= expr(A) BITAND|BITOR|LSHIFT|RSHIFT(OP) expr(Y).
                                        {A = lp_make_binary(ctx, token_to_binop(@OP), A, Y);}
expr(A) ::= expr(A) PLUS|MINUS(OP) expr(Y).
                                        {A = lp_make_binary(ctx, token_to_binop(@OP), A, Y);}
expr(A) ::= expr(A) STAR|SLASH|REM(OP) expr(Y).
                                        {A = lp_make_binary(ctx, token_to_binop(@OP), A, Y);}
expr(A) ::= expr(A) CONCAT expr(Y).     {A = lp_make_binary(ctx, LP_OP_CONCAT, A, Y);}

// LIKE / MATCH / GLOB
%type likeop {LpToken}
likeop(A) ::= LIKE_KW|MATCH(A).
likeop(A) ::= NOT LIKE_KW|MATCH(X). {A=X; A.n|=0x80000000;}
expr(A) ::= expr(A) likeop(OP) expr(Y).  [LIKE_KW]  {
  int bNot = OP.n & 0x80000000;
  OP.n &= 0x7fffffff;
  A = lp_make_like(ctx, A, Y, 0, &OP, bNot);
}
expr(A) ::= expr(A) likeop(OP) expr(Y) ESCAPE expr(E).  [LIKE_KW]  {
  int bNot = OP.n & 0x80000000;
  OP.n &= 0x7fffffff;
  A = lp_make_like(ctx, A, Y, E, &OP, bNot);
}

// IS NULL / IS NOT NULL
expr(A) ::= expr(A) ISNULL.   { A = lp_make_isnull(ctx, A, 0); }
expr(A) ::= expr(A) NOTNULL.  { A = lp_make_isnull(ctx, A, 1); }
expr(A) ::= expr(A) NOT NULL. { A = lp_make_isnull(ctx, A, 1); }

// IS / IS NOT
expr(A) ::= expr(A) IS expr(Y).     {
  A = lp_make_is(ctx, A, Y, 0);
}
expr(A) ::= expr(A) IS NOT expr(Y). {
  A = lp_make_is(ctx, A, Y, 1);
}
expr(A) ::= expr(A) IS NOT DISTINCT FROM expr(Y). {
  A = lp_make_is(ctx, A, Y, 0);
}
expr(A) ::= expr(A) IS DISTINCT FROM expr(Y). {
  A = lp_make_is(ctx, A, Y, 1);
}

// Unary operators
expr(A) ::= NOT expr(X).    { A = lp_make_unary(ctx, LP_UOP_NOT, X); }
expr(A) ::= BITNOT expr(X). { A = lp_make_unary(ctx, LP_UOP_BITNOT, X); }
expr(A) ::= PLUS|MINUS(B) expr(X). [BITNOT] {
  if( @B==TK_MINUS ){
    A = lp_make_unary(ctx, LP_UOP_MINUS, X);
  }else{
    A = lp_make_unary(ctx, LP_UOP_PLUS, X);
  }
}

// -> and ->> operators
expr(A) ::= expr(B) PTR(C) expr(D). {
  int op = (C.n==3) ? LP_OP_PTR2 : LP_OP_PTR;
  A = lp_make_binary(ctx, op, B, D);
}

// BETWEEN
%type between_op {int}
between_op(A) ::= BETWEEN.     {A = 0;}
between_op(A) ::= NOT BETWEEN. {A = 1;}
expr(A) ::= expr(A) between_op(N) expr(X) AND expr(Y). [BETWEEN] {
  A = lp_make_between(ctx, A, X, Y, N);
}

// IN
%type in_op {int}
in_op(A) ::= IN.      {A = 0;}
in_op(A) ::= NOT IN.  {A = 1;}
expr(A) ::= expr(A) in_op(N) LP exprlist(Y) RP. [IN] {
  A = lp_make_in_list(ctx, A, Y, N);
}
expr(A) ::= LP select(X) RP. {
  A = lp_make_subquery(ctx, X);
}
expr(A) ::= expr(A) in_op(N) LP select(Y) RP.  [IN] {
  A = lp_make_in_select(ctx, A, Y, N);
}
expr(A) ::= expr(A) in_op(N) nm(Y) dbnm(Z) paren_exprlist(E). [IN] {
  /* IN table_name - treat as IN (SELECT * FROM table) */
  LpNode *t = lp_make_from_table(ctx, &Y, Z.n>0?&Z:0, 0);
  if(E) lp_from_table_set_args(ctx, t, E);
  LpNodeList *cols = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  LpNode *star = lp_make_result_star(ctx);
  lp_list_append(ctx, cols, star);
  LpNode *sel = lp_make_select(ctx, 0, cols, t, 0, 0, 0, 0, 0);
  A = lp_make_in_select(ctx, A, sel, N);
}
expr(A) ::= EXISTS LP select(Y) RP. {
  A = lp_make_exists(ctx, Y);
}

// CASE
expr(A) ::= CASE case_operand(X) case_exprlist(Y) case_else(Z) END. {
  A = lp_make_case(ctx, X, Y, Z);
}
%type case_exprlist {LpNodeList*}
case_exprlist(A) ::= case_exprlist(A) WHEN expr(Y) THEN expr(Z). {
  lp_list_append(ctx, A, Y);
  lp_list_append(ctx, A, Z);
}
case_exprlist(A) ::= WHEN expr(Y) THEN expr(Z). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  lp_list_append(ctx, A, Y);
  lp_list_append(ctx, A, Z);
}
%type case_else {LpNode*}
case_else(A) ::=  ELSE expr(X).         {A = X;}
case_else(A) ::=  .                     {A = 0;}
%type case_operand {LpNode*}
case_operand(A) ::= expr(A).
case_operand(A) ::= .                   {A = 0;}

// Expression list
%type exprlist {LpNodeList*}
%type nexprlist {LpNodeList*}

exprlist(A) ::= nexprlist(A).
exprlist(A) ::= .                            {A = 0;}
nexprlist(A) ::= nexprlist(A) COMMA expr(Y). {
  lp_list_append(ctx, A, Y);
}
nexprlist(A) ::= expr(Y). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  lp_list_append(ctx, A, Y);
}

// Parenthesized expression list
%type paren_exprlist {LpNodeList*}
paren_exprlist(A) ::= .   {A = 0;}
paren_exprlist(A) ::= LP exprlist(X) RP.  {A = X;}

/////////////////// CREATE INDEX /////////////////////////////

cmd ::= createkw uniqueflag(U) INDEX ifnotexists(NE) nm(X) dbnm(D)
        ON nm(Y) LP sortlist(Z) RP where_opt(W). {
  ctx->result = lp_make_create_index(ctx, &X, D.n>0?&D:0, &Y, Z, U, NE, W);
}

%type uniqueflag {int}
uniqueflag(A) ::= UNIQUE.  {A = 1;}
uniqueflag(A) ::= .        {A = 0;}

// eidlist (expression id list for CREATE INDEX, column lists etc.)
%type eidlist {LpNodeList*}
%type eidlist_opt {LpNodeList*}

eidlist_opt(A) ::= .                         {A = 0;}
eidlist_opt(A) ::= LP eidlist(X) RP.         {A = X;}
eidlist(A) ::= eidlist(A) COMMA nm(Y) collate(C) sortorder(Z). {
  LpNode *n = lp_make_id_node(ctx, &Y);
  (void)C; (void)Z;
  lp_list_append(ctx, A, n);
}
eidlist(A) ::= nm(Y) collate(C) sortorder(Z). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  LpNode *n = lp_make_id_node(ctx, &Y);
  (void)C; (void)Z;
  lp_list_append(ctx, A, n);
}

%type collate {int}
collate(C) ::= .              {C = 0;}
collate(C) ::= COLLATE ids.   {C = 1;}

/////////////////// DROP INDEX /////////////////////////////

cmd ::= DROP INDEX ifexists(E) fullname(X). {
  ctx->result = lp_make_drop(ctx, LP_DROP_INDEX, &X.name, &X.schema, E);
}

/////////////////// VACUUM /////////////////////////////

%type vinto {LpNode*}
cmd ::= VACUUM vinto(Y).                { ctx->result = lp_make_vacuum(ctx, 0, Y); }
cmd ::= VACUUM nm(X) vinto(Y).          { ctx->result = lp_make_vacuum(ctx, &X, Y); }
vinto(A) ::= INTO expr(X).              {A = X;}
vinto(A) ::= .                          {A = 0;}

/////////////////// PRAGMA /////////////////////////////

cmd ::= PRAGMA nm(X) dbnm(Z).                { ctx->result = lp_make_pragma(ctx, &X, Z.n>0?&Z:0, 0, 0); }
cmd ::= PRAGMA nm(X) dbnm(Z) EQ nmnum(Y).    { ctx->result = lp_make_pragma(ctx, &X, Z.n>0?&Z:0, &Y, 0); }
cmd ::= PRAGMA nm(X) dbnm(Z) LP nmnum(Y) RP. { ctx->result = lp_make_pragma(ctx, &X, Z.n>0?&Z:0, &Y, 0); }
cmd ::= PRAGMA nm(X) dbnm(Z) EQ minus_num(Y).
                                             { ctx->result = lp_make_pragma(ctx, &X, Z.n>0?&Z:0, &Y, 1); }
cmd ::= PRAGMA nm(X) dbnm(Z) LP minus_num(Y) RP.
                                             { ctx->result = lp_make_pragma(ctx, &X, Z.n>0?&Z:0, &Y, 1); }

nmnum(A) ::= plus_num(A).
nmnum(A) ::= nm(A).
nmnum(A) ::= ON(A).
nmnum(A) ::= DELETE(A).
nmnum(A) ::= DEFAULT(A).
%token_class number INTEGER|FLOAT.
plus_num(A) ::= PLUS number(X).       {A = X;}
plus_num(A) ::= number(A).
minus_num(A) ::= MINUS number(X).     {A = X;}

/////////////////// CREATE TRIGGER /////////////////////////////

cmd ::= createkw trigger_decl(A) BEGIN trigger_cmd_list(S) END. {
  ctx->result = A;
  if(A) A->u.create_trigger.body = *S;
}

%type trigger_decl {LpNode*}
trigger_decl(A) ::= temp(T) TRIGGER ifnotexists(NOERR) nm(B) dbnm(Z)
                    trigger_time(C) trigger_event(D)
                    ON fullname(E) foreach_clause when_clause(G). {
  LpNodeList *ucols = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  if(D.cols) *ucols = *D.cols;
  A = lp_make_create_trigger(ctx, &B, Z.n>0?&Z:0, T, NOERR, C, D.event,
                              &E.name, ucols, G, 0);
}

%type trigger_time {int}
trigger_time(A) ::= BEFORE.      { A = LP_TRIGGER_BEFORE; }
trigger_time(A) ::= AFTER.       { A = LP_TRIGGER_AFTER; }
trigger_time(A) ::= INSTEAD OF.  { A = LP_TRIGGER_INSTEAD_OF; }
trigger_time(A) ::= .            { A = LP_TRIGGER_BEFORE; }

%type trigger_event {struct {int event; LpNodeList *cols;}}
trigger_event(A) ::= DELETE.             {A.event = LP_TRIGGER_DELETE; A.cols = 0;}
trigger_event(A) ::= INSERT.             {A.event = LP_TRIGGER_INSERT; A.cols = 0;}
trigger_event(A) ::= UPDATE.             {A.event = LP_TRIGGER_UPDATE; A.cols = 0;}
trigger_event(A) ::= UPDATE OF idlist(X). {A.event = LP_TRIGGER_UPDATE; A.cols = X;}

foreach_clause ::= .
foreach_clause ::= FOR EACH ROW.

%type when_clause {LpNode*}
when_clause(A) ::= .             { A = 0; }
when_clause(A) ::= WHEN expr(X). { A = X; }

%type trigger_cmd_list {LpNodeList*}
trigger_cmd_list(A) ::= trigger_cmd_list(A) trigger_cmd(X) SEMI. {
  lp_list_append(ctx, A, X);
}
trigger_cmd_list(A) ::= trigger_cmd(X) SEMI. {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  lp_list_append(ctx, A, X);
}

tridxby ::= .
tridxby ::= INDEXED BY nm(X). {
  lp_error(ctx, LP_ERR_SYNTAX, lp_token_end(&X), "%u:%u: INDEXED BY not allowed in triggers", X.pos.line, X.pos.col);
}
tridxby ::= NOT INDEXED. {
  lp_error(ctx, LP_ERR_SYNTAX, ctx->cur_pos, "NOT INDEXED not allowed in triggers");
  /* no specific token to reference here */
}

%type trigger_cmd {LpNode*}
// UPDATE in trigger
trigger_cmd(A) ::=
   UPDATE orconf(R) xfullname(X) tridxby SET setlist(Y) from(F) where_opt(Z) scanpt.  {
  LpToken *s = X.schema.n>0 ? &X.schema : 0;
  LpToken *a = X.alias.n>0 ? &X.alias : 0;
  LpNode *upd = lp_make_update(ctx, R, &X.name, s, a, Y, F, Z, 0, 0, 0);
  A = lp_make_trigger_cmd(ctx, upd);
}
// INSERT in trigger
trigger_cmd(A) ::= scanpt insert_cmd(R) INTO
                   xfullname(X) idlist_opt(F) select(S) upsert(U) scanpt. {
  LpToken *s = X.schema.n>0 ? &X.schema : 0;
  LpToken *a = X.alias.n>0 ? &X.alias : 0;
  LpNode *ins = lp_make_insert(ctx, R, &X.name, s, a, F, S,
                                (U && U->kind==LP_UPSERT)?U:0, 0);
  A = lp_make_trigger_cmd(ctx, ins);
}
// DELETE in trigger
trigger_cmd(A) ::= DELETE FROM xfullname(X) tridxby where_opt(Y) scanpt. {
  LpToken *s = X.schema.n>0 ? &X.schema : 0;
  LpToken *a = X.alias.n>0 ? &X.alias : 0;
  LpNode *del = lp_make_delete(ctx, &X.name, s, a, Y, 0, 0, 0);
  A = lp_make_trigger_cmd(ctx, del);
}
// SELECT in trigger
trigger_cmd(A) ::= scanpt select(X) scanpt. {
  A = lp_make_trigger_cmd(ctx, X);
}

// RAISE expression
expr(A) ::= RAISE LP IGNORE RP.  {
  A = lp_make_raise(ctx, LP_RAISE_IGNORE, 0);
}
expr(A) ::= RAISE LP raisetype(T) COMMA expr(Z) RP.  {
  A = lp_make_raise(ctx, T, Z);
}

%type raisetype {int}
raisetype(A) ::= ROLLBACK.  {A = LP_RAISE_ROLLBACK;}
raisetype(A) ::= ABORT.     {A = LP_RAISE_ABORT;}
raisetype(A) ::= FAIL.      {A = LP_RAISE_FAIL;}

/////////////////// DROP TRIGGER /////////////////////////////

cmd ::= DROP TRIGGER ifexists(NOERR) fullname(X). {
  ctx->result = lp_make_drop(ctx, LP_DROP_TRIGGER, &X.name, &X.schema, NOERR);
}

/////////////////// ATTACH / DETACH /////////////////////////////

cmd ::= ATTACH database_kw_opt expr(F) AS expr(D) key_opt(K). {
  ctx->result = lp_make_attach(ctx, F, D, K);
}
cmd ::= DETACH database_kw_opt expr(D). {
  ctx->result = lp_make_detach(ctx, D);
}

%type key_opt {LpNode*}
key_opt(A) ::= .                     { A = 0; }
key_opt(A) ::= KEY expr(X).          { A = X; }

database_kw_opt ::= DATABASE.
database_kw_opt ::= .

/////////////////// REINDEX /////////////////////////////

cmd ::= REINDEX.                { ctx->result = lp_make_reindex(ctx, 0, 0); }
cmd ::= REINDEX nm(X) dbnm(Y). { ctx->result = lp_make_reindex(ctx, &X, Y.n>0?&Y:0); }

/////////////////// ANALYZE /////////////////////////////

cmd ::= ANALYZE.                { ctx->result = lp_make_analyze(ctx, 0, 0); }
cmd ::= ANALYZE nm(X) dbnm(Y). { ctx->result = lp_make_analyze(ctx, &X, Y.n>0?&Y:0); }

/////////////////// ALTER TABLE /////////////////////////////

cmd ::= ALTER TABLE fullname(X) RENAME TO nm(Z). {
  ctx->result = lp_make_alter_rename(ctx, &X.name, X.schema.n>0?&X.schema:0, &Z);
}
cmd ::= ALTER TABLE fullname(X) ADD kwcolumn_opt columnname carglist. {
  /* The column was added to cur_table via lp_add_column in columnname rule.
  ** We need to package it as an ALTER ADD COLUMN. */
  ctx->result = lp_make_alter_add_column(ctx, &X.name, X.schema.n>0?&X.schema:0);
}
cmd ::= ALTER TABLE fullname(X) DROP kwcolumn_opt nm(Y). {
  ctx->result = lp_make_alter_drop_column(ctx, &X.name, X.schema.n>0?&X.schema:0, &Y);
}
cmd ::= ALTER TABLE fullname(X) RENAME kwcolumn_opt nm(Y) TO nm(Z). {
  ctx->result = lp_make_alter_rename_column(ctx, &X.name, X.schema.n>0?&X.schema:0, &Y, &Z);
}

kwcolumn_opt ::= .
kwcolumn_opt ::= COLUMNKW.

/////////////////// CREATE VIRTUAL TABLE /////////////////////////////

cmd ::= create_vtab.                       { lp_end_create_table(ctx, 0); }
cmd ::= create_vtab LP vtabarglist RP.     { lp_end_create_table(ctx, 0); }
create_vtab ::= createkw VIRTUAL TABLE ifnotexists(E)
                nm(X) dbnm(Y) USING nm(Z). {
  ctx->cur_table = lp_make_create_vtable(ctx, &X, Y.n>0?&Y:0, &Z, E);
}
vtabarglist ::= vtabarg.
vtabarglist ::= vtabarglist COMMA vtabarg.
vtabarg ::= .
vtabarg ::= vtabarg vtabargtoken.
vtabargtoken ::= ANY.
vtabargtoken ::= lp anylist RP.
lp ::= LP.
anylist ::= .
anylist ::= anylist LP anylist RP.
anylist ::= anylist ANY.

/////////////////// CTE /////////////////////////////

%type wqlist {LpNodeList*}
%type wqitem {LpNode*}

with ::= .
with ::= WITH wqlist(W).              {
  LpNode *w = lp_make_with(ctx, 0, W);
  (void)w; /* WITH is consumed when attached to select */
}
with ::= WITH RECURSIVE wqlist(W).    {
  LpNode *w = lp_make_with(ctx, 1, W);
  (void)w;
}

%type wqas {int}
wqas(A)   ::= AS.                  {A = LP_MATERIALIZE_ANY;}
wqas(A)   ::= AS MATERIALIZED.     {A = LP_MATERIALIZE_YES;}
wqas(A)   ::= AS NOT MATERIALIZED. {A = LP_MATERIALIZE_NO;}
wqitem(A) ::= withnm(X) eidlist_opt(Y) wqas(M) LP select(Z) RP. {
  A = lp_make_cte(ctx, &X, Y, Z, M);
}
withnm(A) ::= nm(A).
wqlist(A) ::= wqitem(X). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  lp_list_append(ctx, A, X);
}
wqlist(A) ::= wqlist(A) COMMA wqitem(X). {
  lp_list_append(ctx, A, X);
}

/////////////////// WINDOW FUNCTIONS /////////////////////////////

%type windowdefn_list {LpNodeList*}
windowdefn_list(A) ::= windowdefn(X). {
  A = (LpNodeList*)arena_zeroalloc(ctx->arena, sizeof(LpNodeList));
  lp_list_append(ctx, A, X);
}
windowdefn_list(A) ::= windowdefn_list(A) COMMA windowdefn(Z). {
  lp_list_append(ctx, A, Z);
}

%type windowdefn {LpNode*}
windowdefn(A) ::= nm(X) AS LP window(Y) RP. {
  if(Y) {
    char *name = lp_token_str(ctx, &X);
    Y->u.window_def.name = name;
  }
  A = Y;
}

// Use LpNodeList* for windowdefn_list, but single LpNode* for windowdefn
// since they come back individually

%type window {LpNode*}
%type frame_opt {LpNode*}
%type part_opt {LpNodeList*}
%type filter_clause {LpNode*}
%type over_clause {LpNode*}
%type filter_over {LpNode*}
%type range_or_rows {int}

%type frame_bound {struct {int type; LpNode *expr;}}
%type frame_bound_s {struct {int type; LpNode *expr;}}
%type frame_bound_e {struct {int type; LpNode *expr;}}

window(A) ::= PARTITION BY nexprlist(X) orderby_opt(Y) frame_opt(Z). {
  A = lp_make_window_def(ctx, 0, X, Y, Z, 0);
}
window(A) ::= nm(W) PARTITION BY nexprlist(X) orderby_opt(Y) frame_opt(Z). {
  A = lp_make_window_def(ctx, 0, X, Y, Z, &W);
}
window(A) ::= ORDER BY sortlist(Y) frame_opt(Z). {
  A = lp_make_window_def(ctx, 0, 0, Y, Z, 0);
}
window(A) ::= nm(W) ORDER BY sortlist(Y) frame_opt(Z). {
  A = lp_make_window_def(ctx, 0, 0, Y, Z, &W);
}
window(A) ::= frame_opt(Z). {
  A = lp_make_window_def(ctx, 0, 0, 0, Z, 0);
}
window(A) ::= nm(W) frame_opt(Z). {
  A = lp_make_window_def(ctx, 0, 0, 0, Z, &W);
}

frame_opt(A) ::= . {
  LpNode *s = lp_make_frame_bound(ctx, LP_BOUND_UNBOUNDED_PRECEDING, 0);
  LpNode *e = lp_make_frame_bound(ctx, LP_BOUND_CURRENT_ROW, 0);
  A = lp_make_window_frame(ctx, LP_FRAME_RANGE, s, e, LP_EXCLUDE_NONE);
}
frame_opt(A) ::= range_or_rows(X) frame_bound_s(Y) frame_exclude_opt(Z). {
  LpNode *s = lp_make_frame_bound(ctx, Y.type, Y.expr);
  LpNode *e = lp_make_frame_bound(ctx, LP_BOUND_CURRENT_ROW, 0);
  A = lp_make_window_frame(ctx, X, s, e, Z);
}
frame_opt(A) ::= range_or_rows(X) BETWEEN frame_bound_s(Y) AND
                          frame_bound_e(Z) frame_exclude_opt(W). {
  LpNode *s = lp_make_frame_bound(ctx, Y.type, Y.expr);
  LpNode *e = lp_make_frame_bound(ctx, Z.type, Z.expr);
  A = lp_make_window_frame(ctx, X, s, e, W);
}

range_or_rows(A) ::= RANGE.   {A = LP_FRAME_RANGE;}
range_or_rows(A) ::= ROWS.    {A = LP_FRAME_ROWS;}
range_or_rows(A) ::= GROUPS.  {A = LP_FRAME_GROUPS;}

frame_bound_s(A) ::= frame_bound(X).         {A = X;}
frame_bound_s(A) ::= UNBOUNDED PRECEDING.    {A.type = LP_BOUND_UNBOUNDED_PRECEDING; A.expr = 0;}
frame_bound_e(A) ::= frame_bound(X).         {A = X;}
frame_bound_e(A) ::= UNBOUNDED FOLLOWING.    {A.type = LP_BOUND_UNBOUNDED_FOLLOWING; A.expr = 0;}

frame_bound(A) ::= expr(X) PRECEDING.  {A.type = LP_BOUND_PRECEDING; A.expr = X;}
frame_bound(A) ::= expr(X) FOLLOWING.  {A.type = LP_BOUND_FOLLOWING; A.expr = X;}
frame_bound(A) ::= CURRENT ROW.        {A.type = LP_BOUND_CURRENT_ROW; A.expr = 0;}

%type frame_exclude_opt {int}
frame_exclude_opt(A) ::= . {A = LP_EXCLUDE_NONE;}
frame_exclude_opt(A) ::= EXCLUDE frame_exclude(X). {A = X;}

%type frame_exclude {int}
frame_exclude(A) ::= NO OTHERS.   {A = LP_EXCLUDE_NO_OTHERS;}
frame_exclude(A) ::= CURRENT ROW. {A = LP_EXCLUDE_CURRENT_ROW;}
frame_exclude(A) ::= GROUP.       {A = LP_EXCLUDE_GROUP;}
frame_exclude(A) ::= TIES.        {A = LP_EXCLUDE_TIES;}

%type window_clause {LpNodeList*}
window_clause(A) ::= WINDOW windowdefn_list(B). { A = B; }

filter_over(A) ::= filter_clause(F) over_clause(O). {
  A = O;
  ctx->pending_filter = F;
}
filter_over(A) ::= over_clause(A). {
  ctx->pending_filter = 0;
}
filter_over(A) ::= filter_clause(F). {
  A = 0;
  ctx->pending_filter = F;
}

over_clause(A) ::= OVER LP window(Z) RP. { A = Z; }
over_clause(A) ::= OVER nm(Z). {
  A = lp_make_window_ref(ctx, &Z);
}

filter_clause(A) ::= FILTER LP WHERE expr(X) RP.  { A = X; }

// Synthesized tokens (not used in grammar rules, but needed for tokenizer)
%token
  COLUMN
  AGG_FUNCTION
  AGG_COLUMN
  TRUEFALSE
  ISNOT
  FUNCTION
  UPLUS
  UMINUS
  TRUTH
  REGISTER
  VECTOR
  SELECT_COLUMN
  IF_NULL_ROW
  ASTERISK
  SPAN
  ERROR
.

term(A) ::= QNUMBER(X). {
  /* Quoted number - treat as regular number */
  A = lp_make_literal_int(ctx, &X);
}

// The final tokens (must be last in grammar)
%token SPACE COMMENT ILLEGAL.
