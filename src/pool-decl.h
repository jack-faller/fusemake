#define Pool_T of(Pool, pool_T)

#include "utils.h"

// Make linting work.
#ifndef pool_T
#define pool_T int
#endif

typedef union of(Pool_Element, pool_T) {
	pool_T value;
	unsigned next;
} of(Pool_Element, pool_T);

#define list_T of(Pool_Element, pool_T)
#ifdef POOL_IMPL
#include "list-impl.h"
#else
#include "list-decl.h"
#endif
#undef list_T

typedef struct of(Pool, pool_T) {
	of(List, of(Pool_Element, pool_T)) elements;
	unsigned next;
} of(Pool, pool_T);

#ifndef pool_at
#define pool_at(N) elements.list_at(N).value
#endif

unsigned method(next, Pool_T, *pool);
void method(remove, Pool_T, * pool, unsigned element);
Pool_T of(Pool_T, new)();
void method(free, Pool_T, * pool);

#undef Pool_T
#ifndef POOL_IMPL
#undef pool_T
#endif
