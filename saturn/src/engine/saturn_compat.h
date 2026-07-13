/*----------------------
 | saturn_compat.h
 | Description: The C-runtime shims mojozork.c needs on Saturn that the SRL dummy
 |   headers omit: a minimal always-failing stdio FILE API (for the file-I/O paths
 |   never executed on Saturn), the heap API, and snprintf/strdup. The heap and
 |   strdup are defined in saturn_compat.cxx (routed onto SRL's HighWorkRam TLSF
 |   allocator); the file stubs in saturn_filestub.c; snprintf links from newlib.
 | Author: suinevere
 | Dependencies: stddef.h
 ----------------------*/
#ifndef SATURN_COMPAT_H
#define SATURN_COMPAT_H
#include <stddef.h>

/*----------------------
 | SEEK_SET / SEEK_CUR / SEEK_END
 | Description: The fseek whence constants the SRL dummy <stdio.h> omits, needed by
 |   mojozork.c's (never-executed-on-Saturn) file code.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | FILE + fopen/fclose/fread/fwrite/fseek/ftell/rewind
 | Description: A minimal FILE type and always-failing file API, existing only so
 |   the never-executed file-I/O paths in mojozork.c compile and link on Saturn
 |   (stubs in saturn_filestub.c).
 | Author: suinevere
 ----------------------*/
typedef struct MZ_FILE FILE;
FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int    fseek(FILE *stream, long offset, int whence);
long   ftell(FILE *stream);
void   rewind(FILE *stream);

/*----------------------
 | malloc / free / realloc / snprintf / strdup
 | Description: The heap and string helpers the SRL environment does not declare
 |   but mojozork.c uses on live paths. malloc/free/realloc/strdup are defined in
 |   saturn_compat.cxx onto SRL's HighWorkRam TLSF allocator; snprintf links from
 |   newlib. Declared here so mojozork.c compiles cleanly.
 | Author: suinevere
 ----------------------*/
void  *malloc(size_t size);
void   free(void *ptr);
void  *realloc(void *ptr, size_t size);
int    snprintf(char *str, size_t size, const char *fmt, ...);
char  *strdup(const char *s);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_COMPAT_H */
