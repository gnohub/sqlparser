/* Regression coverage for where schema. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_ast_internal.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static const char *label="startup";
static size_t checks,cases,reachable,unreachable;
#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FAIL|%s|line=%d|%s\n",label,__LINE__,#x);exit(1);}checks++; }while(0)
typedef struct {void *p[1024];size_t n;} Arena;
static void *own(Arena *a,size_t n){void*p=calloc(1,n);REQUIRE(p&&a->n<COUNT(a->p));a->p[a->n++]=p;return p;}
static ProtobufCMessage *msg(Arena*a,const ProtobufCMessageDescriptor*d){ProtobufCMessage*m=own(a,d->sizeof_message);REQUIRE(d->message_init);d->message_init(m);REQUIRE(m->descriptor==d);return m;}
static void close_arena(Arena*a){while(a->n)free(a->p[--a->n]);}
static void child(Arena*a,ProtobufCMessage*m,const ProtobufCFieldDescriptor*f,ProtobufCMessage*c){
 if(f->flags&PROTOBUF_C_FIELD_FLAG_ONEOF){int k=f->id;memcpy((char*)m+f->quantifier_offset,&k,sizeof(k));}
 if(f->label==PROTOBUF_C_LABEL_REPEATED){size_t n=1;ProtobufCMessage**items=own(a,sizeof(*items));items[0]=c;memcpy((char*)m+f->quantifier_offset,&n,sizeof(n));memcpy((char*)m+f->offset,&items,sizeof(items));}
 else memcpy((char*)m+f->offset,&c,sizeof(c));
}
static PgQuery__Node *leaf(Arena*a){PgQuery__Node*n=(void*)msg(a,&pg_query__node__descriptor);n->node_case=PG_QUERY__NODE__NODE_INTEGER;n->integer=(void*)msg(a,&pg_query__integer__descriptor);n->integer->ival=23;return n;}
typedef struct{const ProtobufCMessageDescriptor*d;const ProtobufCFieldDescriptor*via;unsigned dist;} Schema;
static Schema graph[512];static size_t gn;
static size_t index_of(const ProtobufCMessageDescriptor*d){size_t i;for(i=0;i<gn;i++)if(graph[i].d==d)return i;REQUIRE(gn<COUNT(graph));graph[gn]=(Schema){d,NULL,UINT_MAX};return gn++;}
static int where(const ProtobufCFieldDescriptor*f){return f->type==PROTOBUF_C_TYPE_MESSAGE&&f->label!=PROTOBUF_C_LABEL_REPEATED&&!strcmp(f->name,"where_clause")&&f->descriptor==&pg_query__node__descriptor;}
static void schema(void){size_t i;int change;index_of(&pg_query__node__descriptor);for(i=0;i<gn;i++){unsigned j;const ProtobufCMessageDescriptor*d=graph[i].d;for(j=0;j<d->n_fields;j++){const ProtobufCFieldDescriptor*f=d->fields+j;if(where(f)){graph[i].via=f;graph[i].dist=0;}if(f->type==PROTOBUF_C_TYPE_MESSAGE)index_of(f->descriptor);}}
 do{change=0;for(i=0;i<gn;i++){unsigned j;const ProtobufCMessageDescriptor*d=graph[i].d;for(j=0;j<d->n_fields;j++){const ProtobufCFieldDescriptor*f=d->fields+j;size_t k;if(f->type!=PROTOBUF_C_TYPE_MESSAGE)continue;k=index_of(f->descriptor);if(graph[k].dist!=UINT_MAX&&graph[k].dist+1<graph[i].dist){graph[i].dist=graph[k].dist+1;graph[i].via=f;change=1;}}}}while(change);
}
/* Distances strictly decrease, and construction independently records the one
 * terminal slot's exact address. Unreachable initialized leaves have no slots. */
static ProtobufCMessage *path(Arena*a,const ProtobufCMessageDescriptor*d,PgQuery__Node***expected){ProtobufCMessage*m=msg(a,d);Schema*s=graph+index_of(d);if(!s->via)return m;if(where(s->via)){child(a,m,s->via,&leaf(a)->base);*expected=(PgQuery__Node**)((char*)m+s->via->offset);}else child(a,m,s->via,path(a,s->via->descriptor,expected));return m;}
static void verify(PgQuery__Node*n,PgQuery__Node***slots,size_t expected){PgQuery__ParseResult root=PG_QUERY__PARSE_RESULT__INIT;PgQuery__RawStmt raw=PG_QUERY__RAW_STMT__INIT;PgQuery__RawStmt*raws[]={&raw};sqlparser_handle_t h={0};sqlparser_error_t e={0};size_t count=999,i;PgQuery__Node**slot=NULL;
 root.n_stmts=1;root.stmts=raws;raw.stmt=n;h.ast=&root;h.ast_state_bound=1;h.statement_count=1;h.representation=SQLPARSER_REPRESENTATION_LEGACY_AST;h.dialect=SQLPARSER_DIALECT_POSTGRESQL;
 REQUIRE(sqlparser_count_statement_where_clauses(&h,0,&count,&e)==SQLPARSER_STATUS_OK);REQUIRE(count==expected);
 for(i=0;i<expected;i++){REQUIRE(sqlparser_get_statement_where_clause_slot(&h,0,i,&slot,&e)==SQLPARSER_STATUS_OK);REQUIRE(slot==slots[i]);}
 REQUIRE(sqlparser_get_statement_where_clause_slot(&h,0,expected,&slot,&e)==SQLPARSER_STATUS_INVALID_ARGUMENT);REQUIRE(!slot);REQUIRE(!strcmp(e.message,"where selector is out of range"));
 printf("SCHEMA|%s|slots=%zu\n",label,count);cases++;
}
static void variants(void){const ProtobufCMessageDescriptor*d=&pg_query__node__descriptor;unsigned i;int copied,null;char name[180];REQUIRE(d->n_fields==268);
 for(copied=0;copied<2;copied++)for(i=0;i<d->n_fields;i++)for(null=0;null<2;null++){Arena a={0};ProtobufCMessageDescriptor cd=*d;const ProtobufCFieldDescriptor*f=d->fields+i;PgQuery__Node*n=(void*)msg(&a,d);PgQuery__Node**expected=NULL;ProtobufCMessage*c=NULL;
 snprintf(name,sizeof(name),"%s-%03u-%s-%s",copied?"copied":"canonical",f->id,f->name,null?"null":"live");label=name;
 REQUIRE(f->type==PROTOBUF_C_TYPE_MESSAGE&&(f->flags&PROTOBUF_C_FIELD_FLAG_ONEOF));if(!null)c=path(&a,f->descriptor,&expected);child(&a,&n->base,f,c);if(copied)n->base.descriptor=&cd;verify(n,&expected,expected?1:0);
 if(!copied&&!null){if(expected)reachable++;else unreachable++;}close_arena(&a);}
}
static void unknowns(void){int tags[]={0,-1,269,INT_MAX};int copied;size_t i;char name[100];for(copied=0;copied<2;copied++)for(i=0;i<COUNT(tags);i++){Arena a={0};ProtobufCMessageDescriptor cd=pg_query__node__descriptor;PgQuery__Node**expected=NULL;PgQuery__Node*n=(void*)msg(&a,&pg_query__node__descriptor);n->node_case=(PgQuery__Node__NodeCase)tags[i];n->select_stmt=(void*)path(&a,&pg_query__select_stmt__descriptor,&expected);if(copied)n->base.descriptor=&cd;snprintf(name,sizeof(name),"%s-unknown-%d-inactive-live-select",copied?"copied":"canonical",tags[i]);label=name;verify(n,NULL,0);close_arena(&a);}}
typedef struct{PgQuery__Node node;PgQuery__SelectStmt*extra;} Extended;
static void custom(void){int active;char name[100];for(active=0;active<3;active++){Arena a={0};Extended*n=own(&a,sizeof(*n));ProtobufCMessageDescriptor cd=pg_query__node__descriptor;ProtobufCFieldDescriptor fields[269];unsigned names[269],i;ProtobufCIntRange ranges[3]={{1,0},{1001,268},{0,269}};PgQuery__Node**slots[2];size_t count=0;
 pg_query__node__init(&n->node);memcpy(fields,cd.fields,268*sizeof(*fields));fields[268]=(ProtobufCFieldDescriptor){.name="extra",.id=1001,.label=PROTOBUF_C_LABEL_NONE,.type=PROTOBUF_C_TYPE_MESSAGE,.offset=offsetof(Extended,extra),.descriptor=&pg_query__select_stmt__descriptor};for(i=0;i<269;i++)names[i]=i;for(i=1;i<269;i++){unsigned j=i,v=names[i];while(j&&strcmp(fields[names[j-1]].name,fields[v].name)>0){names[j]=names[j-1];j--;}names[j]=v;}
 cd.fields=fields;cd.n_fields=269;cd.sizeof_message=sizeof(*n);cd.fields_sorted_by_name=names;cd.field_ranges=ranges;cd.n_field_ranges=2;n->node.base.descriptor=&cd;
 if(active){PgQuery__Node**slot=NULL;n->node.select_stmt=(void*)path(&a,&pg_query__select_stmt__descriptor,&slot);n->node.node_case=active==1?PG_QUERY__NODE__NODE_SELECT_STMT:(PgQuery__Node__NodeCase)999;if(active==1)slots[count++]=slot;}
 n->extra=(void*)path(&a,&pg_query__select_stmt__descriptor,&slots[count]);count++;snprintf(name,sizeof(name),"custom-extra-field-active-%d",active);label=name;verify(&n->node,slots,count);close_arena(&a);}}
static void empties(void){Arena a={0};PgQuery__Node*n=(void*)msg(&a,&pg_query__node__descriptor);PgQuery__SelectStmt*s=(void*)msg(&a,&pg_query__select_stmt__descriptor);PgQuery__Node**slot=&s->where_clause;PgQuery__Node*items[]={leaf(&a)};n->node_case=PG_QUERY__NODE__NODE_SELECT_STMT;n->select_stmt=s;
 label="empty-select-no-clause";verify(n,NULL,0);s->target_list=items;s->n_target_list=1;label="empty-select-with-target";verify(n,&slot,1);s->n_target_list=0;s->having_clause=leaf(&a);label="having-only-select-empty-where";verify(n,&slot,1);s->having_clause=NULL;s->op=PG_QUERY__SET_OPERATION__SETOP_UNION;label="set-container-no-synthetic-slot";verify(n,NULL,0);s->n_values_lists=1;s->values_lists=items;label="values-no-synthetic-slot";verify(n,NULL,0);close_arena(&a);
}

static void deep_and_siblings(void){
 Arena a={0};PgQuery__Node*nodes[129];PgQuery__Node**slots[129];size_t i;
 label="bounded-129-select-schema-chain";
 for(i=0;i<COUNT(nodes);i++){PgQuery__SelectStmt*t=(void*)msg(&a,&pg_query__select_stmt__descriptor);nodes[i]=(void*)msg(&a,&pg_query__node__descriptor);nodes[i]->node_case=PG_QUERY__NODE__NODE_SELECT_STMT;nodes[i]->select_stmt=t;t->where_clause=leaf(&a);slots[COUNT(nodes)-1-i]=&t->where_clause;}
 for(i=0;i+1<COUNT(nodes);i++){PgQuery__SelectStmt*t=nodes[i]->select_stmt;t->n_target_list=1;t->target_list=own(&a,sizeof(*t->target_list));t->target_list[0]=nodes[i+1];}
 verify(nodes[0],slots,COUNT(slots));close_arena(&a);
 {Arena b={0};PgQuery__Node*root=(void*)msg(&b,&pg_query__node__descriptor);PgQuery__SelectStmt*t=(void*)msg(&b,&pg_query__select_stmt__descriptor);PgQuery__Node*items[3],*having=(void*)msg(&b,&pg_query__node__descriptor);PgQuery__Node**ordered[4];
 root->node_case=PG_QUERY__NODE__NODE_SELECT_STMT;root->select_stmt=t;t->n_target_list=3;t->target_list=items;items[0]=(void*)msg(&b,&pg_query__node__descriptor);items[0]->node_case=PG_QUERY__NODE__NODE_SELECT_STMT;items[0]->select_stmt=(void*)path(&b,&pg_query__select_stmt__descriptor,&ordered[0]);items[1]=NULL;items[2]=(void*)msg(&b,&pg_query__node__descriptor);items[2]->node_case=PG_QUERY__NODE__NODE_SELECT_STMT;items[2]->select_stmt=(void*)path(&b,&pg_query__select_stmt__descriptor,&ordered[1]);ordered[2]=&t->where_clause;t->having_clause=having;having->node_case=PG_QUERY__NODE__NODE_SELECT_STMT;having->select_stmt=(void*)path(&b,&pg_query__select_stmt__descriptor,&ordered[3]);
 label="repeated-siblings-null-item-before-empty-where-before-having-subquery";verify(root,ordered,4);close_arena(&b);}
}
static void empty_gates(void){Arena a={0};PgQuery__Node*n=(void*)msg(&a,&pg_query__node__descriptor);PgQuery__Node**slot;PgQuery__CopyStmt*c=(void*)msg(&a,&pg_query__copy_stmt__descriptor);PgQuery__OnConflictClause*o=(void*)msg(&a,&pg_query__on_conflict_clause__descriptor);PgQuery__Constraint*k=(void*)msg(&a,&pg_query__constraint__descriptor);
 n->node_case=PG_QUERY__NODE__NODE_COPY_STMT;n->copy_stmt=c;slot=&c->where_clause;label="copy-to-empty-excluded";verify(n,NULL,0);c->is_from=1;label="copy-from-empty-allowed";verify(n,&slot,1);c->is_from=0;c->where_clause=leaf(&a);label="copy-to-present-clause";verify(n,&slot,1);
 n->node_case=PG_QUERY__NODE__NODE_ON_CONFLICT_CLAUSE;n->on_conflict_clause=o;slot=&o->where_clause;o->action=PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_NOTHING;label="on-conflict-nothing-empty-excluded";verify(n,NULL,0);o->action=PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_UPDATE;label="on-conflict-update-empty-allowed";verify(n,&slot,1);o->action=PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_NOTHING;o->where_clause=leaf(&a);label="on-conflict-nothing-present-clause";verify(n,&slot,1);
 n->node_case=PG_QUERY__NODE__NODE_CONSTRAINT;n->constraint=k;slot=&k->where_clause;label="ordinary-constraint-empty-excluded";verify(n,NULL,0);k->contype=PG_QUERY__CONSTR_TYPE__CONSTR_EXCLUSION;label="exclusion-constraint-empty-allowed";verify(n,&slot,1);k->contype=PG_QUERY__CONSTR_TYPE__CONSTR_CHECK;k->where_clause=leaf(&a);label="check-constraint-present-clause";verify(n,&slot,1);close_arena(&a);
}

int main(void){schema();variants();unknowns();custom();empties();deep_and_siblings();empty_gates();label="summary";REQUIRE(reachable+unreachable==268);printf("SUMMARY|where_schema|cases=%zu|checks=%zu|schema_messages=%zu|reachable_variants=%zu|leaf_variants=%zu\n",cases,checks,gn,reachable,unreachable);return 0;}
