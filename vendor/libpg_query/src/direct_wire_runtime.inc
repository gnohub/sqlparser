/* Isolated prototype. Include after existing enum conversion functions.
 * Return 1: independently malloc-owned output; 0: unsupported (normal fallback);
 * -1: allocation/size failure or internal size mismatch (no output published).
 * No public ABI, ownership contract, or observer behavior is changed here. */
#include <stdint.h>
#include <limits.h>
typedef struct {
 uint32_t *sizes; size_t count,capacity,index,position,output_size;
 unsigned char *output; unsigned depth; int writing,status,certifying;
} DirectWire;
typedef void (*DirectWireVisitor)(DirectWire *,const void *);
static void dw_Node(DirectWire *,const void *);
static size_t dw_varint_length(uint64_t v) { size_t n=1; while(v>=128){v>>=7;n++;} return n; }
static void dw_bytes(DirectWire *c,const void *bytes,size_t n) {
 if(c->status!=1)return;
 if(n>SIZE_MAX-c->position){c->status=-1;return;}
 if(c->writing) {
  if(c->position>c->output_size || n>c->output_size-c->position){c->status=-2;return;}
  if(n)memcpy(c->output+c->position,bytes,n);
 }
 c->position+=n;
}
static void dw_measure_bytes(DirectWire *c,size_t n) {
 if(c->status!=1)return;
 if(n>SIZE_MAX-c->position){c->status=-1;return;}
 c->position+=n;
}
static void dw_varint(DirectWire *c,uint64_t v) {
 unsigned char bytes[10];size_t n=0;
 if(!c->writing){dw_measure_bytes(c,dw_varint_length(v));return;}
 do {bytes[n++]=(unsigned char)((v&127U)|(v>=128U?128U:0U));v>>=7U;}while(v);
 dw_bytes(c,bytes,n);
}
static void dw_scalar(DirectWire *c,unsigned field,uint64_t value) {
 if(value==0 || c->status!=1)return;
 dw_varint(c,(uint64_t)field<<3U);dw_varint(c,value);
}
static void dw_data_field(DirectWire *c,unsigned field,const void *data,size_t n) {
 if(c->status!=1)return;
 dw_varint(c,((uint64_t)field<<3U)|2U);dw_varint(c,n);dw_bytes(c,data,n);
}
static void dw_string(DirectWire *c,unsigned field,const char *text) {
 if(text && *text && c->status==1)dw_data_field(c,field,text,strlen(text));
}
static void dw_double(DirectWire *c,unsigned field,double value) {
 uint64_t bits; unsigned char bytes[8]; unsigned i;
 if(value==0.0 || c->status!=1)return;
 memcpy(&bits,&value,8);
 for(i=0;i<8;i++)bytes[i]=(unsigned char)(bits>>(8U*i));
 dw_varint(c,((uint64_t)field<<3U)|1U);dw_bytes(c,bytes,8);
}
static int dw_slot(DirectWire *c,size_t *slot) {
 if(c->count==c->capacity) {
  size_t capacity=c->capacity?c->capacity*2U:128U; uint32_t *next;
  if(capacity<c->capacity || capacity>SIZE_MAX/sizeof(*next)){c->status=-1;return 0;}
  next=realloc(c->sizes,capacity*sizeof(*next));
  if(!next){c->status=-1;return 0;}
  c->sizes=next;c->capacity=capacity;
 }
 *slot=c->count++;return 1;
}
static void dw_message(DirectWire *c,unsigned field,DirectWireVisitor visitor,const void *object) {
 size_t slot,start,length;
 if(c->status!=1)return;
 if(c->depth>=256U){
#ifdef DIRECT_WIRE_REPORT
 fprintf(stderr,"DIRECT_WIRE_FALLBACK_REASON depth_limit\n");
#endif
 c->status=0;return;} /* Deep input uses existing converter. */
 c->depth++;
 if(c->writing) {
  if(c->index>=c->count){c->status=-2;goto done;}
  length=c->sizes[c->index++];
  dw_varint(c,((uint64_t)field<<3U)|2U);dw_varint(c,length);
  start=c->position;visitor(c,object);
  if(c->status==1 && c->position-start!=length)c->status=-2;
 } else {
  if(!dw_slot(c,&slot))goto done;
  start=c->position;visitor(c,object);
  if(c->status!=1)goto done;
  length=c->position-start;
  if(length>UINT32_MAX){c->status=-1;goto done;}
  c->sizes[slot]=(uint32_t)length;
  dw_varint(c,((uint64_t)field<<3U)|2U);dw_varint(c,length);
 }
 done:c->depth--;
}
static void dw_list(DirectWire *c,unsigned field,const List *list) {
 const ListCell *cell;
 if(!list || c->status!=1)return;
 if(!IsA(list,List)){
#ifdef DIRECT_WIRE_REPORT
 fprintf(stderr,"DIRECT_WIRE_FALLBACK_REASON non_node_list=%d\n",(int)nodeTag(list));
#endif
 c->status=0;return;}
 foreach(cell,list) { dw_message(c,field,dw_Node,lfirst(cell)); if(c->status!=1)return; }
}
#include "direct_wire_visitors.inc"
static int pg_query_try_direct_wire_mode(const void *object,PgQueryProtobuf *result,
 int certify,size_t *statement_count) {
 DirectWire c={0}; c.status=1; c.certifying=certify; result->data=NULL;result->len=0;
 if(statement_count)*statement_count=0U;
 dw_ParseResult(&c,object);
 if(c.status!=1){free(c.sizes);return c.status;}
 c.output_size=c.position;c.position=0;c.writing=1;c.certifying=0;
 c.output=pg_query_protobuf_alloc_output(c.output_size?c.output_size:1U);
 if(!c.output){free(c.sizes);return -1;}
 dw_ParseResult(&c,object);
 if(c.status==1 && (c.position!=c.output_size || c.index!=c.count))c.status=-2;
#ifdef DIRECT_WIRE_REPORT
 fprintf(stderr,"direct_wire_result,%d,%zu,%zu,%zu\n",c.status,c.output_size,c.count,c.capacity*sizeof(*c.sizes));
#endif
 free(c.sizes);
 if(c.status!=1){
  free(c.output);
#ifdef DIRECT_WIRE_COMPARE
  if(c.status==-2){fprintf(stderr,"DIRECT_WIRE_INTERNAL_SIZE_MISMATCH\n");abort();}
#endif
  return c.status<0?-1:0;
 }
 result->data=(char *)c.output;result->len=c.output_size;
 if(statement_count)*statement_count=object?(size_t)list_length((const List *)object):0U;
 return 1;
}

/* Generic encoding retains its existing contract and whitelist-independent behavior. */
static int pg_query_try_direct_wire(const void *object,PgQueryProtobuf *result) {
 return pg_query_try_direct_wire_mode(object,result,0,NULL);
}
/* Only the first existing sizing traversal proves this conservative subset.
 * No AExpr/Merge/hierarchy node can reach a successful certificate. */
static int pg_query_try_direct_wire_certified(const void *object,PgQueryProtobuf *result,
 size_t *statement_count) {
 return pg_query_try_direct_wire_mode(object,result,1,statement_count);
}
