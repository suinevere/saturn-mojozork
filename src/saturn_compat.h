#ifndef SATURN_COMPAT_H
#define SATURN_COMPAT_H
#include <stddef.h>

/* The SRL dummy <stdio.h> omits these; mojozork.c's (never-executed-on-Saturn)
   file code references them. */
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal FILE + always-failing file API. Exist only so the never-executed
   file-I/O paths in mojozork.c compile and link on Saturn. */
typedef struct MZ_FILE FILE;
FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int    fseek(FILE *stream, long offset, int whence);
long   ftell(FILE *stream);
void   rewind(FILE *stream);

/* The SRL environment's <stdlib.h> does not declare the heap API; mojozork.c
   uses these on live paths. We DEFINE malloc/free/realloc/strdup in
   saturn_compat.cxx (routed onto SRL's HighWorkRam TLSF allocator). */
void  *malloc(size_t size);
void   free(void *ptr);
void  *realloc(void *ptr, size_t size);

/* Provided by newlib (freestanding) but the dummy stdio/string headers omit the
   declarations; declare them so mojozork.c compiles cleanly. */
int    snprintf(char *str, size_t size, const char *fmt, ...);
char  *strdup(const char *s);   /* we DEFINE this one (see saturn_compat.cxx) */

#ifdef __cplusplus
}
#endif
#endif /* SATURN_COMPAT_H */
