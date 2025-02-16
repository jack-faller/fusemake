#ifndef POOL_H
#define POOL_H

#include "utils.h"

// Make linting work.
#ifndef POOL_T
#define POOL_T int
#endif

typedef union of(Pool_Element, POOL_T) {
  POOL_T value;
	unsigned next_free;
} of(Pool_Element, POOL_T);

#define LIST_T of(Pool_Element, POOL_T)
#include "list.h"
#undef LIST_T

typedef struct of(Pool, POOL_T) {
	of(List, of(Pool_Element, POOL_T)) elements;
	unsigned back;
} of(Pool, POOL_T);

#define pool_at(N) elements.list_at(N).value

unsigned of(of(Pool, POOL_T), next)(of(Pool, POOL_T) *pool);
unsigned of(of(Pool, POOL_T), remove)(of(Pool, POOL_T) *pool);

#endif
