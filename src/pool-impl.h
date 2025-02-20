#define POOL_IMPL
#include "pool-decl.h"
#define Pool_T of(Pool, pool_T)

Pool_T of(Pool_T, new)() {
	return (Pool_T) {
		.elements = of(of(List, of(Pool_Element, pool_T)), new)(),
	};
}
void method(free, Pool_T, *pool) {
  of(of(List, of(Pool_Element, pool_T)), free)(&pool->elements);
}
unsigned method(next, Pool_T, *pool) {
	if (pool->next == pool->elements.length) {
		of(of(List, of(Pool_Element, pool_T)), reserve)(&pool->elements, 1);
		pool->elements.length++;
		return pool->next++;
	} else {
		unsigned out = pool->next;
		pool->next = pool->elements.list_at(out).next;
		return out;
	}
}
void method(remove, Pool_T, *pool, unsigned element) {
	pool->elements.list_at(element).next = pool->next;
	pool->next = element;
}

#undef Pool_T
#undef pool_T
#undef POOL_IMPL
