#pragma once
#define ARG_UNUSED(x) (void)(x)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define BIT(n) (1U << (n))
