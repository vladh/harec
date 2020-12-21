#include <assert.h>
#include <stdlib.h>
#include "identifier.h"
#include "scope.h"
#include "trace.h"

struct scope *
scope_push(struct scope **stack, enum trace_sys sys)
{
	struct scope *new = calloc(1, sizeof(struct scope));
	if (*stack) {
		new->parent = *stack;
	}
	*stack = new;
	if (sys != TR_MAX) {
		trenter(sys, "scope %p", new);
	}
	return new;
}

struct scope *
scope_pop(struct scope **stack, enum trace_sys sys)
{
	struct scope *prev = *stack;
	assert(prev);
	*stack = prev->parent;
	if (sys != TR_MAX) {
		trleave(sys, NULL);
	}
	return prev;
}

void
scope_free(struct scope *scope)
{
	if (!scope) {
		return;
	}

	struct scope_object *obj = scope->objects;
	while (obj) {
		struct scope_object *next = obj->next;
		free(obj);
		obj = next;
	}

	free(scope);
}

void
scope_free_all(struct scopes *scopes)
{
	while (scopes) {
		struct scopes *next = scopes->next;
		scope_free(scopes->scope);
		free(scopes);
		scopes = next;
	}
}

void
scope_insert(const struct identifier *ident, const struct type *type)
{
	assert(0); // TODO
}

const struct type *
scope_lookup(const struct identifier *ident)
{
	assert(0); // TODO
}
