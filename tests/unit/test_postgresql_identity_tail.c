#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#ifndef PG_DIALECT_SOURCE
#define PG_DIALECT_SOURCE "../../src/dialect/sqlparser_dialect_postgresql.c"
#endif
#include PG_DIALECT_SOURCE
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line=%d\n",__LINE__); return 1; } } while(0)
int main(void) {
 const char ws[]=" \t\n\r\v\f"; char b[1100]; size_t i,n;
 CHECK(!sqlparser_postgresql_identity_tail_may_match("",0));
 CHECK(!sqlparser_postgresql_identity_tail_may_match(ws,6));
 CHECK(sqlparser_postgresql_identity_tail_may_match(")",1));
 CHECK(sqlparser_postgresql_identity_tail_may_match(";",1));
 CHECK(!sqlparser_postgresql_identity_tail_may_match("/",1));
 for(i=0;i<6;i++) { b[0]=')'; memset(b+1,ws[i],63); CHECK(sqlparser_postgresql_identity_tail_may_match(b,64)); b[0]='/'; CHECK(!sqlparser_postgresql_identity_tail_may_match(b,64)); }
 for(n=64;n<=1000;n++) { b[0]='/';memset(b+1,' ',n);CHECK(sqlparser_postgresql_identity_tail_may_match(b,n+1)); }
 memset(b,' ',64);CHECK(!sqlparser_postgresql_identity_tail_may_match(b,64));
 memset(b,' ',65);CHECK(sqlparser_postgresql_identity_tail_may_match(b,65));
 CHECK(sqlparser_postgresql_identity_tail_may_match("-- )\n",5));
 CHECK(sqlparser_postgresql_identity_tail_may_match("-- ;\n",5));
 CHECK(!sqlparser_postgresql_identity_tail_may_match("/* ) */\n",8));
 puts("ordinary tail-gate boundaries PASS (no fault hooks)");return 0;
}
