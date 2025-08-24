// flecs_impl.c
// Compile Flecs implementation from C (not C++) to avoid C++ warnings/errors.
#define FLECS_IMPL
#define FLECS_NO_CPP
#define FLECS_META
#define FLECS_DOC
#define FLECS_PARSER
#define FLECS_QUERY
#define FLECS_JSON
#include <flecs.h>

