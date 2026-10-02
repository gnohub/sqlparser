# Third-Party Notices

This project contains or depends on the following third-party components.

## 1. libpg_query

- Location: `vendor/libpg_query/`
- Purpose: SQL parsing, scanning, summary extraction, and deparse
- License: BSD 3-Clause

The original license text is available at:

- `vendor/libpg_query/LICENSE`

## 2. jansson

- Location: `vendor/jansson/`
- Purpose: JSON encoding and decoding
- License: MIT

The vendored source is used by both Linux and MSVC Windows builds. No
system-provided Jansson package is required.

The original license text is available at:

- `vendor/jansson/LICENSE`

## 3. Bundled protobuf-c specialization

`vendor/libpg_query/vendor/protobuf-c/protobuf-c.c` contains local performance
specializations for exact generated PgQuery descriptors. Node, scalar literal,
and selected wrapper encoding use direct field handling; small known wire
shapes use a bounded unpack shortcut. Other descriptors and unrecognized wire
shapes retain generic handling, including unknown fields and duplicate fields.
`tests/unit/test_protobuf_node.c` and `tests/unit/test_protobuf_fastpath.c`
check descriptor invariants and compare encoding, decoding, allocation failures,
and cleanup against the generic runtime. The original protobuf-c copyright
and BSD license notices are retained in the vendored source.

The vendored C serializer also provides a private, per-call observer for a
borrowed protobuf-c tree before its parser context is released. The C++
serializer retains the serialized-tree fallback. No observer may retain tree
pointers or change ownership.
