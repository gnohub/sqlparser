/* Production TU, an independently frozen validator plus all private helpers,
 * and an independent scalar quote/comment scanner. No production bypass. */
#include <ctype.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_internal.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "returning guard:%d case=%zu locale=%s: %s\n", __LINE__, comparisons, setlocale(LC_CTYPE,NULL), #x); abort(); } } while (0)
static size_t comparisons, actual_scans, frozen_scans, absent_routes, present_routes;
static size_t actual_idents, frozen_idents;
static int actual_isalnum(int c) { ++actual_idents; return isalnum(c); }
static int frozen_isalnum(int c) { ++frozen_idents; return isalnum(c); }
static size_t guarded_skip(sqlparser_dialect_t dialect, const char *sql, size_t position)
{ ++actual_scans; return sqlparser_public_skip_quoted_or_comment(dialect, sql, position); }
#define sqlparser_public_skip_quoted_or_comment guarded_skip
#undef isalnum
#define isalnum actual_isalnum
#include "../../src/dialect/sqlparser_dialect_dml_result.c"
#undef isalnum
#undef sqlparser_public_skip_quoted_or_comment
#include "../oracle_returningguard/frozen_surface_scanner.inc"
static size_t frozen_skip_quoted_or_comment(sqlparser_dialect_t dialect, const char *sql, size_t position)
{ ++frozen_scans; return reference_skip_quoted_or_comment(dialect, sql, position); }
#define isalnum frozen_isalnum
#include "../oracle_returningguard/frozen_validator.inc"
#undef isalnum
#include "../oracle_returningguard/cases.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static int scalar_raw_match(const char *sql)
{
    const unsigned char *p = (const unsigned char *)sql;
    static const unsigned char target[] = "returning";
    for (; *p; ++p) {
        size_t i;
        for (i=0; i<9U; ++i) if (!p[i] || tolower(p[i])!=tolower(target[i])) break;
        if (i==9U) return 1;
    }
    return 0;
}
static void compare_one(const char *sql, int dialect, int allow_return, int plain)
{
    sqlparser_error_t a,b; sqlparser_status_t ar,br; int may=sql ? scalar_raw_match(sql) : 0;
    memset(&a,0xa5,sizeof(a)); memcpy(&b,&a,sizeof(b)); actual_scans=frozen_scans=0;
    ar=sqlparser_dialect_returning_into_validate((sqlparser_dialect_t)dialect,sql,allow_return,plain,&a);
    br=frozen_sqlparser_dialect_returning_into_validate((sqlparser_dialect_t)dialect,sql,allow_return,plain,&b);
    CHECK(ar==br); CHECK(!memcmp(&a,&b,sizeof(a)));
    CHECK(ar==sqlparser_dialect_returning_into_validate((sqlparser_dialect_t)dialect,sql,allow_return,plain,NULL));
    CHECK(br==frozen_sqlparser_dialect_returning_into_validate((sqlparser_dialect_t)dialect,sql,allow_return,plain,NULL));
#if CHAR_BIT == 8 && UCHAR_MAX == 255
    if (sql && dialect==SQLPARSER_DIALECT_ORACLE && !allow_return) {
        CHECK(sqlparser_returning_into_raw_may_match(sql)==may);
        if (!may) { CHECK(actual_scans==0U); CHECK(ar==SQLPARSER_STATUS_OK); ++absent_routes; }
        else { CHECK(actual_scans==frozen_scans); ++present_routes; }
    } else CHECK(actual_scans==frozen_scans);
#else
    (void)may;
    CHECK(actual_scans==frozen_scans);
#endif
    ++comparisons;
}
static void all_modes(const char *sql)
{
    int d,r,p;
    for(d=-1;d<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER+1;d++) for(r=0;r<2;r++) for(p=0;p<2;p++) compare_one(sql,d,r,p);
}
static void corpus(void)
{
    size_t i,j; char word[10], input[160];
    all_modes(NULL);
    for(i=0;i<COUNT(returningguard_cases);i++) all_modes(returningguard_cases[i]);
    for(i=0;i<512U;i++) {
        for(j=0;j<9U;j++) word[j]=(char)((i&(1U<<j)) ? "RETURNING"[j] : "returning"[j]);
        word[9]=0; snprintf(input,sizeof(input),"UPDATE t SET a=1 %s a,b INTO :out",word); all_modes(input);
    }
    /* Exact-size allocation ends at NUL, including every partial keyword. */
    for(i=0;i<=9U;i++) for(j=0;j<16U;j++) {
        char *p=malloc(j+i+1U); CHECK(p); memset(p,'r',j); memcpy(p+j,"returning",i); p[j+i]=0; all_modes(p); free(p);
    }
    /* An embedded NUL ends the API input regardless of the following bytes. */
    { char x[]="SELECT 1\0RETURNING"; all_modes(x); }
}
static void byte_matrix(void)
{
    size_t i; unsigned v; char word[10], input[96];
    for(i=0;i<9U;i++) for(v=1;v<=255U;v++) {
        memcpy(word,"returning",10U); word[i]=(char)(unsigned char)v;
        snprintf(input,sizeof(input),"UPDATE t SET a=1 %s a,b INTO :out",word);
        compare_one(input,SQLPARSER_DIALECT_ORACLE,0,0); compare_one(input,SQLPARSER_DIALECT_ORACLE,0,1);
    }
}
static uint32_t state=UINT32_C(0x82041937);
static uint32_t random_value(void) { state^=state<<13; state^=state>>17; state^=state<<5; return state; }
static void fuzz(void)
{
    static const unsigned char chars[]="returningRETURNINGqQnNuU&$'\"\\/*-:;()[]{}<>_#123 ,\r\n\t\200\235\335\377";
    size_t i,j,n; char s[129];
    for(i=0;i<6000U;i++) {
        n=random_value()%128U; for(j=0;j<n;j++) s[j]=(char)chars[random_value()%(sizeof(chars)-1U)]; s[n]=0;
        if(n>=9U && i%3U==0U) memcpy(s+random_value()%(n-8U),"returning",9U);
        all_modes(s);
    }
}
int main(int argc,char **argv)
{
    static const char *const locales[]={"C","C.UTF-8","tr_TR","tr_TR.UTF-8","en_US","de_DE"};
    size_t i,accepted=0; FILE *list=NULL; char locale[256];
    CHECK(setlocale(LC_CTYPE,"C"));
    if(argc==3 && !strcmp(argv[1],"--fixture")) {
        FILE *f=fopen(argv[2],"rb"); long n; char *sql;
        CHECK(f && !fseek(f,0,SEEK_END)); n=ftell(f); CHECK(n>=0 && !fseek(f,0,SEEK_SET));
        sql=malloc((size_t)n+1U); CHECK(sql); CHECK(fread(sql,1,(size_t)n,f)==(size_t)n); fclose(f); sql[n]=0;
        compare_one(sql,SQLPARSER_DIALECT_ORACLE,0,0);
        printf("fixture bytes=%ld; raw_may_match=%d; actual/frozen scan calls=%zu/%zu; ident calls=%zu/%zu (two validation calls each)\n",n,sqlparser_returning_into_raw_may_match(sql),actual_scans,frozen_scans,actual_idents,frozen_idents);
        free(sql); return 0;
    }
    corpus(); fuzz();
    for(i=0;i<COUNT(locales);i++) if(setlocale(LC_CTYPE,locales[i])) { corpus(); byte_matrix(); ++accepted; }
    if(argc==3 && !strcmp(argv[1],"--locales")) {
        list=fopen(argv[2],"r"); CHECK(list);
        while(fgets(locale,sizeof(locale),list)) { locale[strcspn(locale,"\r\n")]=0; if(setlocale(LC_CTYPE,locale)) { byte_matrix(); ++accepted; } }
        CHECK(!ferror(list)); fclose(list);
    }
    CHECK(setlocale(LC_CTYPE,"C"));
    printf("RETURNING frozen differential: %zu comparisons; %zu absence and %zu retained routes; %zu locale runs\n",comparisons,absent_routes,present_routes,accepted);
    return 0;
}
