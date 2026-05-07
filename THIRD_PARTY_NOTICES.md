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

The vendored source is used by the MSVC Windows build. Linux builds continue to
use the system-provided Jansson package through `pkg-config`.

The original license text is available at:

- `vendor/jansson/LICENSE`
