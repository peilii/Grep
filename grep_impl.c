#include <glib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "grep_impl.h"


struct _Grep {
    /* Implement me */
};


/**
 * GrepFree:
 * @grep: Grep structure
 *
 * Free given Grep structure among with its members (that need
 * it). NOP if @g is NULL.
 */
void
GrepFree(Grep *grep)
{
    /* Implement me */
}


static int
addPaths(GSList **paths,
         const char *path,
         int recursive)
{
    struct stat sb;
    GDir *dirp = NULL;

    if (strcmp(path, "-") == 0)
        path = "/dev/stdin";

    if (lstat(path, &sb) < 0) {
        perror(path);
        goto error;
    }

    if (!S_ISDIR(sb.st_mode)) {
        *paths = g_slist_prepend(*paths, g_strdup(path));
    } else {
        const char *f;

        if (!recursive) {
            errno = EISDIR;
            perror(path);
            goto error;
        }

        if (!(dirp = g_dir_open(path, 0, NULL))) {
            perror(path);
            goto error;
        }

        while ((f = g_dir_read_name(dirp))) {
            g_autofree char *newpath = g_build_filename(path, f, NULL);

            if (addPaths(paths, newpath, recursive) < 0)
                goto error;
        }
    }

    if (dirp)
        g_dir_close(dirp);
    return 0;

 error:
    if (dirp)
        g_dir_close(dirp);
    return -1;
}


/**
 * GrepInit:
 * @recursive: whether to traverse paths recursively
 * @paths: paths to traverse
 * @npaths: number of items in @paths array
 *
 * Allocate and initialize Grep structure. Process given @paths.
 *
 * Returns: an allocated structure on success,
 *          NULL otherwise.
 */
Grep *
GrepInit(int recursive,
         const char **paths,
         size_t npaths)
{
    /* Implement me */
}


/**
 * GrepDo:
 * @grep: Grep structure
 * @pattern: pattern to match
 * @linenumber: whether to report line numbers
 * @filename: whether to report filenames
 * @cb: pattern matching callback
 *
 * Feeds @cb with each path to match.
 *
 * Returns: 0 on success,
 *         -1 otherwise.
 */
int
GrepDo(Grep *grep,
       const char *pattern,
       int linenumber,
       int filename,
       GrepCallback cb)
{
    /* Implement me */
}
