#include "list.h"
#include <stdlib.h>

of(List, T) of(of(List, T), new)() {
	return of(of(List, T), new_with_capactity)(4);
}
of(List, T) of(of(List, T), new_with_capactity)(unsigned capacity) {
	return (of(List, T)) {
		.at = malloc(sizeof(T) * capacity),
		.length = 0,
		.capacity = capacity,
	};
}
void of(of(List, T), free)(of(List, T) * list) { free(list->at); }

void of(grow_exact, T)(of(List, T) * list, unsigned additional_elements) {
  list->at = realloc(list->at, list->capacity += additional_elements);
}
void of(push, T)(of(List, T) * list, T item) {
	if (!(list->length < list->capacity))
		of(grow_exact, T)(list, list->capacity / 2 + 1);
	list->at[list->length++] = item;
}
