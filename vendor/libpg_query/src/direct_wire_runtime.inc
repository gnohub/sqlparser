/* Direct protobuf encoder. Include after existing enum conversion functions.
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
static size_t dw_varint_length(uint64_t v) {
 size_t n;
 if(v<128U)return 1U;
 if(v<16384U)return 2U;
 n=3U;v>>=14U;
 while(v>=128U){v>>=7U;n++;}
 return n;
}
static void dw_bytes(DirectWire *c,const void *bytes,size_t n) {
 if(c->status!=1)return;
 if(n>SIZE_MAX-c->position){c->status=-1;return;}
 if(c->writing) {
  if(c->position>c->output_size || n>c->output_size-c->position){c->status=-2;return;}
  if(n)memcpy(c->output+c->position,bytes,n);
 }
 c->position+=n;
}
static void dw_varint(DirectWire *c,uint64_t v) {
 size_t n,position;unsigned char *out;
 if(c->status!=1)return;
 n=dw_varint_length(v);position=c->position;
 if(n>SIZE_MAX-position){c->status=-1;return;}
 if(!c->writing){c->position=position+n;return;}
 if(position>c->output_size || n>c->output_size-position){c->status=-2;return;}
 /* Reserve the entire varint before writing: failures cannot leave a partial
  * varint behind, and the common tag/short-value paths need no temporary copy. */
 out=c->output+position;
 if(n==1U)out[0]=(unsigned char)v;
 else if(n==2U){out[0]=(unsigned char)(v|128U);out[1]=(unsigned char)(v>>7U);}
 else {
  while(v>=128U){*out++=(unsigned char)(v|128U);v>>=7U;}
  *out=(unsigned char)v;
 }
 c->position=position+n;
}
/* The sizing traversal never changes writing. Account for both envelope
 * varints together without entering the encoder or reloading its mode twice.
 * Keep the original sequential updates at overflow edges: the tag may fit
 * even when the following length does not. No output is written here. */
static void dw_measure_envelope(DirectWire *c,unsigned field,size_t length) {
 uint64_t tag=((uint64_t)field<<3U)|2U;
 size_t n;
 if(c->status!=1)return;
 n=dw_varint_length(tag)+dw_varint_length(length);
 if(n>SIZE_MAX-c->position){dw_varint(c,tag);dw_varint(c,length);return;}
 c->position+=n;
}
static void dw_scalar(DirectWire *c,unsigned field,uint64_t value) {
 if(value==0 || c->status!=1)return;
 /* Sizing has no output side effects. Account for the tag and value together,
  * but retain the sequential encoder at overflow edges so a fitting tag is
  * still reflected in position when the value cannot fit. */
 if(!c->writing) {
  size_t n=dw_varint_length((uint64_t)field<<3U)+dw_varint_length(value);
  if(n<=SIZE_MAX-c->position){c->position+=n;return;}
 }
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
  dw_measure_envelope(c,field,length);
 }
 done:c->depth--;
}
/* AConst's primitive children have no descendants. Retain the same depth
 * boundary, but derive their tiny message sizes locally rather than reserving
 * cache frames. The source tree remains immutable between the two passes. */
static int dw_primitive_message_allowed(DirectWire *c) {
 if(c->status!=1)return 0;
 if(c->depth>=256U){
#ifdef DIRECT_WIRE_REPORT
 fprintf(stderr,"DIRECT_WIRE_FALLBACK_REASON depth_limit\n");
#endif
 c->status=0;return 0;}
 return 1;
}
static void dw_primitive_int32_message(DirectWire *c,unsigned field,int32_t value) {
 uint64_t encoded;size_t start,length;
 if(!dw_primitive_message_allowed(c))return;
 encoded=(uint64_t)(int64_t)value;
 if(c->writing) {
  length=value?1U+dw_varint_length(encoded):0U;
  dw_varint(c,((uint64_t)field<<3U)|2U);dw_varint(c,length);
 }
 start=c->position;dw_scalar(c,1,encoded);
 /* Measure the body before its envelope, exactly as dw_message does, so
  * partial sizing state at SIZE_MAX is unchanged. This body is at most 11. */
 if(!c->writing && c->status==1)dw_measure_envelope(c,field,c->position-start);
}
static void dw_primitive_string_message(DirectWire *c,unsigned field,const char *text,int32_t location) {
 uint64_t encoded;size_t n,start,length,overhead;
 if(!dw_primitive_message_allowed(c))return;
 n=text && *text?strlen(text):0U;encoded=(uint64_t)(int64_t)location;
 if(c->writing) {
  overhead=n?1U+dw_varint_length(n):0U;
  if(location)overhead+=1U+dw_varint_length(encoded);
  /* Check before adding: the body must fit the same uint32 cache length
   * limit on both 32-bit and 64-bit hosts, even without a cache entry. */
  if(n>UINT32_MAX-overhead){c->status=-1;return;}
  length=n+overhead;
  dw_varint(c,((uint64_t)field<<3U)|2U);dw_varint(c,length);
 }
 start=c->position;
 if(n)dw_data_field(c,1,text,n);
 dw_scalar(c,2,encoded);
 if(!c->writing && c->status==1) {
  length=c->position-start;
  if(length>UINT32_MAX){c->status=-1;return;}
  dw_measure_envelope(c,field,length);
 }
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
