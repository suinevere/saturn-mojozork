#include <srl.hpp>
#include "saturn_compat.h"

// Route the C heap onto SRL's TLSF allocator in High Work RAM.
// Intentional: this becomes the process-wide C heap. mojozork.c and any newlib/
// libstdc++ code that calls malloc/free are routed to SRL's TLSF High Work RAM
// arena — a single unified heap for the whole program.
// WARNING: this heap is only valid AFTER SRL::Core::Initialize() runs (called at
// the top of main()). Do NOT add global/static objects whose constructors allocate
// — they would call this malloc before the SRL heap exists and fault.
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
