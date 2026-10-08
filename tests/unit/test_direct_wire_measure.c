/* Private sizing-state differential tests. Include the implementation with
 * renamed external entry points rather than exposing test APIs or layouts. */
#define pg_query_nodes_to_protobuf test_nodes_to_protobuf
#define pg_query_nodes_to_protobuf_observed test_nodes_to_protobuf_observed
#define pg_query_nodes_to_protobuf_certified test_nodes_to_protobuf_certified
/* Match the vendor build's warning boundary while keeping this test strict. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../../vendor/libpg_query/src/pg_query_outfuncs_protobuf.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#undef pg_query_nodes_to_protobuf
#undef pg_query_nodes_to_protobuf_observed
#undef pg_query_nodes_to_protobuf_certified

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while (0)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))

/* Exact pre-change measurement branch, including slot, status and depth work. */
static void reference_message(DirectWire *c,unsigned field,DirectWireVisitor visitor,const void *object) {
 size_t slot,start,length;
 if(c->status!=1)return;
 if(c->depth>=256U){c->status=0;return;}
 c->depth++;
 if(!dw_slot(c,&slot))goto done;
 start=c->position;visitor(c,object);
 if(c->status!=1)goto done;
 length=c->position-start;
 if(length>UINT32_MAX){c->status=-1;goto done;}
 c->sizes[slot]=(uint32_t)length;
 dw_varint(c,((uint64_t)field<<3U)|2U);dw_varint(c,length);
 done:c->depth--;
}
static void payload(DirectWire *c,const void *object) {
 const size_t *length=object;
 CHECK(c->writing==0);
 if(c->status!=1)return;
 if(*length>SIZE_MAX-c->position){c->status=-1;return;}
 c->position+=*length;
}
static void reject_payload(DirectWire *c,const void *object) {
 c->status=*(const int *)object;
}
static void reference_scalar(DirectWire *c,unsigned field,uint64_t value) {
 if(value==0 || c->status!=1)return;
 dw_varint(c,(uint64_t)field<<3U);dw_varint(c,value);
}
static void scalar_boundary_states(void) {
 static const unsigned fields[]={0U,1U,15U,16U,268U,2047U,2048U,UINT_MAX};
 static const uint64_t values[]={0U,1U,126U,127U,128U,129U,16383U,16384U,
  2097151U,2097152U,268435455U,268435456U,UINT32_MAX,
  UINT64_C(34359738367),UINT64_C(34359738368),UINT64_C(4398046511103),
  UINT64_C(4398046511104),UINT64_C(562949953421311),UINT64_C(562949953421312),
  UINT64_C(72057594037927935),UINT64_C(72057594037927936),
  UINT64_C(9223372036854775807),UINT64_C(9223372036854775808),
  (uint64_t)(int64_t)INT32_MIN,UINT64_MAX};
 static const int statuses[]={1,0,-1,-2};
 static const size_t capacities[]={0U,1U,2U,3U,9U,10U,11U,19U,20U,21U,63U};
 static const size_t positions[]={0U,1U,2U,8U,32U,64U};
 size_t checks=0U;
 for(size_t f=0;f<COUNT(fields);f++)for(size_t v=0;v<COUNT(values);v++)
 for(size_t s=0;s<COUNT(statuses);s++)for(size_t edge=0;edge<24U;edge++) {
  DirectWire expected={0},actual={0};
  expected.position=actual.position=edge<3U?edge:SIZE_MAX-(edge-3U);
  expected.status=actual.status=statuses[s];
  reference_scalar(&expected,fields[f],values[v]);
  dw_scalar(&actual,fields[f],values[v]);
  CHECK(actual.position==expected.position && actual.status==expected.status);
  CHECK(actual.writing==0 && actual.output==NULL && actual.count==0U);
  checks++;
 }
 /* The encoding branch remains byte-for-byte equivalent, including the
  * sequential partial-tag behavior when the output reservation is too small. */
 for(size_t f=0;f<COUNT(fields);f++)for(size_t v=0;v<COUNT(values);v++)
 for(size_t s=0;s<COUNT(statuses);s++)for(size_t n=0;n<COUNT(capacities);n++)
 for(size_t p=0;p<COUNT(positions);p++) {
  unsigned char want[80],got[80];DirectWire expected={0},actual={0};
  memset(want,0xa5,sizeof(want));memset(got,0xa5,sizeof(got));
  expected.output=want;actual.output=got;expected.writing=actual.writing=1;
  expected.position=actual.position=positions[p];
  expected.output_size=actual.output_size=capacities[n];
  expected.status=actual.status=statuses[s];
  reference_scalar(&expected,fields[f],values[v]);
  dw_scalar(&actual,fields[f],values[v]);
  CHECK(actual.position==expected.position && actual.status==expected.status);
  CHECK(memcmp(want,got,sizeof(want))==0);checks++;
 }
 printf("direct-wire scalar state/byte differentials: %zu\n",checks);
}
static void boundary_states(void) {
 static const unsigned fields[]={0U,1U,15U,16U,268U,2047U,2048U,UINT_MAX};
 static const size_t lengths[]={0U,1U,126U,127U,128U,129U,16383U,16384U,
  2097151U,2097152U,268435455U,268435456U,UINT32_MAX,SIZE_MAX};
 static const int statuses[]={1,0,-1,-2};
 size_t checks=0U;
 for(size_t f=0;f<COUNT(fields);f++)for(size_t l=0;l<COUNT(lengths);l++)
 for(size_t s=0;s<COUNT(statuses);s++)for(size_t edge=0;edge<24U;edge++) {
  DirectWire expected={0},actual={0};
  expected.position=actual.position=edge<3U?edge:SIZE_MAX-(edge-3U);
  expected.status=actual.status=statuses[s];
  dw_varint(&expected,((uint64_t)fields[f]<<3U)|2U);
  dw_varint(&expected,lengths[l]);
  dw_measure_envelope(&actual,fields[f],lengths[l]);
  CHECK(actual.position==expected.position && actual.status==expected.status);
  CHECK(actual.writing==0 && actual.output==NULL && actual.count==0U);
  checks++;
 }
 for(size_t f=0;f<COUNT(fields);f++)for(size_t l=0;l<COUNT(lengths);l++)
 for(size_t s=0;s<COUNT(statuses);s++)for(size_t edge=0;edge<24U;edge++) {
  uint32_t want[2]={0x12345678U,0xa5a5a5a5U},got[2]={0x12345678U,0xa5a5a5a5U};
  DirectWire expected={0},actual={0};
  expected.sizes=want;actual.sizes=got;expected.capacity=actual.capacity=2U;
  expected.position=actual.position=edge<3U?edge:SIZE_MAX-(edge-3U);
  expected.status=actual.status=statuses[s];expected.depth=actual.depth=17U;
  reference_message(&expected,fields[f],payload,&lengths[l]);
  dw_message(&actual,fields[f],payload,&lengths[l]);
  CHECK(actual.position==expected.position && actual.status==expected.status);
  CHECK(actual.depth==expected.depth && actual.count==expected.count);
  CHECK(actual.index==expected.index && actual.capacity==expected.capacity);
  CHECK(memcmp(want,got,sizeof(want))==0);checks++;
 }
 for(unsigned depth=255U;depth<=256U;depth++)for(size_t s=1;s<COUNT(statuses);s++) {
  uint32_t want[1]={123U},got[1]={123U};DirectWire expected={0},actual={0};
  expected.sizes=want;actual.sizes=got;expected.capacity=actual.capacity=1U;
  expected.status=actual.status=1;expected.depth=actual.depth=depth;
  reference_message(&expected,268U,reject_payload,&statuses[s]);
  dw_message(&actual,268U,reject_payload,&statuses[s]);
  CHECK(actual.position==expected.position && actual.status==expected.status);
  CHECK(actual.depth==expected.depth && actual.count==expected.count);
  CHECK(want[0]==got[0]);checks++;
 }
 printf("direct-wire measurement state differentials: %zu\n",checks);
}
/* Keep the original primitive visitors and dw_message as an independent
 * reference for the AConst-only helpers. The removed child cache slot and
 * index are deliberately not compared: externally observable state, complete
 * output bytes (including unwritten sentinels), and final depth must agree. */
typedef void (*PrimitiveMessage)(DirectWire *,unsigned,const void *);
typedef struct {
 const char *name;
 DirectWireVisitor visitor;
 PrimitiveMessage message;
 const void *object;
} PrimitiveCase;
static void primitive_integer(DirectWire *c,unsigned field,const void *object) {
 const Integer *node=object;
 dw_primitive_int32_message(c,field,(int32_t)node->ival);
}
static void primitive_float(DirectWire *c,unsigned field,const void *object) {
 const Float *node=object;
 dw_primitive_string_message(c,field,node->fval,0);
}
static void primitive_string(DirectWire *c,unsigned field,const void *object) {
 const String *node=object;
 dw_primitive_string_message(c,field,node->sval,(int32_t)node->location);
}
static void primitive_compare(const PrimitiveCase *test,unsigned field,
 int writing,size_t position,size_t output_size,unsigned depth,int status,
 int certifying,uint32_t body_length,unsigned char *want,unsigned char *got,
 size_t buffer_length) {
 uint32_t reference_sizes[3]={0x12345678U,body_length,0xa5a5a5a5U};
 uint32_t actual_sizes[3]={0x12345678U,body_length,0xa5a5a5a5U};
 DirectWire expected={0},actual={0};int same;
 expected.sizes=reference_sizes;actual.sizes=actual_sizes;
 expected.capacity=actual.capacity=COUNT(reference_sizes);
 /* Start after an existing slot, as happens inside a parent message. */
 expected.count=actual.count=writing?2U:1U;
 expected.index=actual.index=writing?1U:0U;
 expected.position=actual.position=position;
 expected.output_size=actual.output_size=output_size;
 expected.depth=actual.depth=depth;expected.status=actual.status=status;
 expected.writing=actual.writing=writing;
 expected.certifying=actual.certifying=certifying;
 if(writing) {
  CHECK(output_size<=buffer_length);
  memset(want,0xa5,buffer_length);memset(got,0xa5,buffer_length);
  expected.output=want;actual.output=got;
 }
 dw_message(&expected,field,test->visitor,test->object);
 test->message(&actual,field,test->object);
 same=actual.status==expected.status && actual.position==expected.position &&
  actual.depth==expected.depth && actual.writing==expected.writing &&
  actual.certifying==expected.certifying && actual.output_size==expected.output_size;
 if(writing)same=same && memcmp(want,got,buffer_length)==0;
 else same=same && actual.output==NULL;
 if(!same)fprintf(stderr,
  "primitive %s field=%u writing=%d start=%zu limit=%zu depth=%u status=%d "
  "certifying=%d body=%u: expected status=%d position=%zu depth=%u; "
  "actual status=%d position=%zu depth=%u\n",
  test->name,field,writing,position,output_size,depth,status,certifying,body_length,
  expected.status,expected.position,expected.depth,actual.status,actual.position,actual.depth);
 CHECK(same);
}
static size_t primitive_case_states(const PrimitiveCase *test) {
 static const unsigned fields[]={1U,2U,4U,16U,UINT_MAX};
 static const unsigned depths[]={0U,255U,256U,UINT_MAX};
 static const int statuses[]={1,0,-1,-2,2,INT_MIN,INT_MAX};
 static const size_t starts[]={0U,1U,7U};
 static const size_t short_lengths[]={0U,1U,2U,3U,4U,5U,9U,10U,11U,127U,128U,16383U,16384U};
 DirectWire body={0};size_t checks=0U,positions[64],position_count=0U;
 size_t buffer_length;unsigned char *want,*got;
 body.status=1;test->visitor(&body,test->object);
 CHECK(body.status==1 && body.position<=UINT32_MAX);
 buffer_length=body.position+64U;
 want=malloc(buffer_length);got=malloc(buffer_length);CHECK(want!=NULL && got!=NULL);
 for(size_t p=0;p<COUNT(starts);p++)positions[position_count++]=starts[p];
 for(size_t edge=0;edge<24U;edge++)positions[position_count++]=SIZE_MAX-edge;
 /* Exercise overflow while emitting the body and immediately around the
  * point at which the body fits but its enclosing tag/length does not. */
 for(size_t edge=0;edge<12U;edge++)positions[position_count++]=SIZE_MAX-body.position-edge;
 for(size_t edge=1;edge<=12U && edge<=body.position;edge++)
  positions[position_count++]=SIZE_MAX-body.position+edge;
 CHECK(position_count<=COUNT(positions));
 for(size_t f=0;f<COUNT(fields);f++) {
  size_t wire_length=body.position+dw_varint_length(((uint64_t)fields[f]<<3U)|2U)+
   dw_varint_length(body.position);
  for(size_t d=0;d<COUNT(depths);d++)for(size_t s=0;s<COUNT(statuses);s++)
  for(size_t p=0;p<position_count;p++) {
   primitive_compare(test,fields[f],0,positions[p],0U,depths[d],statuses[s],
    (int)(p&1U),(uint32_t)body.position,want,got,buffer_length);checks++;
  }
  /* Cut output before and inside both envelope varints, primitive field
   * tags, values and string data. Nonzero starts also check prefix ownership. */
  for(size_t p=0;p<COUNT(starts);p++)for(size_t d=0;d<3U;d++) {
   for(size_t n=0;n<COUNT(short_lengths)+3U;n++) {
    size_t remaining=n<COUNT(short_lengths)?short_lengths[n]:
     wire_length+n-COUNT(short_lengths)-1U;
    if(remaining>wire_length+1U)continue;
    primitive_compare(test,fields[f],1,starts[p],starts[p]+remaining,depths[d],1,
     (int)(n&1U),(uint32_t)body.position,want,got,buffer_length);checks++;
   }
  }
  /* Arbitrary initial statuses must be unchanged even with an exhausted
   * destination or a depth that would otherwise force fallback. */
  for(size_t d=0;d<COUNT(depths);d++)for(size_t s=0;s<COUNT(statuses);s++)
  for(size_t full=0;full<2U;full++) {
   primitive_compare(test,fields[f],1,7U,full?7U+wire_length:0U,depths[d],statuses[s],
    (int)full,(uint32_t)body.position,want,got,buffer_length);checks++;
  }
  /* No large allocation or invalid output pointer is needed to compare
   * arithmetic-overflow versus exhausted-output precedence. */
  for(size_t edge=0;edge<24U;edge++) {
   primitive_compare(test,fields[f],1,SIZE_MAX-edge,buffer_length,255U,1,0,
    (uint32_t)body.position,want,got,buffer_length);checks++;
  }
 }
 free(want);free(got);return checks;
}
static void primitive_message_states(void) {
 static const int32_t integers[]={0,1,127,128,16383,16384,2097151,2097152,
  268435455,268435456,INT32_MAX,-1,-127,-128,INT32_MIN};
 static const int32_t locations[]={0,1,127,128,16383,16384,INT32_MAX,-1,INT32_MIN};
 /* Include body-envelope thresholds as well as string-length thresholds,
  * with both short and ten-byte signed location fields. */
 static const size_t lengths[]={0U,1U,122U,123U,124U,125U,126U,127U,128U,129U,
  16368U,16369U,16370U,16378U,16379U,16380U,16381U,16382U,16383U,16384U};
 size_t checks=0U,cases=0U;char name[96];
 for(size_t i=0;i<COUNT(integers);i++) {
  Integer node={0};PrimitiveCase test={name,dw_Integer,primitive_integer,&node};
  node.type=T_Integer;node.ival=integers[i];
  snprintf(name,sizeof(name),"Integer(%ld)",(long)integers[i]);
  checks+=primitive_case_states(&test);cases++;
 }
 /* A NULL text pointer and a present empty string are distinct input cases. */
 for(size_t i=0;i<=COUNT(lengths);i++) {
  char *text=NULL;size_t length=i<COUNT(lengths)?lengths[i]:0U;
  Float number={0};PrimitiveCase float_test={name,dw_Float,primitive_float,&number};
  if(i<COUNT(lengths)) {
   text=malloc(length+1U);CHECK(text!=NULL);memset(text,'7',length);text[length]='\0';
  }
  number.type=T_Float;number.fval=text;
  snprintf(name,sizeof(name),"Float(%s,%zu)",text?"text":"null",length);
  checks+=primitive_case_states(&float_test);cases++;
  for(size_t l=0;l<COUNT(locations);l++) {
   String node={0};PrimitiveCase string_test={name,dw_String,primitive_string,&node};
   node.type=T_String;node.sval=text;node.location=locations[l];
   snprintf(name,sizeof(name),"String(%s,%zu,location=%ld)",text?"text":"null",length,
    (long)locations[l]);
   checks+=primitive_case_states(&string_test);cases++;
  }
  free(text);
 }
 printf("direct-wire primitive message state/byte differentials: %zu cases, %zu checks\n",cases,checks);
}
/* Exact pre-helper AConst dispatch, so mixed retained/removed child frames
 * have independently constructed size caches during both traversals. */
static void reference_aconst(DirectWire *c,const void *object) {
 const A_Const *node=object;
 if(!node->isnull) {
  switch(nodeTag(&node->val.node)) {
   case T_Integer: dw_message(c,1,dw_Integer,&node->val.ival);break;
   case T_Float: dw_message(c,2,dw_Float,&node->val.fval);break;
   case T_Boolean: dw_message(c,3,dw_Boolean,&node->val.boolval);break;
   case T_String: dw_message(c,4,dw_String,&node->val.sval);break;
   case T_BitString: dw_message(c,5,dw_BitString,&node->val.bsval);break;
   default: c->status=0;return;
  }
 }
 dw_scalar(c,10,(uint64_t)!!node->isnull);
 dw_scalar(c,11,(uint64_t)(int64_t)(int32_t)node->location);
}
typedef struct { A_Const nodes[11]; } PrimitiveSiblings;
static void reference_primitive_siblings(DirectWire *c,const void *object) {
 const PrimitiveSiblings *tree=object;
 for(size_t i=0;i<COUNT(tree->nodes);i++)dw_message(c,2,reference_aconst,&tree->nodes[i]);
}
static void actual_primitive_siblings(DirectWire *c,const void *object) {
 const PrimitiveSiblings *tree=object;
 for(size_t i=0;i<COUNT(tree->nodes);i++)dw_message(c,2,dw_AConst,&tree->nodes[i]);
}
static void primitive_sibling_cache(void) {
 PrimitiveSiblings tree={0};size_t removed=0U;
 for(size_t i=0;i<COUNT(tree.nodes);i++) {
  tree.nodes[i].type=T_A_Const;tree.nodes[i].location=i&1U?-1:(int)(127U+i);
 }
 tree.nodes[0].val.ival.type=T_Integer;tree.nodes[0].val.ival.ival=0;
 tree.nodes[1].val.boolval.type=T_Boolean;tree.nodes[1].val.boolval.boolval=false;
 tree.nodes[2].val.sval.type=T_String;tree.nodes[2].val.sval.sval="abc";
 tree.nodes[2].val.sval.location=128;
 tree.nodes[3].val.bsval.type=T_BitString;tree.nodes[3].val.bsval.bsval="b101";
 tree.nodes[4].val.fval.type=T_Float;tree.nodes[4].val.fval.fval="1.25";
 tree.nodes[5].val.ival.type=T_Integer;tree.nodes[5].val.ival.ival=INT32_MIN;
 tree.nodes[6].val.boolval.type=T_Boolean;tree.nodes[6].val.boolval.boolval=true;
 tree.nodes[7].val.sval.type=T_String;tree.nodes[7].val.sval.sval="";
 tree.nodes[7].val.sval.location=-1;
 tree.nodes[8].val.bsval.type=T_BitString;tree.nodes[8].val.bsval.bsval=NULL;
 tree.nodes[9].val.fval.type=T_Float;tree.nodes[9].val.fval.fval=NULL;
 tree.nodes[10].isnull=true;
 for(size_t i=0;i<COUNT(tree.nodes);i++)if(!tree.nodes[i].isnull) {
  NodeTag tag=nodeTag(&tree.nodes[i].val.node);
  if(tag==T_Integer || tag==T_Float || tag==T_String)removed++;
 }
 for(int certifying=0;certifying<=1;certifying++) {
  DirectWire expected={0},actual={0};size_t buffer_length;
  expected.status=actual.status=1;expected.certifying=actual.certifying=certifying;
  dw_message(&expected,1,reference_primitive_siblings,&tree);
  dw_message(&actual,1,actual_primitive_siblings,&tree);
  CHECK(expected.status==1 && actual.status==1 && expected.position==actual.position);
  CHECK(expected.depth==0U && actual.depth==0U && expected.count==actual.count+removed);
  expected.output_size=expected.position;actual.output_size=actual.position;
  buffer_length=expected.position+16U;
  expected.output=malloc(buffer_length);actual.output=malloc(buffer_length);
  CHECK(expected.output!=NULL && actual.output!=NULL);
  memset(expected.output,0xa5,buffer_length);memset(actual.output,0xa5,buffer_length);
  expected.position=actual.position=0U;expected.writing=actual.writing=1;
  expected.certifying=actual.certifying=0;
  dw_message(&expected,1,reference_primitive_siblings,&tree);
  dw_message(&actual,1,actual_primitive_siblings,&tree);
  CHECK(expected.status==1 && actual.status==1 && expected.position==actual.position);
  CHECK(expected.position==expected.output_size && actual.position==actual.output_size);
  CHECK(expected.depth==0U && actual.depth==0U);
  CHECK(expected.index==expected.count && actual.index==actual.count);
  CHECK(memcmp(expected.output,actual.output,buffer_length)==0);
  free(expected.output);free(actual.output);free(expected.sizes);free(actual.sizes);
 }
 printf("direct-wire mixed primitive sibling cache: %zu nodes, %zu removed child frames\n",
  COUNT(tree.nodes),removed);
}
static void observe(const PgQuery__ParseResult *tree,void *context) {
 size_t *calls=context;CHECK(tree!=NULL);(*calls)++;
}
static void same_native_wire(const char *sql) {
 size_t calls=0U;
 PgQueryProtobufParseResult actual=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
  sql,PG_QUERY_PARSE_DEFAULT,NULL,NULL);
 PgQueryProtobufParseResult reference=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
  sql,PG_QUERY_PARSE_DEFAULT,observe,&calls);
 CHECK(actual.error==NULL && reference.error==NULL && calls==1U);
 CHECK(actual.parse_tree.data && reference.parse_tree.data);
 CHECK(actual.parse_tree.len==reference.parse_tree.len);
 CHECK(memcmp(actual.parse_tree.data,reference.parse_tree.data,actual.parse_tree.len)==0);
 pg_query_exit();
 CHECK(memcmp(actual.parse_tree.data,reference.parse_tree.data,actual.parse_tree.len)==0);
 pg_query_free_protobuf_parse_result(actual);pg_query_free_protobuf_parse_result(reference);
}
static void native_wire_boundaries(void) {
 static const char *const sqls[]={
  "SELECT 0,1,127,128,16383,16384,2097151,2097152,2147483647,-1,-2147483648,NULL,true,false",
  "SELECT 2147483648,-2147483649,9223372036854775808,-9223372036854775809,1e-1000,0.0,-0.0",
  "SELECT 'a',B'101',1.25,ARRAY[1,2],a.b FROM t WHERE a IN(1,2)",
  "INSERT INTO t(a,b) VALUES(0,''),(127,'x'),(128,NULL),(-1,'z')",
  "WITH x AS(SELECT 1) SELECT x.* FROM x; UPDATE t SET a=1; DELETE FROM t WHERE a=1",
  "CREATE TABLE t(a int); ALTER TABLE t ADD COLUMN b text; DROP TABLE t"
 };
 static const size_t lengths[]={0U,1U,126U,127U,128U,129U,16382U,16383U,16384U};
 for(size_t i=0;i<COUNT(sqls);i++)same_native_wire(sqls[i]);
 for(size_t i=0;i<COUNT(lengths);i++) {
  size_t n=lengths[i];char *sql=malloc(n+10U);CHECK(sql!=NULL);
  memcpy(sql,"SELECT '",8U);memset(sql+8U,'x',n);sql[n+8U]='\'';sql[n+9U]='\0';
  same_native_wire(sql);free(sql);
 }
 printf("direct-wire native/reference byte differentials: %zu\n",COUNT(sqls)+COUNT(lengths));
}
/* Fused list-item encoding is compared against the generated Node visitors,
 * not against another fused writer. Cache-frame counts may differ, but each
 * writer must consume precisely its own first-pass frames. */
static void fused_item(DirectWire *c,unsigned field,const void *object) {
 if(!dw_scalar_list_item(c,field,object))dw_message(c,field,dw_Node,object);
}
static void fused_case(const void *object) {
 static const unsigned fields[]={1U,15U,16U,268U,UINT_MAX};
 static const unsigned depths[]={0U,252U,253U,254U,255U,256U};
 static const int statuses[]={1,0,-1,-2};
 for(size_t f=0;f<COUNT(fields);f++)for(size_t d=0;d<COUNT(depths);d++)
 for(size_t st=0;st<COUNT(statuses);st++)for(size_t edge=0;edge<24U;edge++) {
  DirectWire want={0},got={0};
  want.status=got.status=statuses[st];want.depth=got.depth=depths[d];
  want.position=got.position=edge?SIZE_MAX-(edge-1U):0U;
  dw_message(&want,fields[f],dw_Node,object);fused_item(&got,fields[f],object);
  CHECK(want.status==got.status && want.position==got.position && want.depth==got.depth);
  free(want.sizes);free(got.sizes);
 }
 for(size_t f=0;f<COUNT(fields);f++)for(size_t d=0;d<COUNT(depths);d++)
 for(int certify=0;certify<=1;certify++) {
  DirectWire want={0},got={0};size_t size;
  want.status=got.status=1;want.depth=got.depth=depths[d];
  want.certifying=got.certifying=certify;
  dw_message(&want,fields[f],dw_Node,object);fused_item(&got,fields[f],object);
  CHECK(want.status==got.status && want.position==got.position);
  if(want.status!=1){free(want.sizes);free(got.sizes);continue;}
  size=want.position;
  want.output=malloc(size+16U);got.output=malloc(size+16U);CHECK(want.output && got.output);
  for(size_t edge=0;edge<20U;edge++) {
   size_t limit=edge<16U?edge:(edge==16U?size/2U:(edge==17U?size-1U:(edge==18U?size:size+1U)));
   if(limit>size+16U)continue;
   memset(want.output,0xa5,size+16U);memset(got.output,0xa5,size+16U);
   want.position=got.position=want.index=got.index=0U;
   want.output_size=got.output_size=limit;want.status=got.status=1;
   want.writing=got.writing=1;want.certifying=got.certifying=0;
   dw_message(&want,fields[f],dw_Node,object);fused_item(&got,fields[f],object);
   CHECK(want.status==got.status && want.position==got.position && want.depth==got.depth);
   CHECK(memcmp(want.output,got.output,size+16U)==0);
   if(got.status==1)CHECK(want.index==want.count && got.index==got.count);
  }
  free(want.output);free(got.output);free(want.sizes);free(got.sizes);
 }
}
static void reference_scalar_list(DirectWire *c,const void *object) {
 const List *list=object;const ListCell *cell;
 foreach(cell,list)dw_message(c,1U,dw_Node,lfirst(cell));
}
static void fused_mixed_cache(void) {
 A_Const values[4]={{0}};SQLValueFunction function={0};
 List *list=malloc(offsetof(List,initial_elements)+5U*sizeof(ListCell));CHECK(list);
 list->type=T_List;list->length=list->max_length=5;list->elements=list->initial_elements;
 for(size_t i=0;i<4U;i++){values[i].type=T_A_Const;values[i].location=(int)i;list->elements[i].ptr_value=&values[i];}
 values[0].val.ival.type=T_Integer;values[0].val.ival.ival=INT32_MIN;
 values[1].val.boolval.type=T_Boolean;values[1].val.boolval.boolval=true;
 values[2].val.sval.type=T_String;values[2].val.sval.sval="mixed";
 values[3].isnull=true;
 NodeSetTag(&function,T_SQLValueFunction);function.op=SVFOP_CURRENT_TIMESTAMP;function.typmod=-1;
 list->elements[4].ptr_value=&function;
 for(int certify=0;certify<=1;certify++) {
  DirectWire want={0},got={0};
  want.status=got.status=1;want.certifying=got.certifying=certify;
  dw_message(&want,2U,reference_scalar_list,list);dw_message(&got,2U,dw_List,list);
  CHECK(want.status==1 && got.status==1 && want.position==got.position);
  CHECK(want.count==got.count+6U);
  want.output_size=want.position;got.output_size=got.position;
  want.output=malloc(want.output_size);got.output=malloc(got.output_size);CHECK(want.output && got.output);
  want.position=got.position=0U;want.writing=got.writing=1;want.certifying=got.certifying=0;
  dw_message(&want,2U,reference_scalar_list,list);dw_message(&got,2U,dw_List,list);
  CHECK(want.status==1 && got.status==1 && want.position==got.position);
  CHECK(want.index==want.count && got.index==got.count);
  CHECK(memcmp(want.output,got.output,want.output_size)==0);
  free(want.output);free(got.output);free(want.sizes);free(got.sizes);
 }
 free(list);
 puts("direct-wire fused mixed cache frames passed");
}
static void fused_scalar_states(void) {
 static const int32_t values[]={0,1,127,128,16383,16384,INT32_MAX,INT32_MIN,-1};
 static const size_t lengths[]={0U,1U,126U,127U,128U,129U,16383U,16384U};
 A_Const a={0};SQLValueFunction v={0};size_t cases=0U;
 a.type=T_A_Const;a.val.ival.type=T_Integer;
 for(size_t i=0;i<COUNT(values);i++) {
  a.val.ival.ival=values[i];a.location=values[COUNT(values)-1U-i];fused_case(&a);cases++;
 }
 for(size_t i=0;i<COUNT(lengths);i++) {
  char *text=malloc(lengths[i]+1U);CHECK(text);memset(text,'x',lengths[i]);text[lengths[i]]=0;
  memset(&a,0,sizeof(a));a.type=T_A_Const;a.location=i&1U?-1:16384;
  a.val.sval.type=T_String;a.val.sval.sval=text;a.val.sval.location=i&1U?-1:128;
  fused_case(&a);cases++;
  a.val.fval.type=T_Float;a.val.fval.fval=text;fused_case(&a);cases++;free(text);
 }
 memset(&a,0,sizeof(a));a.type=T_A_Const;a.val.sval.type=T_String;
 fused_case(&a);cases++;
 a.isnull=true;fused_case(&a);cases++;
 a.isnull=false;a.val.boolval.type=T_Boolean;a.val.boolval.boolval=true;fused_case(&a);cases++;
 /* SQLValueFunction.type is an Oid; its NodeTag lives in Expr. */
 NodeSetTag(&v,T_SQLValueFunction);
 for(size_t i=0;i<COUNT(values);i++) {
  v.op=SVFOP_CURRENT_TIMESTAMP;v.typmod=values[i];v.location=values[COUNT(values)-1U-i];
  v.type=i&1U?UINT32_MAX:0U;fused_case(&v);cases++;
 }
 printf("direct-wire fused scalar state/byte cases: %zu\n",cases);
}
int main(void) {
 boundary_states();scalar_boundary_states();primitive_message_states();primitive_sibling_cache();
 fused_scalar_states();fused_mixed_cache();native_wire_boundaries();return 0;
}
