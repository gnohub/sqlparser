#!/usr/bin/env python3
"""Reapply exact-width semantic-value copies after extracting Bison gram.c.

The reduction push belongs to the Bison skeleton rather than gram.y. Reject
unknown or partially modified skeletons instead of silently losing the copy.
"""
from pathlib import Path
import sys

OLD = "  *++yyvsp = yyval;"
NEW = """  /* Preserve every semantic-value byte while copying in core-sized pieces.
   * Most actions update only the core member; matching that load width avoids
   * a partial-store/full-vector-load dependency. The hierarchy tail, padding,
   * union layout and all grammar productions remain unchanged. */
  ++yyvsp;
  for (size_t yycopy_offset = 0; yycopy_offset < sizeof(yyval);) {
    size_t yycopy_size = sizeof(yyval) - yycopy_offset;
    if (yycopy_size > sizeof(core_YYSTYPE)) yycopy_size = sizeof(core_YYSTYPE);
    memcpy((char *)yyvsp + yycopy_offset,
           (const char *)&yyval + yycopy_offset, yycopy_size);
    yycopy_offset += yycopy_size;
  }"""


def rewrite(source):
    if source.count(NEW) == 1 and OLD not in source:
        return source
    if source.count(OLD) != 1 or NEW in source:
        raise ValueError("Expected exactly one unmodified Bison reduction push")
    return source.replace(OLD, NEW, 1)


if __name__ == "__main__":
    path = Path(sys.argv[1])
    original = path.read_text()
    updated = rewrite(original)
    if updated != original:
        path.write_text(updated)
