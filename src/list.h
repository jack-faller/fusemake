#ifndef LIST_H
#define LIST_H

#include "utils.h"

// Make linting work.
#ifndef LIST_T
#define LIST_T int
#endif

typedef struct of(List, LIST_T) {
	LIST_T *elements;
	unsigned length, capacity;
} of(List, LIST_T);

#ifndef list_at
#define list_at(N) elemetns[(N)]
#endif

void of(push, LIST_T)(of(List, LIST_T) * list, LIST_T item);
void of(grow_exact, LIST_T)(of(List, LIST_T) * list, unsigned additional_elements);
of(List, LIST_T) of(of(List, LIST_T), new)();
of(List, LIST_T) of(of(List, LIST_T), new_with_capactity)(unsigned capacity);
void of(of(List, LIST_T), free)(of(List, LIST_T) * list);

#endif
