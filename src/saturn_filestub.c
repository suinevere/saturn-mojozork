#include "saturn_compat.h"

/* Saturn has no writable filesystem here; every file op fails. The mojozork
   code paths that call these (save/restore, #script, loadStory) all handle
   failure gracefully and none run in normal play. */
FILE  *fopen(const char *path, const char *mode) { (void)path; (void)mode; return (FILE *)0; }
int    fclose(FILE *s)                            { (void)s; return 0; }
size_t fread(void *p, size_t sz, size_t n, FILE *s)  { (void)p; (void)sz; (void)n; (void)s; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *s) { (void)p; (void)sz; (void)n; (void)s; return 0; }
int    fseek(FILE *s, long off, int wh)           { (void)s; (void)off; (void)wh; return -1; }
long   ftell(FILE *s)                             { (void)s; return -1L; }
void   rewind(FILE *s)                            { (void)s; }
