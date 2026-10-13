/* Regression coverage for node tail walk. */
#include "sqlparser_ast_internal.h"
static sqlparser_status_t m22_checked_names(ProtobufCMessage *,
    const sqlparser_name_context_t *, sqlparser_name_search_t *);
#define main m18_schema_public_main
#define sqlparser_walk_message_names m22_checked_names
#include "test_schema_name_policy.c"
#undef sqlparser_walk_message_names
#undef main

static int m18_message_search_accept(
	sqlparser_message_search_t *search,
	ProtobufCMessage *message)
{
	if (search == NULL || message == NULL) {
		return 0;
	}
	if (message->descriptor != search->descriptor) {
		return 0;
	}
	if (search->accept != NULL && !search->accept(message)) {
		return 0;
	}

	return 1;
}

static sqlparser_status_t m18_record_name_atom(
	ProtobufCMessage *message,
	ProtobufCMessage *location_owner,
	const sqlparser_name_context_t *context,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	char **slot,
	sqlparser_name_search_t *search)
{
	const char *owner_type;
	const char *field_name;

	if (slot == NULL || search == NULL || *slot == NULL || (*slot)[0] == '\0') {
		return SQLPARSER_STATUS_OK;
	}

	owner_type = NULL;
	field_name = NULL;
	if (golden_message_is_name_container(descriptor) && context != NULL) {
		owner_type = context->owner_type;
		field_name = context->field_name;
	}
	if (owner_type == NULL && descriptor != NULL) {
		owner_type = descriptor->short_name;
	}
	if (field_name == NULL && field != NULL) {
		field_name = field->name;
	}

	if (owner_type == NULL || field_name == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!golden_name_atom_is_identifier(
		    message,
		    context,
		    descriptor,
		    field)) {
		return SQLPARSER_STATUS_OK;
	}

	if (search->match_slot != NULL) {
		if (slot == search->match_slot) {
			if (search->name_view != NULL) {
				search->name_view->owner_type = owner_type;
				search->name_view->field_name = field_name;
				search->name_view->value = *slot;
			}
			search->target_slot = slot;
			search->target_owner = location_owner;
			search->target_field_owner =
				context != NULL ? context->field_owner : NULL;
			search->target_index = search->seen;
		}
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (!search->want_target) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (search->seen != search->target_index) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (search->name_view != NULL) {
		search->name_view->owner_type = owner_type;
		search->name_view->field_name = field_name;
		search->name_view->value = *slot;
	}
	search->target_slot = slot;
	search->target_owner = location_owner;
	search->target_field_owner =
		context != NULL ? context->field_owner : NULL;
	search->seen++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t m18_walk_message_tree(
	ProtobufCMessage *message,
	sqlparser_message_search_t *search,
	sqlparser_error_t *out_error)
{
	const ProtobufCMessageDescriptor *descriptor;
	const ProtobufCFieldDescriptor *fields;
	unsigned field_count;
	const uint8_t *base;
	unsigned index;

	if (message == NULL || search == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->want_target && search->target_message != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	if (m18_message_search_accept(search, message)) {
		if (!search->want_target) {
			search->seen++;
		} else if (search->seen == search->target_index) {
			search->target_message = message;
			search->seen++;
			return SQLPARSER_STATUS_OK;
		} else {
			search->seen++;
		}
	}

	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	base = (const uint8_t *)message;
	fields = descriptor->fields;
	field_count = descriptor->n_fields; /* frozen whole-field oracle */
	for (index = 0U; index < field_count; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &fields[index];
		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}

		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			size_t item_count;
			ProtobufCMessage *const *items;
			size_t item_index;

			item_count = *(const size_t *)(base + field->quantifier_offset);
			items = *(ProtobufCMessage *const * const *)(base + field->offset);
			for (item_index = 0U; item_index < item_count; item_index++) {
				sqlparser_status_t status;

				status = m18_walk_message_tree(items[item_index], search, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (search->want_target && search->target_message != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
			continue;
		}

		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
			const int case_value = *(const int *)(base + field->quantifier_offset);

			if (case_value != (int)field->id) {
				continue;
			}
		}

		{
			ProtobufCMessage *child;
			sqlparser_status_t status;

			child = *(ProtobufCMessage **)(base + field->offset);
			if (child == NULL) {
				continue;
			}

			status = m18_walk_message_tree(child, search, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (search->want_target && search->target_message != NULL) {
				return SQLPARSER_STATUS_OK;
			}
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t m18_walk_message_names(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	sqlparser_name_search_t *search)
{
	const ProtobufCMessageDescriptor *descriptor;
	const ProtobufCFieldDescriptor *fields;
	unsigned field_count;
	uint8_t *base;
	ProtobufCMessage *location_owner;
	unsigned index;

	if (message == NULL || search == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->want_target && search->target_slot != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	base = (uint8_t *)message;
	/* Read-only public name views do not consume source-location ownership.
	 * Keep tracking as the default for every mutation/provenance visitor. */
	location_owner = NULL;
	if (!search->omit_location_owner) {
		location_owner =
			reference_location(message) != NULL ?
				message :
				(context != NULL ? context->location_owner : NULL);
	}
	fields = descriptor->fields;
	field_count = descriptor->n_fields; /* frozen whole-field oracle */
	for (index = 0U; index < field_count; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &fields[index];
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
			const int case_value = *(const int *)(base + field->quantifier_offset);

			if (case_value != (int)field->id) {
				continue;
			}
		}

		if (field->type == PROTOBUF_C_TYPE_STRING) {
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t item_count;
				char **items;
				size_t item_index;
				sqlparser_status_t status;

				item_count = *(const size_t *)(base + field->quantifier_offset);
				items = *(char ***)(base + field->offset);
				for (item_index = 0U; item_index < item_count; item_index++) {
					status = m18_record_name_atom(
						message,
						location_owner,
						context,
						descriptor,
						field,
						&items[item_index],
						search);
					if (status != SQLPARSER_STATUS_OK) {
						return status;
					}
					if (search->want_target && search->target_slot != NULL) {
						return SQLPARSER_STATUS_OK;
					}
				}
			} else {
				sqlparser_status_t status;

				status = m18_record_name_atom(
					message,
					location_owner,
					context,
					descriptor,
					field,
					(char **)(base + field->offset),
					search);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (search->want_target && search->target_slot != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
			continue;
		}

		if (field->type == PROTOBUF_C_TYPE_MESSAGE) {
			sqlparser_name_context_t next_context;

			next_context =
				golden_next_name_context(
					message,
					descriptor,
					field,
					context);
			next_context.location_owner = location_owner;
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t item_count;
				ProtobufCMessage **items;
				size_t item_index;

				item_count = *(const size_t *)(base + field->quantifier_offset);
				items = *(ProtobufCMessage ***)(base + field->offset);
				for (item_index = 0U; item_index < item_count; item_index++) {
					sqlparser_status_t status;

					status = m18_walk_message_names(
						items[item_index],
						&next_context,
						search);
					if (status != SQLPARSER_STATUS_OK) {
						return status;
					}
					if (search->want_target && search->target_slot != NULL) {
						return SQLPARSER_STATUS_OK;
					}
				}
			} else {
				ProtobufCMessage *child;
				sqlparser_status_t status;

				child = *(ProtobufCMessage **)(base + field->offset);
				if (child == NULL) {
					continue;
				}

				status = m18_walk_message_names(child, &next_context, search);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (search->want_target && search->target_slot != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
		}
	}

	return SQLPARSER_STATUS_OK;
}

static size_t tail_name_pairs, tail_tree_pairs, tail_fixtures, callback_pairs;
static size_t tail_alternatives, tail_dynamic, tail_custom, tail_depths;

static void equal_name_search(const sqlparser_name_search_t *a,
    const sqlparser_name_search_t *b)
{
    CHECK(a->seen == b->seen && a->target_index == b->target_index);
    CHECK(a->want_target == b->want_target && a->omit_location_owner == b->omit_location_owner);
    CHECK(a->match_slot == b->match_slot && a->target_slot == b->target_slot);
    CHECK(a->target_owner == b->target_owner && a->target_field_owner == b->target_field_owner);
    if (a->name_view != NULL) {
        CHECK(b->name_view != NULL);
        CHECK(a->name_view->owner_type == b->name_view->owner_type);
        CHECK(a->name_view->field_name == b->name_view->field_name);
        CHECK(a->name_view->value == b->name_view->value);
    }
}

/* Every direct name search in the reused schema/public harness also takes the
 * recursive oracle path, including counts, match-slot and early targets. */
static sqlparser_status_t m22_checked_names(ProtobufCMessage *root,
    const sqlparser_name_context_t *context, sqlparser_name_search_t *search)
{
    sqlparser_name_search_t oracle;
    sqlparser_name_view_t view;
    sqlparser_status_t a, b;
    if (search == NULL) {
        a = m18_walk_message_names(root, context, NULL);
        b = sqlparser_walk_message_names(root, context, NULL);
    } else {
        oracle = *search;
        if (search->name_view != NULL) { view = *search->name_view; oracle.name_view = &view; }
        a = m18_walk_message_names(root, context, &oracle);
        b = sqlparser_walk_message_names(root, context, search);
        equal_name_search(&oracle, search);
    }
    CHECK(a == b); tail_name_pairs++; return b;
}

static ProtobufCMessage *callback_log[4096];
static size_t callback_count;
static int callback_filter;
static int plain_accept(ProtobufCMessage *message)
{
    CHECK(callback_count < COUNT(callback_log));
    callback_log[callback_count++] = message;
    return !callback_filter || (callback_count % 2U) != 0U;
}

static void compare_tree(ProtobufCMessage *root, sqlparser_message_search_t seed)
{
    sqlparser_message_search_t a = seed, b = seed;
    sqlparser_error_t ea, eb;
    ProtobufCMessage *log[4096];
    size_t n, i;
    sqlparser_status_t sa, sb;
    memset(&ea, 0x5a, sizeof(ea)); eb = ea;
    callback_count = 0U;
    sa = m18_walk_message_tree(root, &a, &ea);
    n = callback_count; memcpy(log, callback_log, n * sizeof(*log));
    callback_count = 0U;
    sb = sqlparser_walk_message_tree(root, &b, &eb);
    CHECK(sa == sb && a.seen == b.seen && a.target_message == b.target_message);
    CHECK(a.target_index == b.target_index && a.want_target == b.want_target);
    CHECK(memcmp(&ea, &eb, sizeof(ea)) == 0 && callback_count == n);
    for (i = 0U; i < n; i++) CHECK(log[i] == callback_log[i]);
    tail_tree_pairs++;
}

static void compare_tail_fixture(ProtobufCMessage *root,
    const sqlparser_name_context_t *context)
{
    sqlparser_name_context_t saved = {0};
    size_t i, total;
    if (context != NULL) saved = *context;
    memset(&reference, 0, sizeof(reference)); collect_full(root, context);
    for (i = 0; i < reference.n_messages; i++) {
        const ProtobufCMessageDescriptor *d = reference.messages[i]->descriptor;
        size_t j;
        for (j = 0; j < i; j++) if (reference.messages[j]->descriptor == d) break;
        if (j != i) continue;
        for (callback_filter = 0; callback_filter <= 1; callback_filter++) {
            sqlparser_message_search_t seed = {0};
            seed.descriptor = d; seed.accept = plain_accept;
            compare_tree(root, seed);
            callback_count = 0; CHECK(m18_walk_message_tree(root, &seed, NULL) == SQLPARSER_STATUS_OK);
            total = seed.seen;
            for (j = 0; j <= total; j++) {
                seed.seen = 0; seed.want_target = 1; seed.target_index = j; seed.target_message = NULL;
                compare_tree(root, seed);
            }
            seed.seen = 0; seed.target_index = SIZE_MAX; seed.target_message = NULL;
            compare_tree(root, seed);
            seed.target_message = root; compare_tree(root, seed);
        }
        { sqlparser_message_search_t seed = {0}; seed.descriptor = d; compare_tree(root, seed); }
    }
    for (int omit = 0; omit <= 1; omit++) {
        sqlparser_name_search_t names = {0};
        names.omit_location_owner = omit;
        CHECK(m22_checked_names(root, context, &names) == SQLPARSER_STATUS_OK);
        total = names.seen;
        for (i = 0; i <= total; i++) {
            sqlparser_name_view_t view = {0};
            memset(&names, 0, sizeof(names)); names.omit_location_owner = omit;
            names.want_target = 1; names.target_index = i; names.name_view = &view;
            CHECK(m22_checked_names(root, context, &names) == SQLPARSER_STATUS_OK);
            if (names.target_slot != NULL) {
                char **slot = names.target_slot;
                memset(&names, 0, sizeof(names)); names.omit_location_owner = omit;
                names.match_slot = slot; names.name_view = &view;
                CHECK(m22_checked_names(root, context, &names) == SQLPARSER_STATUS_OK);
                names.want_target = 1;
                CHECK(m22_checked_names(root, context, &names) == SQLPARSER_STATUS_OK);
            }
        }
        /* Empty view output and untouched pre-existing target are contracts. */
        memset(&names, 0, sizeof(names)); names.omit_location_owner = omit;
        names.want_target = 1; names.target_index = SIZE_MAX;
        CHECK(m22_checked_names(root, context, &names) == SQLPARSER_STATUS_OK);
    }
    if (context != NULL) {
        CHECK(context->owner_type == saved.owner_type && context->field_name == saved.field_name);
        CHECK(context->field_owner == saved.field_owner && context->location_owner == saved.location_owner);
        CHECK(context->identifier_forbidden == saved.identifier_forbidden);
        CHECK(context->schema_identifier_policy == saved.schema_identifier_policy);
    }
    tail_fixtures++;
}

static void tail_schema_cases(void)
{
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    PgQuery__ColumnRef owner = PG_QUERY__COLUMN_REF__INIT;
    const ProtobufCFieldDescriptor *field = protobuf_c_message_descriptor_get_field_by_name(owner.base.descriptor, "fields");
    sqlparser_name_context_t context = sqlparser_next_name_context(&owner.base, owner.base.descriptor, field, NULL);
    unsigned i;
    CHECK(d->n_fields == 268U && field != NULL);
    for (i = 0; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        pool_t pool = {0}; PgQuery__Node node = PG_QUERY__NODE__INIT;
        ProtobufCMessage *child = fixture_message(&pool, f->descriptor, 1U);
        case_name = f->name;
        /* Use this field's actual protobuf id and offset, never union offset 0. */
        node.node_case = (PgQuery__Node__NodeCase)f->id;
        memcpy((char *)&node + f->offset, &child, sizeof(child));
        compare_tail_fixture(&node.base, NULL);
        context.location_owner = NULL; compare_tail_fixture(&node.base, &context);
        context.location_owner = &owner.base; compare_tail_fixture(&node.base, &context);
        context.identifier_forbidden = 1; compare_tail_fixture(&node.base, &context);
        context.identifier_forbidden = 0;
        child = NULL; memcpy((char *)&node + f->offset, &child, sizeof(child));
        compare_tail_fixture(&node.base, &context);
        while (pool.count) free(pool.items[--pool.count]);
        tail_alternatives++;
    }
    {
        const int values[] = {0, -1, INT_MIN, INT_MAX, 269, 99999};
        PgQuery__Node node = PG_QUERY__NODE__INIT;
        PgQuery__String string = PG_QUERY__STRING__INIT;
        string.sval = (char *)"unvisited";
        case_name = "unknown active protobuf IDs";
        for (i = 0; i < COUNT(values); i++) {
            CHECK(protobuf_c_message_descriptor_get_field(d, (uint32_t)values[i]) == NULL);
            node.node_case = (PgQuery__Node__NodeCase)values[i]; node.string = &string;
            compare_tail_fixture(&node.base, NULL); compare_tail_fixture(&node.base, &context);
        }
        for (i = 1; i < d->fields[d->n_fields - 1].id; i++)
            if (protobuf_c_message_descriptor_get_field(d, i) == NULL) {
                node.node_case = (PgQuery__Node__NodeCase)i;
                compare_tail_fixture(&node.base, &context);
            }
    }
}

/* Real, initialized storage for both canonical root state and a custom state.
 * The custom descriptor has sparse protobuf ids, distinct valid offsets and
 * two message fields. Name "Node" deliberately does not confer identity. */
typedef struct {
    PgQuery__Node node;
    ProtobufCMessage *first;
    ProtobufCMessage *second;
} changing_root_t;
static const ProtobufCMessageDescriptor custom_node_descriptor;
static void custom_node_init(ProtobufCMessage *message)
{
    memset(message, 0, sizeof(changing_root_t)); message->descriptor = &custom_node_descriptor;
}
static const ProtobufCFieldDescriptor custom_node_fields[] = {
    {"first", 701, PROTOBUF_C_LABEL_OPTIONAL, PROTOBUF_C_TYPE_MESSAGE, 0,
     offsetof(changing_root_t, first), &pg_query__node__descriptor, NULL, 0, 0, NULL, NULL},
    {"second", 907, PROTOBUF_C_LABEL_OPTIONAL, PROTOBUF_C_TYPE_MESSAGE, 0,
     offsetof(changing_root_t, second), &pg_query__node__descriptor, NULL, 0, 0, NULL, NULL}
};
static const unsigned custom_name_order[] = {0, 1};
static const ProtobufCIntRange custom_ranges[] = {{701, 0}, {907, 1}, {0, 2}};
static const ProtobufCMessageDescriptor custom_node_descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC, "test.Node", "Node", "TestNode", "test",
    sizeof(changing_root_t), 2, custom_node_fields, custom_name_order, 2, custom_ranges,
    custom_node_init, NULL, NULL, NULL
};

typedef struct {
    changing_root_t root;
    PgQuery__Node old_node, first_node, last_node;
    PgQuery__List old_list, new_list;
    PgQuery__Node *old_items[1], *new_items[3];
    PgQuery__String old_atom, new_atom, last_atom;
    ProtobufCMessageDescriptor copy;
} mutation_fixture_t;
static mutation_fixture_t mutation;
static int mutation_mode, reject_root;

static void init_mutation(void)
{
    memset(&mutation, 0, sizeof(mutation));
    pg_query__node__init(&mutation.root.node); pg_query__node__init(&mutation.old_node);
    pg_query__node__init(&mutation.first_node); pg_query__node__init(&mutation.last_node);
    pg_query__list__init(&mutation.old_list); pg_query__list__init(&mutation.new_list);
    pg_query__string__init(&mutation.old_atom); pg_query__string__init(&mutation.new_atom);
    pg_query__string__init(&mutation.last_atom);
    mutation.old_atom.sval = (char *)"old"; mutation.new_atom.sval = (char *)"new"; mutation.last_atom.sval = (char *)"last";
    mutation.old_node.node_case = PG_QUERY__NODE__NODE_STRING; mutation.old_node.string = &mutation.old_atom;
    mutation.first_node.node_case = PG_QUERY__NODE__NODE_STRING; mutation.first_node.string = &mutation.new_atom;
    mutation.last_node.node_case = PG_QUERY__NODE__NODE_STRING; mutation.last_node.string = &mutation.last_atom;
    mutation.old_items[0] = &mutation.old_node;
    mutation.old_list.n_items = 1; mutation.old_list.items = mutation.old_items;
    mutation.new_items[0] = &mutation.first_node; mutation.new_items[1] = NULL; mutation.new_items[2] = &mutation.last_node;
    mutation.new_list.n_items = 3; mutation.new_list.items = mutation.new_items;
    mutation.root.node.node_case = PG_QUERY__NODE__NODE_STRING; mutation.root.node.string = &mutation.old_atom;
    mutation.root.first = &mutation.first_node.base; mutation.root.second = &mutation.last_node.base;
    mutation.copy = pg_query__node__descriptor;
    if (mutation_mode == 1 || mutation_mode == 2) {
        mutation.root.node.node_case = PG_QUERY__NODE__NODE_LIST; mutation.root.node.list = &mutation.old_list;
    }
    if (mutation_mode == 6) mutation.root.node.base.descriptor = &mutation.copy;
}
static int mutating_accept(ProtobufCMessage *message)
{
    CHECK(callback_count < COUNT(callback_log)); callback_log[callback_count++] = message;
    if (message == &mutation.root.node.base) {
        switch (mutation_mode) {
        case 0: /* Active case and child both change. */
        case 1: /* Child changes with the same active case. */
        case 6: /* Copied descriptor becomes canonical. */
            mutation.root.node.base.descriptor = &pg_query__node__descriptor;
            mutation.root.node.node_case = PG_QUERY__NODE__NODE_LIST;
            mutation.root.node.list = &mutation.new_list; break;
        case 2: /* Canonical becomes an equal copied descriptor. */
            mutation.root.node.base.descriptor = &mutation.copy;
            mutation.root.node.list = &mutation.new_list; break;
        case 3: mutation.root.node.base.descriptor = &custom_node_descriptor; break;
        case 4: mutation.root.node.base.descriptor = NULL; break;
        case 5: mutation.root.node.node_case = PG_QUERY__NODE__NODE__NOT_SET; break;
        default: CHECK(0);
        }
        if (reject_root) return 0;
    }
    return 1;
}
static void callback_mutations(void)
{
    case_name = "accept callback refresh and early stop";
    for (mutation_mode = 0; mutation_mode < 7; mutation_mode++)
        for (reject_root = 0; reject_root <= 1; reject_root++)
            for (int target = -1; target <= 4; target++) {
                sqlparser_message_search_t a = {0}, b;
                ProtobufCMessage *log[16], *result;
                const ProtobufCMessageDescriptor *result_descriptor;
                size_t n, i, seen;
                int result_case;
                init_mutation(); a.descriptor = mutation.root.node.base.descriptor;
                a.accept = mutating_accept; a.want_target = target >= 0;
                a.target_index = target >= 0 ? (size_t)target : 0;
                if (target == 4) a.target_message = &mutation.last_node.base;
                b = a; callback_count = 0;
                CHECK(m18_walk_message_tree(&mutation.root.node.base, &a, NULL) == SQLPARSER_STATUS_OK);
                n = callback_count; CHECK(n < COUNT(log)); memcpy(log, callback_log, n * sizeof(*log));
                result = a.target_message; seen = a.seen;
                result_descriptor = mutation.root.node.base.descriptor; result_case = mutation.root.node.node_case;
                init_mutation(); callback_count = 0;
                CHECK(sqlparser_walk_message_tree(&mutation.root.node.base, &b, NULL) == SQLPARSER_STATUS_OK);
                CHECK(b.seen == seen && b.target_message == result && callback_count == n);
                CHECK(mutation.root.node.base.descriptor == result_descriptor);
                CHECK((int)mutation.root.node.node_case == result_case);
                for (i = 0; i < n; i++) CHECK(log[i] == callback_log[i]);
                if (target == 0 && !reject_root) CHECK(n == 1 && seen == 1 && result == &mutation.root.node.base);
                if (target == 4) CHECK(n == 0 && seen == 0 && result == &mutation.last_node.base);
                if (target == -1 && mutation_mode <= 3) {
                    CHECK(n == 3 && log[1] == &mutation.first_node.base && log[2] == &mutation.last_node.base);
                    CHECK(seen == (reject_root ? 2U : 3U));
                }
                callback_pairs++;
            }
}

static void alias_depth_and_custom_cases(void)
{
    PgQuery__Node nodes[66]; PgQuery__AIndirection groups[65];
    PgQuery__String string = PG_QUERY__STRING__INIT;
    PgQuery__ColumnRef owner = PG_QUERY__COLUMN_REF__INIT;
    sqlparser_name_context_t context = TEST_CONTEXT("ColumnRef", "fields", &owner.base, &owner.base, 0);
    unsigned i;
    string.sval = (char *)"borrowed_depth_identifier";
    for (unsigned depth = 64; depth <= 65; depth++) {
        for (i = 0; i < depth; i++) {
            pg_query__node__init(&nodes[i]); pg_query__a__indirection__init(&groups[i]);
            nodes[i].node_case = PG_QUERY__NODE__NODE_A_INDIRECTION; nodes[i].a_indirection = &groups[i];
            groups[i].arg = &nodes[i + 1];
        }
        pg_query__node__init(&nodes[depth]); nodes[depth].node_case = PG_QUERY__NODE__NODE_STRING;
        nodes[depth].string = &string;
        case_name = depth == 64 ? "64 grouping wrappers" : "65 grouping wrappers";
        compare_tail_fixture(&nodes[0].base, NULL); compare_tail_fixture(&nodes[0].base, &context);
        CHECK(string.sval == (char *)"borrowed_depth_identifier"); tail_depths++;
    }
    {
        PgQuery__List list = PG_QUERY__LIST__INIT; PgQuery__Node root = PG_QUERY__NODE__INIT;
        PgQuery__Node *items[] = {&nodes[0], NULL, &nodes[0]};
        sqlparser_name_search_t search = {0};
        case_name = "repeated alias slots";
        list.n_items = COUNT(items); list.items = items;
        root.node_case = PG_QUERY__NODE__NODE_LIST; root.list = &list;
        compare_tail_fixture(&root.base, &context);
        search.match_slot = &string.sval;
        CHECK(m22_checked_names(&root.base, &context, &search) == SQLPARSER_STATUS_OK);
        CHECK(search.seen == 2 && search.target_index == 1 && search.target_slot == &string.sval);
    }
    {
        PgQuery__Node node = PG_QUERY__NODE__INIT;
        ProtobufCMessageDescriptor copy = pg_query__node__descriptor;
        changing_root_t custom;
        case_name = "copied and sparse custom descriptors";
        node.node_case = PG_QUERY__NODE__NODE_STRING; node.string = &string; node.base.descriptor = &copy;
        compare_tail_fixture(&node.base, NULL); compare_tail_fixture(&node.base, &context); tail_custom++;
        custom_node_init((ProtobufCMessage *)&custom); custom.first = &node.base; custom.second = &nodes[0].base;
        CHECK(protobuf_c_message_descriptor_get_field(&custom_node_descriptor, 701) == &custom_node_fields[0]);
        CHECK(protobuf_c_message_descriptor_get_field(&custom_node_descriptor, 907) == &custom_node_fields[1]);
        compare_tail_fixture((ProtobufCMessage *)&custom, NULL); compare_tail_fixture((ProtobufCMessage *)&custom, &context); tail_custom++;
    }
    {
        ProtobufCMessage empty = {0}; sqlparser_message_search_t search = {0};
        case_name = "NULL roots, descriptors and searches";
        compare_tail_fixture(NULL, NULL); compare_tail_fixture(&empty, NULL);
        CHECK(sqlparser_walk_message_tree(NULL, &search, NULL) == m18_walk_message_tree(NULL, &search, NULL));
        CHECK(sqlparser_walk_message_tree(&empty, NULL, NULL) == m18_walk_message_tree(&empty, NULL, NULL));
        CHECK(m22_checked_names(NULL, NULL, NULL) == SQLPARSER_STATUS_OK);
        CHECK(m22_checked_names(&nodes[0].base, &context, NULL) == SQLPARSER_STATUS_OK);
    }
}

static void dynamic_wrapper_cases(void)
{
    PgQuery__VariableSetStmt variable = PG_QUERY__VARIABLE_SET_STMT__INIT;
    PgQuery__FuncCall func = PG_QUERY__FUNC_CALL__INIT;
    PgQuery__Constraint constraint = PG_QUERY__CONSTRAINT__INIT;
    PgQuery__AlterTableCmd alter = PG_QUERY__ALTER_TABLE_CMD__INIT;
    PgQuery__String string = PG_QUERY__STRING__INIT;
    PgQuery__Node node = PG_QUERY__NODE__INIT;
    const struct { ProtobufCMessage *owner; const char *enum_field; } rows[] = {
        {&variable.base, "kind"}, {&func.base, "funcformat"},
        {&constraint.base, "contype"}, {&alter.base, "subtype"}
    };
    string.sval = (char *)"dynamic_name"; node.node_case = PG_QUERY__NODE__NODE_STRING; node.string = &string;
    for (size_t k = 0; k < COUNT(rows); k++) {
        ProtobufCMessage *owner = rows[k].owner;
        const ProtobufCFieldDescriptor *kind = protobuf_c_message_descriptor_get_field_by_name(owner->descriptor, rows[k].enum_field);
        const ProtobufCEnumDescriptor *enums = kind->descriptor;
        case_name = owner->descriptor->short_name;
        for (unsigned v = 0; v < enums->n_values; v++) {
            int value = enums->values[v].value;
            memcpy((char *)owner + kind->offset, &value, sizeof(value));
            for (unsigned f = 0; f < owner->descriptor->n_fields; f++) {
                sqlparser_name_context_t context = sqlparser_next_name_context(owner, owner->descriptor, &owner->descriptor->fields[f], NULL);
                context.location_owner = owner;
                compare_tail_fixture(&node.base, &context);
                context.identifier_forbidden = 1; compare_tail_fixture(&node.base, &context);
                tail_dynamic++;
            }
        }
    }
}

/* A valid String-shaped message with no location field makes inherited-owner
 * preservation and omit-mode owner clearing directly observable. */
static const ProtobufCMessageDescriptor no_location_string_descriptor;
static void no_location_string_init(ProtobufCMessage *message)
{
    pg_query__string__init((PgQuery__String *)message);
    message->descriptor = &no_location_string_descriptor;
}
static const ProtobufCFieldDescriptor no_location_string_fields[] = {
    {"sval", 1201, PROTOBUF_C_LABEL_OPTIONAL, PROTOBUF_C_TYPE_STRING, 0,
     offsetof(PgQuery__String, sval), NULL, NULL, 0, 0, NULL, NULL}
};
static const unsigned no_location_name_order[] = {0};
static const ProtobufCIntRange no_location_ranges[] = {{1201, 0}, {0, 1}};
static const ProtobufCMessageDescriptor no_location_string_descriptor = {
    PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC, "test.String", "String", "TestString", "test",
    sizeof(PgQuery__String), 1, no_location_string_fields, no_location_name_order, 1,
    no_location_ranges, no_location_string_init, NULL, NULL, NULL
};
static void explicit_owner_and_seed_cases(void)
{
    PgQuery__String string;
    PgQuery__Node node = PG_QUERY__NODE__INIT;
    PgQuery__ColumnRef owner = PG_QUERY__COLUMN_REF__INIT;
    sqlparser_name_context_t context = TEST_CONTEXT("ColumnRef", "fields", &owner.base, &owner.base, 0);
    char untouched[] = "unmatched-slot";
    char *unmatched = untouched;
    const ProtobufCFieldDescriptor *f = protobuf_c_message_descriptor_get_field_by_name(owner.base.descriptor, "fields");
    no_location_string_init((ProtobufCMessage *)&string); string.sval = (char *)"inherited_identifier";
    node.node_case = PG_QUERY__NODE__NODE_STRING; node.string = &string;
    context = sqlparser_next_name_context(&owner.base, owner.base.descriptor, f, NULL);
    context.location_owner = &owner.base;
    case_name = "inherited location owner and seeded ordinals";
    CHECK(reference_location(&string.base) == NULL);
    for (int omit = 0; omit <= 1; omit++) {
        sqlparser_name_search_t search = {0}; sqlparser_name_view_t view = {0};
        compare_tail_fixture(&node.base, &context);
        search.want_target = 1; search.target_index = 7; search.seen = 7;
        search.omit_location_owner = omit; search.name_view = &view;
        CHECK(m22_checked_names(&node.base, &context, &search) == SQLPARSER_STATUS_OK);
        CHECK(search.target_slot == &string.sval && search.seen == 8);
        CHECK(search.target_owner == (omit ? NULL : &owner.base));
        CHECK(search.target_field_owner == &owner.base && view.value == string.sval);
        CHECK(view.owner_type == context.owner_type && view.field_name == context.field_name);
        search.seen = 17; /* Preexisting target must return before visiting. */
        CHECK(m22_checked_names(&node.base, &context, &search) == SQLPARSER_STATUS_OK && search.seen == 17);
        memset(&search, 0, sizeof(search)); search.match_slot = &string.sval; search.seen = 7;
        search.omit_location_owner = omit;
        CHECK(m22_checked_names(&node.base, &context, &search) == SQLPARSER_STATUS_OK);
        CHECK(search.target_index == 7 && search.seen == 8);
        memset(&search, 0, sizeof(search)); search.match_slot = &unmatched; search.seen = 7;
        search.omit_location_owner = omit;
        CHECK(m22_checked_names(&node.base, &context, &search) == SQLPARSER_STATUS_OK);
        CHECK(search.target_slot == NULL && search.seen == 8);
    }
    {
        sqlparser_message_search_t search = {0};
        search.descriptor = &pg_query__node__descriptor; search.seen = 7;
        search.target_index = 7; search.want_target = 1; compare_tree(&node.base, search);
        search.target_index = 8; compare_tree(&node.base, search);
        search.descriptor = &no_location_string_descriptor; search.target_index = 7;
        compare_tree(&node.base, search);
        search.want_target = 0; compare_tree(&node.base, search);
    }
    context.identifier_forbidden = 1;
    compare_tail_fixture(&node.base, &context);
    tail_custom++;
}

int main(int argc, char **argv)
{
    tail_schema_cases(); callback_mutations(); alias_depth_and_custom_cases(); dynamic_wrapper_cases(); explicit_owner_and_seed_cases();
    CHECK(tail_alternatives == 268 && callback_pairs == 84 && tail_depths == 2);
    {
        int result = m18_schema_public_main(argc, argv);
        fprintf(stderr, "TAIL alternatives=%zu fixtures=%zu tree_pairs=%zu name_pairs=%zu callback_pairs=%zu dynamic=%zu custom=%zu depths=%zu\n",
            tail_alternatives, tail_fixtures, tail_tree_pairs, tail_name_pairs, callback_pairs, tail_dynamic, tail_custom, tail_depths);
        return result;
    }
}
