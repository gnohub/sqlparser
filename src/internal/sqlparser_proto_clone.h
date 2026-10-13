#ifndef SQLPARSER_PROTO_CLONE_H
#define SQLPARSER_PROTO_CLONE_H

#include "protobuf-c/protobuf-c.h"

/* Clone a valid protobuf-C tree with independent heap-owned members. Immutable
 * descriptor defaults retain their sentinel identity. NULL means allocation
 * failure or malformed input; all partial allocations are released. The caller
 * owns the result and releases it with protobuf_c_message_free_unpacked(...,
 * NULL). The source and its allocator are never modified. */
ProtobufCMessage *sqlparser_clone_proto_message(const ProtobufCMessage *source);

#endif
