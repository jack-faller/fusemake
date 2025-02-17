#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stdio.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...) eprintf(__VA_ARGS__)

#define of_0(A, B) A##_##B
#define of(A, B) of_0(A, B)

#define method(name, on, ...) of(on, name)(on __VA_ARGS__)

#endif
