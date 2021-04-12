#pragma once

#include <stddef.h>

typedef struct _Grep Grep;

void
GrepFree(Grep *g);

Grep *
GrepInit(int recursive,
         const char **paths,
         size_t npaths);

typedef int (*GrepCallback) (const char *file,
                             const char *pattern,
                             int linenumber,
                             int filename);

int
GrepDo(Grep *grep,
       const char *pattern,
       int linenumber,
       int filename,
       GrepCallback cb);
