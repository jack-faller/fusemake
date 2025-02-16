#include "list.h"
#include <stdlib.h>

of(List, LIST_T) of(of(List, LIST_T), new)() {
	return of(of(List, LIST_T), new_with_capactity)(4);
}
of(List, LIST_T) of(of(List, LIST_T), new_with_capactity)(unsigned capacity) {
	return (of(List, LIST_T)) {
		.elements = malloc(sizeof(LIST_T) * capacity),
		.length = 0,
		.capacity = capacity,
	};
}
void of(of(List, LIST_T), free)(of(List, LIST_T) * list) { free(list->elements); }

void of(grow_exact, LIST_T)(of(List, LIST_T) * list, unsigned additional_elements) {
  list->elements = realloc(list->elements, list->capacity += additional_elements);
}
void of(push, LIST_T)(of(List, LIST_T) * list, LIST_T item) {
	if (!(list->length < list->capacity))
		of(grow_exact, LIST_T)(list, list->capacity / 2 + 1);
	list->elements[list->length++] = item;
}
