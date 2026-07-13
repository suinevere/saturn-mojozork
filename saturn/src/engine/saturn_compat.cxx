/*----------------------
 | saturn_compat.cxx
 | Description: Routes the process-wide C heap (malloc/free/realloc/strdup) onto
 |   SRL's TLSF allocator in High Work RAM, so mojozork.c and any newlib/libstdc++
 |   code share one unified heap. This heap is only valid AFTER
 |   SRL::Core::Initialize() runs at the top of main(), so no global/static object
 |   whose constructor allocates may be added -- it would call this malloc before
 |   the SRL heap exists and fault.
 | Author: suinevere
 | Dependencies: saturn_compat.h, SRL (Memory::HighWorkRam)
 ----------------------*/
#include <srl.hpp>
#include "saturn_compat.h"

/*----------------------
 | malloc / free / realloc
 | Description: The standard C allocator, forwarded to SRL's High Work RAM TLSF
 |   arena. free ignores a NULL pointer.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | strdup
 | Description: Duplicates a NUL-terminated string into a fresh High Work RAM
 |   allocation, hand-rolled so it needs no <string.h>.
 | Author: suinevere
 | Dependencies: SRL (Memory::HighWorkRam)
 | Globals: N/A
 | Params: s -- the string to copy
 | Returns: the new copy, or NULL if the allocation failed
 ----------------------*/
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
