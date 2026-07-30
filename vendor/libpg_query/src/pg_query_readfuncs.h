#ifndef PG_QUERY_READFUNCS_H
#define PG_QUERY_READFUNCS_H

#include "pg_query.h"

#include "postgres.h"
#include "nodes/pg_list.h"
#include "postgres_deparse.h"

List * pg_query_protobuf_to_nodes(PgQueryProtobuf protobuf);
List * pg_query_protobuf_to_nodes_opts(
	PgQueryProtobuf protobuf,
	const PostgresDeparseOpts *opts);
void pg_query_readfuncs_reset_options(void);

#endif
