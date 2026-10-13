/* Regression coverage for proto clone. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_proto_clone.h"
#include "../../src/core/sqlparser_ast_internal.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "proto clone:%d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void same_encoding(const ProtobufCMessage *a, const ProtobufCMessage *b)
{
    size_t n = protobuf_c_message_get_packed_size(a);
    size_t m = protobuf_c_message_get_packed_size(b);
    uint8_t *left = malloc(n ? n : 1), *right = malloc(m ? m : 1);
    CHECK(left && right && n == m);
    CHECK(protobuf_c_message_pack(a, left) == n);
    CHECK(protobuf_c_message_pack(b, right) == n);
    CHECK(memcmp(left, right, n) == 0);
    free(left); free(right);
}

static void generated_node_alternatives(void)
{
    const ProtobufCMessageDescriptor *descriptor = &pg_query__node__descriptor;
    unsigned i;
    for (i = 0; i < descriptor->n_fields; i++) {
        const ProtobufCFieldDescriptor *field = &descriptor->fields[i];
        const ProtobufCMessageDescriptor *child_descriptor = field->descriptor;
        PgQuery__Node source = PG_QUERY__NODE__INIT, *copy = NULL;
        ProtobufCMessage *child = malloc(child_descriptor->sizeof_message), *copied_child;
        sqlparser_error_t error;
        CHECK(child && field->type == PROTOBUF_C_TYPE_MESSAGE);
        protobuf_c_message_init(child_descriptor, child);
        source.node_case = (PgQuery__Node__NodeCase)field->id;
        memcpy((uint8_t *)&source + field->offset, &child, sizeof(child));
        CHECK(sqlparser_clone_proto_node(&source, &copy, &error) == SQLPARSER_STATUS_OK);
        memcpy(&copied_child, (uint8_t *)copy + field->offset, sizeof(copied_child));
        CHECK(copy->node_case == source.node_case && copied_child != child);
        CHECK(copied_child->descriptor == child_descriptor);
        same_encoding(&source.base, &copy->base);
        protobuf_c_message_free_unpacked(child, NULL);
        protobuf_c_message_free_unpacked(&copy->base, NULL);
    }
    {
        PgQuery__Node empty = PG_QUERY__NODE__INIT, *copy = NULL;
        sqlparser_error_t error;
        CHECK(sqlparser_clone_proto_node(&empty, &copy, &error) == SQLPARSER_STATUS_INTERNAL_ERROR);
        CHECK(copy == NULL);
        /* Generic trees permit empty messages, including an empty ParseResult. */
        ProtobufCMessage *message = sqlparser_clone_proto_message(&empty.base);
        CHECK(message);
        protobuf_c_message_free_unpacked(message, NULL);
        empty.node_case = PG_QUERY__NODE__NODE_STRING;
        CHECK(sqlparser_clone_proto_node(&empty, &copy, &error) == SQLPARSER_STATUS_INTERNAL_ERROR);
    }
}

typedef union {
    int32_t i32; int64_t i64; uint32_t u32; uint64_t u64;
    float f32; double f64; protobuf_c_boolean boolean; int enumeration;
} Scalar;
typedef struct {
    ProtobufCMessage base;
    protobuf_c_boolean has_value;
    Scalar value;
    size_t n_values;
    void *values;
} ScalarMessage;

static void scalar_fields(void)
{
    ProtobufCType type;
    for (type = PROTOBUF_C_TYPE_INT32; type <= PROTOBUF_C_TYPE_ENUM; type++) {
        ProtobufCFieldDescriptor fields[2] = {
            {.name="value", .id=1, .label=PROTOBUF_C_LABEL_OPTIONAL,
             .type=type, .quantifier_offset=offsetof(ScalarMessage, has_value),
             .offset=offsetof(ScalarMessage, value)},
            {.name="values", .id=2, .label=PROTOBUF_C_LABEL_REPEATED,
             .type=type, .quantifier_offset=offsetof(ScalarMessage, n_values),
             .offset=offsetof(ScalarMessage, values)}
        };
        ProtobufCMessageDescriptor descriptor = {
            .magic=PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
            .name="CloneScalars", .sizeof_message=sizeof(ScalarMessage),
            .n_fields=2, .fields=fields
        };
        Scalar value = {0};
        ScalarMessage source = {0}, *copy;
        size_t width;
        switch (type) {
            case PROTOBUF_C_TYPE_INT64:
            case PROTOBUF_C_TYPE_SINT64:
            case PROTOBUF_C_TYPE_SFIXED64: value.i64 = -1234567890123LL; width=sizeof(value.i64); break;
            case PROTOBUF_C_TYPE_UINT64:
            case PROTOBUF_C_TYPE_FIXED64: value.u64 = UINT64_C(12345678901234567890); width=sizeof(value.u64); break;
            case PROTOBUF_C_TYPE_UINT32:
            case PROTOBUF_C_TYPE_FIXED32: value.u32 = UINT32_C(3212345678); width=sizeof(value.u32); break;
            case PROTOBUF_C_TYPE_FLOAT: value.f32 = 123.125F; width=sizeof(value.f32); break;
            case PROTOBUF_C_TYPE_DOUBLE: value.f64 = -456.0625; width=sizeof(value.f64); break;
            case PROTOBUF_C_TYPE_BOOL: value.boolean = 1; width=sizeof(value.boolean); break;
            case PROTOBUF_C_TYPE_ENUM: value.enumeration = 7; width=sizeof(value.enumeration); break;
            default: value.i32 = -12345678; width=sizeof(value.i32); break;
        }
        source.base.descriptor=&descriptor; source.has_value=1;
        source.value=value; source.n_values=1; source.values=&value;
        copy=(ScalarMessage *)sqlparser_clone_proto_message(&source.base);
        CHECK(copy && copy->has_value == 1 && copy->n_values == 1);
        CHECK(copy->values != source.values);
        CHECK(!memcmp(&copy->value, &value, width) && !memcmp(copy->values, &value, width));
        same_encoding(&source.base, &copy->base);
        protobuf_c_message_free_unpacked(&copy->base, NULL);
        source.has_value=0;
        copy=(ScalarMessage *)sqlparser_clone_proto_message(&source.base);
        CHECK(copy && !copy->has_value && !memcmp(&copy->value, &value, width));
        same_encoding(&source.base, &copy->base);
        protobuf_c_message_free_unpacked(&copy->base, NULL);
    }
}

typedef struct {
    ProtobufCMessage base;
    char *default_text, *text;
    protobuf_c_boolean has_bytes;
    ProtobufCBinaryData bytes;
    size_t n_texts; char **texts;
    size_t n_blobs; ProtobufCBinaryData *blobs;
    size_t n_children; PgQuery__String **children;
    PgQuery__String *child;
    uint32_t selected;
    union { int32_t integer; char *text; ProtobufCBinaryData bytes; PgQuery__String *child; } choice;
    protobuf_c_boolean has_default_bytes;
    ProtobufCBinaryData default_bytes;
} OwnedMessage;

static const char default_text[]="default";
static uint8_t default_data[]={0x12, 0x34};
static const ProtobufCBinaryData default_bytes={sizeof(default_data), default_data};
#define FIELD(id_, member_, type_, label_, quantifier_, default_) \
    {.name=#member_, .id=id_, .type=type_, .label=label_, \
     .offset=offsetof(OwnedMessage, member_), .quantifier_offset=quantifier_, .default_value=default_}
static const ProtobufCFieldDescriptor owned_fields[]={
    FIELD(1, default_text, PROTOBUF_C_TYPE_STRING, PROTOBUF_C_LABEL_OPTIONAL, 0, default_text),
    FIELD(2, text, PROTOBUF_C_TYPE_STRING, PROTOBUF_C_LABEL_OPTIONAL, 0, NULL),
    FIELD(3, bytes, PROTOBUF_C_TYPE_BYTES, PROTOBUF_C_LABEL_OPTIONAL, offsetof(OwnedMessage, has_bytes), NULL),
    FIELD(4, texts, PROTOBUF_C_TYPE_STRING, PROTOBUF_C_LABEL_REPEATED, offsetof(OwnedMessage, n_texts), NULL),
    FIELD(5, blobs, PROTOBUF_C_TYPE_BYTES, PROTOBUF_C_LABEL_REPEATED, offsetof(OwnedMessage, n_blobs), NULL),
    FIELD(6, children, PROTOBUF_C_TYPE_MESSAGE, PROTOBUF_C_LABEL_REPEATED, offsetof(OwnedMessage, n_children), NULL),
    FIELD(7, child, PROTOBUF_C_TYPE_MESSAGE, PROTOBUF_C_LABEL_OPTIONAL, 0, NULL),
    {.name="choice_integer", .id=8, .type=PROTOBUF_C_TYPE_INT32, .label=PROTOBUF_C_LABEL_OPTIONAL,
     .offset=offsetof(OwnedMessage, choice), .quantifier_offset=offsetof(OwnedMessage, selected), .flags=PROTOBUF_C_FIELD_FLAG_ONEOF},
    {.name="choice_text", .id=9, .type=PROTOBUF_C_TYPE_STRING, .label=PROTOBUF_C_LABEL_OPTIONAL,
     .offset=offsetof(OwnedMessage, choice), .quantifier_offset=offsetof(OwnedMessage, selected), .flags=PROTOBUF_C_FIELD_FLAG_ONEOF},
    {.name="choice_bytes", .id=10, .type=PROTOBUF_C_TYPE_BYTES, .label=PROTOBUF_C_LABEL_OPTIONAL,
     .offset=offsetof(OwnedMessage, choice), .quantifier_offset=offsetof(OwnedMessage, selected), .flags=PROTOBUF_C_FIELD_FLAG_ONEOF},
    {.name="choice_child", .id=11, .type=PROTOBUF_C_TYPE_MESSAGE, .label=PROTOBUF_C_LABEL_OPTIONAL,
     .offset=offsetof(OwnedMessage, choice), .quantifier_offset=offsetof(OwnedMessage, selected), .flags=PROTOBUF_C_FIELD_FLAG_ONEOF},
    FIELD(12, default_bytes, PROTOBUF_C_TYPE_BYTES, PROTOBUF_C_LABEL_OPTIONAL, offsetof(OwnedMessage, has_default_bytes), &default_bytes)
};
static const ProtobufCMessageDescriptor owned_descriptor={
    .magic=PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC, .name="CloneOwnership",
    .sizeof_message=sizeof(OwnedMessage), .n_fields=sizeof(owned_fields)/sizeof(owned_fields[0]), .fields=owned_fields
};

static void owned_fields_and_oneofs(void)
{
    uint8_t data[]={1,2,3}, unknown_data[]={0x81,0x00};
    char text[]="source", *texts[]={text, (char *)protobuf_c_empty_string};
    ProtobufCBinaryData blobs[]={{sizeof(data),data},{0,data}};
    ProtobufCMessageUnknownField unknown={123,PROTOBUF_C_WIRE_TYPE_VARINT,sizeof(unknown_data),unknown_data};
    PgQuery__String child=PG_QUERY__STRING__INIT, *children[]={&child,&child};
    OwnedMessage source={0};
    uint32_t selected;
    child.sval=text;
    source.base.descriptor=&owned_descriptor;
    source.base.n_unknown_fields=1; source.base.unknown_fields=&unknown;
    source.default_text=(char *)default_text; source.text=text;
    source.has_bytes=1; source.bytes=blobs[0];
    source.n_texts=2; source.texts=texts; source.n_blobs=2; source.blobs=blobs;
    source.n_children=2; source.children=children; source.child=&child;
    source.default_bytes=default_bytes;
    for (selected=0; selected<=11; selected++) {
        OwnedMessage *copy, *survivor;
        if (selected && selected<8) continue;
        source.selected=selected;
        memset(&source.choice, 0, sizeof(source.choice));
        if (selected==8) source.choice.integer=-17;
        if (selected==9) source.choice.text=text;
        if (selected==10) source.choice.bytes=blobs[0];
        if (selected==11) source.choice.child=&child;
        copy=(OwnedMessage *)sqlparser_clone_proto_message(&source.base);
        CHECK(copy && copy->selected==selected && copy->default_text==default_text);
        CHECK(copy->default_bytes.data==default_data && !copy->has_default_bytes);
        CHECK(copy->text!=text && !strcmp(copy->text,text));
        CHECK(copy->bytes.data!=data && !memcmp(copy->bytes.data,data,sizeof(data)));
        CHECK(copy->texts!=texts && copy->texts[0]!=text && copy->texts[1]!=protobuf_c_empty_string);
        CHECK(copy->blobs!=blobs && copy->blobs[0].data!=data && copy->blobs[1].data!=data);
        CHECK(copy->children!=children && copy->children[0]!=&child && copy->children[0]!=copy->children[1]);
        CHECK(copy->child!=&child && copy->child!=copy->children[0] && copy->child->sval!=text);
        CHECK(copy->base.unknown_fields!=&unknown && copy->base.unknown_fields[0].data!=unknown_data);
        if (selected==9) CHECK(copy->choice.text!=text);
        if (selected==10) CHECK(copy->choice.bytes.data!=data);
        if (selected==11) CHECK(copy->choice.child!=&child && copy->choice.child!=copy->child);
        same_encoding(&source.base,&copy->base);
        survivor=(OwnedMessage *)sqlparser_clone_proto_message(&copy->base);
        CHECK(survivor);
        protobuf_c_message_free_unpacked(&copy->base,NULL);
        same_encoding(&source.base,&survivor->base);
        survivor->text[0]='x'; survivor->bytes.data[0]=42; survivor->children[0]->sval[0]='y';
        CHECK(text[0]=='s' && data[0]==1 && child.sval[0]=='s');
        protobuf_c_message_free_unpacked(&survivor->base,NULL);
    }
    /* Ordinary malformed inputs exercise cleanup after earlier owned fields. */
    source.texts=NULL;
    CHECK(sqlparser_clone_proto_message(&source.base)==NULL);
    source.texts=texts; source.base.unknown_fields=NULL;
    CHECK(sqlparser_clone_proto_message(&source.base)==NULL);
    source.base.unknown_fields=&unknown; source.bytes.data=NULL;
    CHECK(sqlparser_clone_proto_message(&source.base)==NULL);
    {
        PgQuery__Node node=PG_QUERY__NODE__INIT, *copy=NULL;
        sqlparser_error_t error;
        node.base.n_unknown_fields=1; node.base.unknown_fields=&unknown;
        CHECK(sqlparser_clone_proto_node(&node,&copy,&error)==SQLPARSER_STATUS_OK);
        same_encoding(&node.base,&copy->base);
        protobuf_c_message_free_unpacked(&copy->base,NULL);
    }
}

int main(void)
{
    generated_node_alternatives();
    scalar_fields();
    owned_fields_and_oneofs();
    puts("protobuf clone: all Node alternatives, scalar types, ownership, defaults, oneofs and unknown fields passed");
    return 0;
}
