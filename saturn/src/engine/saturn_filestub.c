/*----------------------
 | saturn_filestub.c
 | Description: No-op stdio file operations. Saturn has no writable filesystem
 |   here, so every file op fails. These are referenced only by code paths that
 |   are unreached on Saturn (the core's save/restore stubs and the #script
 |   loader); the stubs exist purely so that code compiles and links.
 | Author: suinevere
 | Dependencies: saturn_compat.h (FILE, size_t)
 ----------------------*/
#include "saturn_compat.h"

/*----------------------
 | fopen / fclose / fread / fwrite / fseek / ftell / rewind
 | Description: Failing stdio stubs -- fopen returns NULL, reads/writes report 0
 |   bytes, fseek/ftell return -1, and fclose/rewind do nothing.
 | Author: suinevere
 ----------------------*/
FILE  *fopen(const char *path, const char *mode) { (void)path; (void)mode; return (FILE *)0; }
int    fclose(FILE *s)                            { (void)s; return 0; }
size_t fread(void *p, size_t sz, size_t n, FILE *s)  { (void)p; (void)sz; (void)n; (void)s; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *s) { (void)p; (void)sz; (void)n; (void)s; return 0; }
int    fseek(FILE *s, long off, int wh)           { (void)s; (void)off; (void)wh; return -1; }
long   ftell(FILE *s)                             { (void)s; return -1L; }
void   rewind(FILE *s)                            { (void)s; }
