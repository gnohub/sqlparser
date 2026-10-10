/* Frozen targetcache32B cell-reader oracle. Uses the unchanged original
 * wi_certified_u32, never the new location-specific helper. */
static int reference_certified_cell(const uint8_t **cursor, const uint8_t *end,
    sqlparser_wire_scalar_cell_t *cell)
{
    const uint8_t *p = *cursor;
    uint32_t length, value;
    unsigned arm;
    int is_function;
    memset(cell, 0, sizeof(*cell));
    if (!wi_certified_envelope(&p, end, 1U) || p == end) return 0;
    /* Both admitted Node arms have two-byte tags with distinct first bytes. */
    is_function = *p == (uint8_t)((((PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION << 3) | 2U) & 0x7fU) | 0x80U);
    if (!wi_certified_envelope(&p, end, 2U) || p == end) return 0;
    if (is_function) {
        p++; /* SQLValueFunction.op, a proved positive enum. */
        if (!wi_certified_u32(&p, end, &value) || value > INT32_MAX ||
            (size_t)(end - p) <= sizeof(wsi_typmod_default)) return 0;
        cell->kind = SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION;
        cell->integer = (int32_t)value;
        cell->length = wsi_function_length(value);
        p += sizeof(wsi_typmod_default); /* Canonical signed typmod=-1. */
    } else {
        arm = *p++;
        if (!wi_certified_u32(&p, end, &length)) return 0;
        if (arm == ((PG_QUERY__A__CONST__VAL_IVAL << 3) | 2U)) {
            cell->kind = SQLPARSER_WIRE_SCALAR_INTEGER;
            if (length != 0U) {
                if (p == end) return 0;
                p++; /* Integer.ival; zero has an empty wrapper. */
                if (!wi_certified_u32(&p, end, &value) || value > INT32_MAX) return 0;
                cell->integer = (int32_t)value;
            }
        } else {
            cell->kind = arm == ((PG_QUERY__A__CONST__VAL_SVAL << 3) | 2U) ?
                SQLPARSER_WIRE_SCALAR_STRING : SQLPARSER_WIRE_SCALAR_FLOAT;
            cell->text = (const char *)p;
            if (length != 0U) {
                if (p == end) return 0;
                p++; /* String.sval or Float.fval, both field one. */
                if (!wi_certified_u32(&p, end, &value) || value > (size_t)(end - p)) return 0;
                cell->text = (const char *)p;
                cell->length = value;
                p += value;
            }
        }
    }
    if (p == end) return 0;
    p++; /* AConst.location or SQLValueFunction.location. */
    if (!wi_certified_u32(&p, end, &value) || value > INT32_MAX) return 0;
    cell->location = (int32_t)value;
    *cursor = p;
    return 1;
}
