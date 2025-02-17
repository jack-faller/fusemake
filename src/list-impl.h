#define LIST_IMPL
#include "list-decl.h"
#include <stdlib.h>

#define List_T of(List, list_T)

List_T of(List_T, new)() { return of(List_T, new_with_capactity)(4); }
List_T of(List_T, new_with_capactity)(unsigned capacity) {
	return (List_T) {
		.elements = malloc(sizeof(list_T) * capacity),
		.length = 0,
		.capacity = capacity,
	};
}
void method(free, List_T, *list) { free(list->elements); }
void method(grow_exact, List_T, *list, unsigned additional_elements) {
	list->elements
		= realloc(list->elements, list->capacity += additional_elements);
}
void method(grow, List_T, *list) {
	of(List_T, grow_exact)(list, list->capacity / 2 + 1);
}
void method(reserve, List_T, *list, unsigned count) {
	int new_length = list->length + count;
	while (new_length >= list->capacity)
		of(List_T, grow)(list);
}
void method(push, List_T, *list, list_T item) {
	unsigned new_length = list->length + 1;
	if (new_length >= list->capacity)
		of(List_T, grow)(list);
	list->elements[list->length = new_length] = item;
}

#undef List_T
#undef list_T
