#include <srl.hpp>
#include "saturn_compat.h"

// Route the C heap onto SRL's TLSF allocator in High Work RAM.
extern "C" void *malloc(size_t size)
{
    return SRL::Memory::HighWorkRam::Malloc(size);
}

extern "C" void free(void *ptr)
{
    if (ptr != nullptr) {
        SRL::Memory::HighWorkRam::Free(ptr);
    }
}

extern "C" void *realloc(void *ptr, size_t size)
{
    return SRL::Memory::HighWorkRam::Realloc(ptr, size);
}

extern "C" char *strdup(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    char *d = (char *) SRL::Memory::HighWorkRam::Malloc(n + 1);
    if (d) {
        for (size_t i = 0; i <= n; i++) d[i] = s[i];
    }
    return d;
}
