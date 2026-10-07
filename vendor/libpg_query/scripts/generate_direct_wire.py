from pathlib import Path
import re,json
V=Path(__file__).resolve().parent.parent; E=V/'src'
p=(V/'protobuf/pg_query.proto').read_text(); defs=(V/'src/include/pg_query_outfuncs_defs.c').read_text(); conds=(V/'src/include/pg_query_outfuncs_conds.c').read_text()
messages={}
for m in re.finditer(r'message\s+(\w+)\s*\{',p):
 depth=1;i=m.end()
 while depth:
  depth+=(p[i]=='{')-(p[i]=='}');i+=1
 body=p[m.end():i-1]
 fields={a[2]:(a[1],int(a[3]),a[0]) for a in re.findall(r'\b(?:(repeated|optional|required)\s+)?(\w+)\s+(\w+)\s*=\s*(\d+)',body)}
 messages[m[1]]=fields
funcs={}
for m in re.finditer(r'static void\s+_out(\w+)\(OUT_TYPE\((\w+),\s*(\w+)\) out, const (\w+) \*node\)\s*\{(.*?)\n\}',defs,re.S):
 funcs[m[1]]=(m[2],m[4],m[5])
manual=['Integer','Float','Boolean','String','BitString','List','IntList','OidList','AConst']
assert len(funcs)>240,len(funcs)
# SQLValueFunction has only scalar fields and no dialect validation rules.
# Its existing encoder can therefore participate in the validation certificate.
certified_allowed={'SQLValueFunction','ParseResult','RawStmt','InsertStmt','UpdateStmt','DeleteStmt','SelectStmt','RangeVar','ResTarget','List','Node','ColumnRef','String','Integer','Float','Boolean','BitString','AConst'}
output=[]; unsupported=[]
for fn in [*funcs,*manual,'Node','ParseResult']:
 output.append(f'static void dw_{fn}(DirectWire *c, const void *object);')

def field_code(kind,args,msg):
 if kind=='ENUM': outname=args[1]; member=args[3]
 elif kind.startswith('SPECIFIC_NODE'):outname=args[2];member=args[4]
 else:outname=args[0];member=args[2]
 typ,tag,rep=messages[msg][outname]; value='node->'+member
 if kind in ['INT','LONG','UINT','UINT64','BOOL','ENUM']:
  expr=f'_enumToInt{args[0]}({value})' if kind=='ENUM' else value
  if typ=='bool':cast=f'(uint64_t)!!({expr})'
  elif typ=='uint32':cast=f'(uint64_t)(uint32_t)({expr})'
  elif typ=='uint64':cast=f'(uint64_t)({expr})'
  elif typ=='int64':cast=f'(uint64_t)(int64_t)({expr})'
  else:cast=f'(uint64_t)(int64_t)(int32_t)({expr})'
  line=f'dw_scalar(c,{tag},{cast});'
  if kind=='ENUM' and args[0]=='LimitClauseStyle':line=f'if ({value} != LIMIT_CLAUSE_STYLE_DEFAULT) '+line
  return tag,line
 if kind=='FLOAT':
  assert typ=='double'
  return tag,f'dw_double(c,{tag},{value});'
 if kind=='STRING':return tag,f'dw_string(c,{tag},{value});'
 if kind=='CHAR':return tag,f'if({value}) dw_data_field(c,{tag},&{value},1U);'
 if kind=='LIST':
  assert typ=='Node' and rep=='repeated',(msg,kind,args,typ)
  return tag,f'dw_list(c,{tag},{value});'
 if kind=='NODE_PTR':return tag,f'if({value}) dw_message(c,{tag},dw_Node,{value});'
 if kind=='NODE':return tag,f'dw_message(c,{tag},dw_Node,&{value});'
 if kind=='SPECIFIC_NODE_PTR':return tag,f'if({value}) dw_message(c,{tag},dw_{args[0]},{value});'
 if kind=='SPECIFIC_NODE':return tag,f'dw_message(c,{tag},dw_{args[0]},&{value});'
 if kind=='BITMAPSET':return tag,f'if(!bms_is_empty({value})) c->status=0; /* Unsupported bitmap fallback. */'
 raise ValueError(kind)

for fn,(msg,ctype,body) in funcs.items():
 clean=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
 if fn=='SelectStmt':
  clean=re.sub(r'if \(node->limitClauseStyle != LIMIT_CLAUSE_STYLE_DEFAULT\) \{\s*(WRITE_ENUM_FIELD\(LimitClauseStyle,[^;]+;)\s*\}',r'\1',clean)
 calls=list(re.finditer(r'WRITE_(\w+)_FIELD\(([^;]+)\);',clean));rest=re.sub(r'WRITE_\w+_FIELD\([^;]+\);','',clean).strip()
 lines=[]
 try:
  if rest:raise ValueError('nondeclarative: '+rest[:150])
  for call in calls:
   args=[a.strip() for a in call[2].split(',')]; lines.append(field_code(call[1],args,msg))
 except (KeyError,AssertionError,ValueError) as e:
  unsupported.append((fn,str(e)));lines=[(0,'c->status=0;')]
 guards=[]
 if fn not in certified_allowed:guards=[' if(c->certifying){c->status=0;return;}']
 elif fn=='SelectStmt':guards=[' if(c->certifying && (node->startWithClause!=NULL || node->connectByClause!=NULL || node->connectByNoCycle || node->connectByFirst)){c->status=0;return;}']
 output += [f'static void dw_{fn}(DirectWire *c, const void *object) {{',f' const {ctype} *node=object;',*guards,*[f' {line}' for _,line in sorted(lines)],'}']
for fn,member,kind in [('Integer','ival','int'),('Float','fval','string'),('Boolean','boolval','bool'),('String','sval','string'),('BitString','bsval','string')]:
 line=f'dw_string(c,1,node->{member});' if kind=='string' else f'dw_scalar(c,1,(uint64_t)(int64_t)(int32_t)node->{member});'
 if kind=='bool':line=f'dw_scalar(c,1,(uint64_t)!!node->{member});'
 if fn=='String':line+=' dw_scalar(c,2,(uint64_t)(int64_t)(int32_t)node->location);'
 output += [f'static void dw_{fn}(DirectWire *c,const void *object) {{ const {fn} *node=object; {line} }}']
output += ['static void dw_List(DirectWire *c,const void *object) { dw_list(c,1,object); }','static void dw_IntList(DirectWire *c,const void *object) { (void)object; c->status=0; }','static void dw_OidList(DirectWire *c,const void *object) { (void)object; c->status=0; }']
output += ['''static void dw_AConst(DirectWire *c,const void *object) {
 const A_Const *node=object;
 if(!node->isnull) {
  switch(nodeTag(&node->val.node)) {
   case T_Integer: dw_primitive_int32_message(c,1,(int32_t)node->val.ival.ival); break;
   case T_Float: dw_primitive_string_message(c,2,node->val.fval.fval,0); break;
   case T_Boolean: dw_message(c,3,dw_Boolean,&node->val.boolval); break;
   case T_String: dw_primitive_string_message(c,4,node->val.sval.sval,(int32_t)node->val.sval.location); break;
   case T_BitString: dw_message(c,5,dw_BitString,&node->val.bsval); break;
   default: c->status=0; return;
  }
 }
 dw_scalar(c,10,(uint64_t)!!node->isnull);
 dw_scalar(c,11,(uint64_t)(int64_t)(int32_t)node->location);
}''','static void dw_Node(DirectWire *c,const void *object) {',' if(!object || c->status!=1)return;',' switch(nodeTag(object)) {']
for m in re.finditer(r'case (T_\w+):\s*OUT_NODE\(([^)]+)\);',conds):
 args=[a.strip() for a in m[2].split(',')];fn=args[1];field=args[-1]
 if field=='float_':field='float'
 tag=messages['Node'][field][1]
 output.append(f' case {m[1]}: dw_message(c,{tag},dw_{fn},object); break;')
output += [' default:c->status=0;break;',' }','}',f'''static void dw_ParseResult(DirectWire *c,const void *object) {{
 const List *list=object; const ListCell *cell;
 dw_scalar(c,1,PG_VERSION_NUM);
 if(list && !IsA(list,List)) {{ c->status=0;return; }}
 foreach(cell,list) {{ dw_message(c,2,dw_RawStmt,lfirst(cell)); if(c->status!=1)return; }}
}}''']
(E/'direct_wire_visitors.inc').write_text('/* Generated direct protobuf visitors; do not edit manually. */\n'+'\n'.join(output)+'\n')
(E/'generator-summary.json').write_text(json.dumps({'functions':len(funcs),'unsupported_functions':unsupported,'certified_allowed':sorted(certified_allowed),'certification_denied_visitors':len(set(funcs)-certified_allowed)},indent=2))
print('generated',len(funcs),'functions; explicit fallback',unsupported)
