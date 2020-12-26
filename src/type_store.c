#include <assert.h>
#include <stdlib.h>
#include "check.h"
#include "eval.h"
#include "type_store.h"
#include "util.h"

static size_t
ast_array_len(struct type_store *store, const struct ast_type *atype)
{
	// TODO: Maybe we should cache these
	struct expression in, out;
	if (atype->array.length == NULL) {
		return SIZE_UNDEFINED;
	}
	check_expression(store->check_context, atype->array.length, &in);
	enum eval_result r = eval_expr(store->check_context, &in, &out);
	// TODO: Bubble up these errors:
	assert(r == EVAL_OK);
	assert(type_is_integer(out.result));
	if (type_is_signed(out.result)) {
		assert(out.constant.ival > 0);
	}
	return (size_t)out.constant.uval;
}

bool
type_is_assignable(struct type_store *store,
	const struct type *to,
	const struct type *from)
{
	// const and non-const types are mutually assignable
	if (to->flags & TYPE_CONST) {
		to = type_store_lookup_with_flags(store,
			to, to->flags & ~TYPE_CONST);
	}
	if (from->flags & TYPE_CONST) {
		from = type_store_lookup_with_flags(store,
			from, from->flags & ~TYPE_CONST);
	}

	if (to == from) {
		return true;
	}

	switch (to->storage) {
	case TYPE_STORAGE_I8:
	case TYPE_STORAGE_I16:
	case TYPE_STORAGE_I32:
	case TYPE_STORAGE_I64:
	case TYPE_STORAGE_INT:
		return type_is_integer(from)
			&& type_is_signed(from)
			&& to->size >= from->size;
	case TYPE_STORAGE_SIZE:
	case TYPE_STORAGE_U8:
	case TYPE_STORAGE_U16:
	case TYPE_STORAGE_U32:
	case TYPE_STORAGE_U64:
	case TYPE_STORAGE_UINT:
		return type_is_integer(from)
			&& !type_is_signed(from)
			&& to->size >= from->size;
	case TYPE_STORAGE_UINTPTR:
		return (type_is_integer(from)
				&& !type_is_signed(from)
				&& to->size >= from->size)
			|| from->storage == TYPE_STORAGE_POINTER;
	case TYPE_STORAGE_F32:
	case TYPE_STORAGE_F64:
		return type_is_float(from);
	case TYPE_STORAGE_POINTER:
		switch (from->storage) {
		case TYPE_STORAGE_UINTPTR:
			return true;
		case TYPE_STORAGE_NULL:
			return to->pointer.flags & PTR_NULLABLE;
		case TYPE_STORAGE_POINTER:
			if (to->pointer.referent != from->pointer.referent) {
				return false;
			}
			if (to->pointer.flags & PTR_NULLABLE) {
				return from->pointer.flags & PTR_NULLABLE;
			}
			return true;
		default:
			return false;
		}
		assert(0); // Unreachable
	case TYPE_STORAGE_ALIAS:
	case TYPE_STORAGE_ENUM:
	case TYPE_STORAGE_TAGGED_UNION:
		assert(0); // TODO
	// The following types are only assignable from themselves, and are
	// handled above:
	case TYPE_STORAGE_ARRAY:
	case TYPE_STORAGE_BOOL:
	case TYPE_STORAGE_CHAR:
	case TYPE_STORAGE_FUNCTION:
	case TYPE_STORAGE_NULL:
	case TYPE_STORAGE_RUNE:
	case TYPE_STORAGE_SLICE:
	case TYPE_STORAGE_STRING:
	case TYPE_STORAGE_STRUCT:
	case TYPE_STORAGE_UNION:
	case TYPE_STORAGE_VOID:
		return false;
	}

	assert(0); // Unreachable
}

const struct type *
builtin_type_for_storage(enum type_storage storage, bool is_const)
{
	switch (storage) {
	case TYPE_STORAGE_BOOL:
		return is_const ? &builtin_type_const_bool : &builtin_type_bool;
	case TYPE_STORAGE_CHAR:
		return is_const ? &builtin_type_const_char : &builtin_type_char;
	case TYPE_STORAGE_F32:
		return is_const ? &builtin_type_const_f32 : &builtin_type_f32;
	case TYPE_STORAGE_F64:
		return is_const ? &builtin_type_const_f64 : &builtin_type_f64;
	case TYPE_STORAGE_I8:
		return is_const ? &builtin_type_const_i8 : &builtin_type_i8;
	case TYPE_STORAGE_I16:
		return is_const ? &builtin_type_const_i16 : &builtin_type_i16;
	case TYPE_STORAGE_I32:
		return is_const ? &builtin_type_const_i32 : &builtin_type_i32;
	case TYPE_STORAGE_I64:
		return is_const ? &builtin_type_const_i64 : &builtin_type_i64;
	case TYPE_STORAGE_INT:
		return is_const ? &builtin_type_const_int : &builtin_type_int;
	case TYPE_STORAGE_RUNE:
		return is_const ? &builtin_type_const_rune : &builtin_type_rune;
	case TYPE_STORAGE_SIZE:
		return is_const ? &builtin_type_const_size : &builtin_type_size;
	case TYPE_STORAGE_U8:
		return is_const ? &builtin_type_const_u8 : &builtin_type_u8;
	case TYPE_STORAGE_U16:
		return is_const ? &builtin_type_const_u16 : &builtin_type_u16;
	case TYPE_STORAGE_U32:
		return is_const ? &builtin_type_const_u32 : &builtin_type_u32;
	case TYPE_STORAGE_U64:
		return is_const ? &builtin_type_const_u64 : &builtin_type_u64;
	case TYPE_STORAGE_UINT:
		return is_const ? &builtin_type_const_uint : &builtin_type_uint;
	case TYPE_STORAGE_UINTPTR:
		return is_const ? &builtin_type_const_uintptr : &builtin_type_uintptr;
	case TYPE_STORAGE_VOID:
		return &builtin_type_void; // const void and void are the same type
	case TYPE_STORAGE_NULL:
		return &builtin_type_null; // const null and null are the same type
	case TYPE_STORAGE_ALIAS:
	case TYPE_STORAGE_ARRAY:
	case TYPE_STORAGE_FUNCTION:
	case TYPE_STORAGE_POINTER:
	case TYPE_STORAGE_SLICE:
	case TYPE_STORAGE_STRING:
	case TYPE_STORAGE_STRUCT:
	case TYPE_STORAGE_TAGGED_UNION:
	case TYPE_STORAGE_UNION:
	case TYPE_STORAGE_ENUM:
		return NULL;
	}
	assert(0); // Unreachable
}

unsigned long
atype_hash(struct type_store *store, const struct ast_type *type)
{
	unsigned long hash = DJB2_INIT;
	hash = djb2(hash, type->storage);
	hash = djb2(hash, type->flags);

	switch (type->storage) {
	case TYPE_STORAGE_BOOL:
	case TYPE_STORAGE_CHAR:
	case TYPE_STORAGE_F32:
	case TYPE_STORAGE_F64:
	case TYPE_STORAGE_I8:
	case TYPE_STORAGE_I16:
	case TYPE_STORAGE_I32:
	case TYPE_STORAGE_I64:
	case TYPE_STORAGE_INT:
	case TYPE_STORAGE_NULL:
	case TYPE_STORAGE_RUNE:
	case TYPE_STORAGE_SIZE:
	case TYPE_STORAGE_U8:
	case TYPE_STORAGE_U16:
	case TYPE_STORAGE_U32:
	case TYPE_STORAGE_U64:
	case TYPE_STORAGE_UINT:
	case TYPE_STORAGE_UINTPTR:
	case TYPE_STORAGE_VOID:
		break; // built-ins
	case TYPE_STORAGE_ALIAS:
		assert(0); // TODO
	case TYPE_STORAGE_ARRAY:
		hash = djb2(hash, atype_hash(store, type->array.members));
		hash = djb2(hash, ast_array_len(store, type));
		break;
	case TYPE_STORAGE_FUNCTION:
		hash = djb2(hash, atype_hash(store, type->func.result));
		hash = djb2(hash, type->func.variadism);
		hash = djb2(hash, type->func.flags);
		for (struct ast_function_parameters *param = type->func.params;
				param; param = param->next) {
			hash = djb2(hash, atype_hash(store, param->type));
		}
		break;
	case TYPE_STORAGE_ENUM:
		assert(0); // TODO
	case TYPE_STORAGE_POINTER:
		hash = djb2(hash, type->pointer.flags);
		hash = atype_hash(store, type->pointer.referent);
		break;
	case TYPE_STORAGE_SLICE:
	case TYPE_STORAGE_STRING:
	case TYPE_STORAGE_STRUCT:
	case TYPE_STORAGE_TAGGED_UNION:
	case TYPE_STORAGE_UNION:
		assert(0); // TODO
	}
	return hash;
}

unsigned long
type_hash(struct type_store *store, const struct type *type)
{
	unsigned long hash = DJB2_INIT;
	hash = djb2(hash, type->storage);
	hash = djb2(hash, type->flags);
	switch (type->storage) {
	case TYPE_STORAGE_BOOL:
	case TYPE_STORAGE_CHAR:
	case TYPE_STORAGE_F32:
	case TYPE_STORAGE_F64:
	case TYPE_STORAGE_I8:
	case TYPE_STORAGE_I16:
	case TYPE_STORAGE_I32:
	case TYPE_STORAGE_I64:
	case TYPE_STORAGE_INT:
	case TYPE_STORAGE_NULL:
	case TYPE_STORAGE_RUNE:
	case TYPE_STORAGE_SIZE:
	case TYPE_STORAGE_U8:
	case TYPE_STORAGE_U16:
	case TYPE_STORAGE_U32:
	case TYPE_STORAGE_U64:
	case TYPE_STORAGE_UINT:
	case TYPE_STORAGE_UINTPTR:
	case TYPE_STORAGE_VOID:
		break; // built-ins
	case TYPE_STORAGE_ALIAS:
		assert(0); // TODO
	case TYPE_STORAGE_ARRAY:
		hash = djb2(hash, type_hash(store, type->array.members));
		hash = djb2(hash, type->array.length);
		break;
	case TYPE_STORAGE_FUNCTION:
		hash = djb2(hash, type_hash(store, type->func.result));
		hash = djb2(hash, type->func.variadism);
		hash = djb2(hash, type->func.flags);
		for (struct type_func_param *param = type->func.params;
				param; param = param->next) {
			hash = djb2(hash, type_hash(store, param->type));
		}
		break;
	case TYPE_STORAGE_ENUM:
		assert(0); // TODO
	case TYPE_STORAGE_POINTER:
		hash = djb2(hash, type->pointer.flags);
		hash = type_hash(store, type->pointer.referent);
		break;
	case TYPE_STORAGE_SLICE:
	case TYPE_STORAGE_STRING:
	case TYPE_STORAGE_STRUCT:
	case TYPE_STORAGE_TAGGED_UNION:
	case TYPE_STORAGE_UNION:
		assert(0); // TODO
	}
	return hash;
}

static const struct type *
builtin_for_atype(const struct ast_type *atype)
{
	bool is_const = (atype->flags & TYPE_CONST) != 0;
	return builtin_type_for_storage(atype->storage, is_const);
}

static const struct type *
builtin_for_type(const struct type *type)
{
	bool is_const = (type->flags & TYPE_CONST) != 0;
	return builtin_type_for_storage(type->storage, is_const);
}

static bool
type_eq_atype(struct type_store *store,
	const struct type *type,
	const struct ast_type *atype)
{
	if (type->storage != atype->storage || type->flags != atype->flags) {
		return false;
	}

	switch (type->storage) {
	case TYPE_STORAGE_BOOL:
	case TYPE_STORAGE_CHAR:
	case TYPE_STORAGE_F32:
	case TYPE_STORAGE_F64:
	case TYPE_STORAGE_I8:
	case TYPE_STORAGE_I16:
	case TYPE_STORAGE_I32:
	case TYPE_STORAGE_I64:
	case TYPE_STORAGE_INT:
	case TYPE_STORAGE_NULL:
	case TYPE_STORAGE_RUNE:
	case TYPE_STORAGE_SIZE:
	case TYPE_STORAGE_U8:
	case TYPE_STORAGE_U16:
	case TYPE_STORAGE_U32:
	case TYPE_STORAGE_U64:
	case TYPE_STORAGE_UINT:
	case TYPE_STORAGE_UINTPTR:
	case TYPE_STORAGE_VOID:
		return true;
	case TYPE_STORAGE_ALIAS:
		assert(0); // TODO
	case TYPE_STORAGE_ARRAY:
		return type->array.length == ast_array_len(store, atype)
			&& type_eq_atype(store, type->array.members, atype->array.members);
	case TYPE_STORAGE_ENUM:
		assert(0); // TODO
	case TYPE_STORAGE_FUNCTION:
		if (!type_eq_atype(store, type->func.result, atype->func.result)
				|| type->func.variadism != atype->func.variadism
				|| type->func.flags != atype->func.flags) {
			return false;
		}
		struct ast_function_parameters *aparam;
		struct type_func_param *param;
		for (aparam = atype->func.params, param = type->func.params;
				aparam && param;
				aparam = aparam->next, param = param->next) {
			if (!!aparam->next != !!param->next) {
				return false;
			}
			if (!type_eq_atype(store, param->type, aparam->type)) {
				return false;
			}
		}
		return true;
	case TYPE_STORAGE_POINTER:
		return type->pointer.flags == atype->pointer.flags &&
			type_eq_atype(store, type->pointer.referent, atype->pointer.referent);
	case TYPE_STORAGE_SLICE:
	case TYPE_STORAGE_STRING:
	case TYPE_STORAGE_STRUCT:
	case TYPE_STORAGE_TAGGED_UNION:
	case TYPE_STORAGE_UNION:
		assert(0); // TODO
	}

	assert(0); // Unreachable
}

static bool
type_eq_type(struct type_store *store,
	const struct type *a, const struct type *b)
{
	if (a->storage != b->storage || a->flags != b->flags) {
		return false;
	}

	switch (a->storage) {
	case TYPE_STORAGE_BOOL:
	case TYPE_STORAGE_CHAR:
	case TYPE_STORAGE_F32:
	case TYPE_STORAGE_F64:
	case TYPE_STORAGE_I8:
	case TYPE_STORAGE_I16:
	case TYPE_STORAGE_I32:
	case TYPE_STORAGE_I64:
	case TYPE_STORAGE_INT:
	case TYPE_STORAGE_NULL:
	case TYPE_STORAGE_RUNE:
	case TYPE_STORAGE_SIZE:
	case TYPE_STORAGE_U8:
	case TYPE_STORAGE_U16:
	case TYPE_STORAGE_U32:
	case TYPE_STORAGE_U64:
	case TYPE_STORAGE_UINT:
	case TYPE_STORAGE_UINTPTR:
	case TYPE_STORAGE_VOID:
		return true;
	case TYPE_STORAGE_ALIAS:
		assert(0); // TODO
	case TYPE_STORAGE_ARRAY:
		return a->array.length == b->array.length
			&& a->array.expandable == b->array.expandable
			&& type_eq_type(store, a->array.members, b->array.members);
	case TYPE_STORAGE_ENUM:
		assert(0); // TODO
	case TYPE_STORAGE_FUNCTION:
		if (a->func.variadism != b->func.variadism
				|| a->func.flags != b->func.flags
				|| !type_eq_type(store, a->func.result, b->func.result)) {
			return false;
		}
		struct type_func_param *aparam = a->func.params;
		struct type_func_param *bparam = b->func.params;
		while (aparam && bparam) {
			if (!type_eq_type(store, aparam->type, bparam->type)) {
				return false;
			}

			aparam = aparam->next;
			bparam = bparam->next;
		}
		return !aparam && !bparam;
	case TYPE_STORAGE_POINTER:
		return a->pointer.flags == b->pointer.flags &&
			type_eq_type(store, a->pointer.referent, b->pointer.referent);
	case TYPE_STORAGE_SLICE:
	case TYPE_STORAGE_STRING:
	case TYPE_STORAGE_STRUCT:
	case TYPE_STORAGE_TAGGED_UNION:
	case TYPE_STORAGE_UNION:
		assert(0); // TODO
	}

	assert(0); // Unreachable
}

static void
type_init_from_atype(struct type_store *store,
	struct type *type,
	const struct ast_type *atype)
{
	type->storage = atype->storage;
	type->flags = atype->flags;

	switch (type->storage) {
	case TYPE_STORAGE_BOOL:
	case TYPE_STORAGE_CHAR:
	case TYPE_STORAGE_F32:
	case TYPE_STORAGE_F64:
	case TYPE_STORAGE_I8:
	case TYPE_STORAGE_I16:
	case TYPE_STORAGE_I32:
	case TYPE_STORAGE_I64:
	case TYPE_STORAGE_INT:
	case TYPE_STORAGE_NULL:
	case TYPE_STORAGE_RUNE:
	case TYPE_STORAGE_SIZE:
	case TYPE_STORAGE_U8:
	case TYPE_STORAGE_U16:
	case TYPE_STORAGE_U32:
	case TYPE_STORAGE_U64:
	case TYPE_STORAGE_UINT:
	case TYPE_STORAGE_UINTPTR:
	case TYPE_STORAGE_VOID:
		assert(0); // Invariant
	case TYPE_STORAGE_ALIAS:
		assert(0); // TODO
	case TYPE_STORAGE_ARRAY:
		type->array.length = ast_array_len(store, atype);
		type->array.members = type_store_lookup_atype(
				store, atype->array.members);
		// TODO: Bubble this up:
		assert(type->array.members->size != SIZE_UNDEFINED);

		type->align = type->array.members->align;
		if (type->array.length == SIZE_UNDEFINED) {
			type->size = SIZE_UNDEFINED;
		} else {
			type->size = type->array.members->size * type->array.length;
		}
		break;
	case TYPE_STORAGE_ENUM:
		assert(0); // TODO
	case TYPE_STORAGE_FUNCTION:
		type->size = SIZE_UNDEFINED;
		type->align = SIZE_UNDEFINED;
		type->func.result =
			type_store_lookup_atype(store, atype->func.result);
		type->func.variadism = atype->func.variadism;
		type->func.flags = atype->func.flags;
		struct type_func_param *param, **next = &type->func.params;
		for (struct ast_function_parameters *aparam = atype->func.params;
				aparam; aparam = aparam->next) {
			param = *next = xcalloc(1, sizeof(struct type_func_param));
			param->type = type_store_lookup_atype(store, aparam->type);
			next = &param->next;
		}
		break;
	case TYPE_STORAGE_POINTER:
		type->size = 8; // XXX: ARCH
		type->align = 8;
		type->pointer.flags = atype->pointer.flags;
		type->pointer.referent = type_store_lookup_atype(
			store, atype->pointer.referent);
		break;
	case TYPE_STORAGE_SLICE:
	case TYPE_STORAGE_STRING:
	case TYPE_STORAGE_STRUCT:
	case TYPE_STORAGE_TAGGED_UNION:
	case TYPE_STORAGE_UNION:
		assert(0); // TODO
	}
}

static const struct type *type_store_lookup_type(
		struct type_store *store, const struct type *type);

static void
type_init_from_type(struct type_store *store,
	struct type *new, const struct type *old)
{
	new->storage = old->storage;
	new->flags = old->flags;

	switch (old->storage) {
	case TYPE_STORAGE_BOOL:
	case TYPE_STORAGE_CHAR:
	case TYPE_STORAGE_F32:
	case TYPE_STORAGE_F64:
	case TYPE_STORAGE_I16:
	case TYPE_STORAGE_I32:
	case TYPE_STORAGE_I64:
	case TYPE_STORAGE_I8:
	case TYPE_STORAGE_INT:
	case TYPE_STORAGE_NULL:
	case TYPE_STORAGE_RUNE:
	case TYPE_STORAGE_SIZE:
	case TYPE_STORAGE_U16:
	case TYPE_STORAGE_U32:
	case TYPE_STORAGE_U64:
	case TYPE_STORAGE_U8:
	case TYPE_STORAGE_UINT:
	case TYPE_STORAGE_UINTPTR:
	case TYPE_STORAGE_VOID:
		assert(0); // Invariant
	case TYPE_STORAGE_ALIAS:
		assert(0); // TODO
	case TYPE_STORAGE_ARRAY:
		new->array.members = old->array.members;
		new->array.length = old->array.length;
		new->array.expandable = old->array.expandable;
		new->align = new->array.members->align;
		if (new->array.length == SIZE_UNDEFINED) {
			new->size = SIZE_UNDEFINED;
		} else {
			new->size = new->array.members->size * new->array.length;
		}
		break;
	case TYPE_STORAGE_ENUM:
	case TYPE_STORAGE_FUNCTION:
		assert(0); // TODO
	case TYPE_STORAGE_POINTER:
		new->size = 8; // XXX: ARCH
		new->align = 8;
		new->pointer.flags = old->pointer.flags;
		new->pointer.referent = type_store_lookup_type(
			store, old->pointer.referent);
		break;
	case TYPE_STORAGE_SLICE:
	case TYPE_STORAGE_STRING:
	case TYPE_STORAGE_STRUCT:
	case TYPE_STORAGE_TAGGED_UNION:
	case TYPE_STORAGE_UNION:
		assert(0); // TODO
	}
}

const struct type *
type_store_lookup_atype(struct type_store *store, const struct ast_type *atype)
{
	const struct type *builtin = builtin_for_atype(atype);
	if (builtin) {
		return builtin;
	}

	unsigned long hash = atype_hash(store, atype);
	struct type_bucket **next = &store->buckets[hash % TYPE_STORE_BUCKETS];

	struct type_bucket *bucket;
	while (*next) {
		bucket = *next;
		if (type_eq_atype(store, &bucket->type, atype)) {
			return &bucket->type;
		}
		next = &bucket->next;
	}

	bucket = *next = xcalloc(1, sizeof(struct type_bucket));
	type_init_from_atype(store, &bucket->type, atype);
	return &bucket->type;
}

// Used internally for looking up modified forms of other types
static const struct type *
type_store_lookup_type(struct type_store *store, const struct type *type)
{
	const struct type *builtin = builtin_for_type(type);
	if (builtin) {
		return builtin;
	}

	unsigned long hash = type_hash(store, type);
	struct type_bucket **next = &store->buckets[hash % TYPE_STORE_BUCKETS];

	struct type_bucket *bucket;
	while (*next) {
		bucket = *next;
		if (type_eq_type(store, &bucket->type, type)) {
			return &bucket->type;
		}
		next = &bucket->next;
	}

	bucket = *next = xcalloc(1, sizeof(struct type_bucket));
	type_init_from_type(store, &bucket->type, type);
	return &bucket->type;
}

const struct type *
type_store_lookup_with_flags(struct type_store *store,
	const struct type *type, unsigned int flags)
{
	if (type->flags == flags) {
		return type;
	}
	struct type new = *type;
	new.flags = flags;
	return type_store_lookup_type(store, &new);
}

const struct type *
type_store_lookup_pointer(struct type_store *store,
	const struct type *referent, unsigned int ptrflags)
{
	struct type ptr = {
		.storage = TYPE_STORAGE_POINTER,
		.pointer = {
			.referent = referent,
			.flags = ptrflags,
		},
	};
	return type_store_lookup_type(store, &ptr);
}

const struct type *
type_store_lookup_array(struct type_store *store,
	const struct type *members, size_t len, bool expandable)
{
	struct type array = {
		.storage = TYPE_STORAGE_ARRAY,
		.array = {
			.members = members,
			.length = len,
			.expandable = expandable,
		},
	};
	return type_store_lookup_type(store, &array);
}
