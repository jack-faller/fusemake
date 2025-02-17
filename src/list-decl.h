#define List_T of(List, list_T)

#include "utils.h"

// Make linting work.
#ifndef list_T
#define list_T int
#endif

typedef struct of(List, list_T) {
	list_T *elements;
	unsigned length, capacity;
} of(List, list_T);

#ifndef list_at
#define list_at(N) elements[(N)]
#endif

void method(push, List_T, *list, list_T item);
void method(grow_exact, List_T, *list, unsigned additional_elements);
void method(grow, List_T, * list);
void method(reserve, List_T, * list, unsigned elements);
void method(free, List_T, * list);
List_T of(List_T, new)();
List_T of(List_T, new_with_capactity)(unsigned capacity);

#undef List_T
#ifndef LIST_IMPL
#undef list_T
#endif
