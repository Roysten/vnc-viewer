#pragma once

#define ARRAY_COUNT(X) sizeof(X) / sizeof(*(X))

#define container_of(ptr, type, member) (type *)((char *)(ptr)-offsetof(type, member))
