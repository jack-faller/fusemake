#ifndef VECTOR_H
#define VECTOR_H

#include "utils.h"

// Make linting work.
#ifndef T
#define T int
#endif

typedef struct of(List, T) {
  T *at;
  unsigned length, capacity;
} of(List, T);

void of(push, T)(of(List, T) *list, T item);
void of(grow_exact, T)(of(List, T) *list, unsigned additional_elements);
of(List, T) of(of(List, T), new)();
of(List, T) of(of(List, T), new_with_capactity)(unsigned capacity);
void of(of(List, T), free)(of(List, T) *list);

#endif
