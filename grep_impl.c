#include "grep_impl.h"

#include <errno.h>
#include <glib.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ENABLE_MUILTITHREAD 1
#define MAX_THREAD_NUM 10

struct _Grep {
    /* Implement me */
    GSList **paths;
};

struct Param_t {
    const char *file;
    const char *pattern;
    int linenumber;
    int filename;
    int len;
    GrepCallback cb;
};

/**
 * GrepFree:
 * @grep: Grep structure
 *
 * Free given Grep structure among with its members (that need
 * it). NOP if @g is NULL.
 */
void GrepFree(Grep *grep) {
    /* Implement me */
    if (!grep || !grep->paths) {
        perror("grep is empty");
        return;
    }
    g_slist_free(*(grep->paths));
    free(grep);
}

/**
 * example of path traversal function
 */
static int addPaths(GSList **paths, const char *path, int recursive) {
    struct stat sb;
    GDir *dirp = NULL;

    if (strcmp(path, "-") == 0) path = "/dev/stdin";

    if (lstat(path, &sb) < 0) {
        perror(path);
        goto error;
    }

    if (!S_ISDIR(sb.st_mode)) {
        // TODO: Symlinks should not be followed
        *paths = g_slist_prepend(*paths, g_strdup(path));
    } else {
        const char *f;
        // not recursive access a directory should return error
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

            if (addPaths(paths, newpath, recursive) < 0) goto error;
        }
    }

    if (dirp) g_dir_close(dirp);
    return 0;

    error:
    if (dirp) g_dir_close(dirp);
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
Grep *GrepInit(int recursive, const char **paths, size_t npaths) {
    /* Implement me */
    Grep *grep = (Grep *)malloc(sizeof(Grep));
    GSList **processed_paths = NULL;
    char *path = NULL;

    if (!grep) {
        perror("malloc");
        return NULL;
    }

    if (npaths == 0 || !paths) {
        if (! recursive) {
            // no path is given to GrepInit() then get standard input
            path = "/dev/stdin";
        } else {
            // work through current directory
            path = "./";
        }
        if (addPaths(processed_paths, path, recursive) == -1) {
            return NULL;
        }

        grep->paths = processed_paths;
        return grep;
    }

    for (int i = 0; i < npaths; i++) {
        if (addPaths(processed_paths, paths[i], recursive) == -1) {
            return NULL;
        }
    }

    grep->paths = processed_paths;
    return grep;
}

/**
 * MyGrepDoHelper:
 * @file: path of file to be matched
 * @pattern: pattern to match
 * @linenumber: whether to report line numbers
 * @filename: whether to report filenames
 * @cb: pattern matching callback
 * translate filename value for callback
 *
 * Feeds @cb with one path to match in single thread mode
 *
 * Returns: 0 on success,
 *         -1 otherwise.
 */
int SingleThreadHelper(const char *file, const char *pattern, int linenumber,
                       int filename, int len, GrepCallback cb) {
    int retval = 0;
    switch (filename) {
        case 0:
            // if there is just one file to match do NOT report filename,
            // otherwise do report it
            if (len == 1) {
                retval = cb(file, pattern, linenumber, 0);
            } else {
                retval = cb(file, pattern, linenumber, 1);
            }
            break;
        case 1:
            // do report filename
            retval = cb(file, pattern, linenumber, 1);
            break;
        case 2:
            // do NOT report filename, regardless of the number of files to match
            retval = cb(file, pattern, linenumber, 0);
            break;
        default:
            // not supported, should return error
            perror("Unsupported filename");
            retval = -1;
    }
    return retval;
}

/**
 * SingleThreadDo:
 * @grep: Grep structure
 * @pattern: pattern to match
 * @linenumber: whether to report line numbers
 * @filename: whether to report filenames
 * @cb: pattern matching callback
 *
 * Feeds @cb with each path to match in single thread mode
 *
 * Returns: 0 on success,
 *         -1 otherwise.
 */
int SingleThreadDo(Grep *grep, const char *pattern, int linenumber,
                   int filename, GrepCallback cb) {
    /* Implement me */
    int len = g_slist_length(*(grep->paths));
    if (len == 0) {
        perror("empty grep paths");
        return -1;
    }

    // iterate grep and translate GSList to char *
    GSList *iterator = NULL;
    for (iterator = *(grep->paths); iterator; iterator = iterator->next) {
        char *file = (char *)iterator->data;
        if (SingleThreadHelper(file, pattern, linenumber, filename, len, cb) == -1) {
            // If the callback fails, then no further files should be processed and
            // GrepDo() should return an error.
            return -1;
        }
    }

    return 0;
}

/**
 * MultithreadHelper:
 * @arg: a structure which stores parameters for thread
 *
 * Feeds @cb with each path to match in multiple thread mode
 *
 * Returns: NULL on success,
 *          exit with error otherwise.
 */
void *MultithreadHelper(void *arg) {
    struct Param_t *param = (struct Param_t *)arg;
    int retval = 0;
    switch (param->filename) {
        case 0:
            // if there is just one file to match do NOT report filename,
            // otherwise do report it
            if (param->len == 1) {
                retval = param->cb(param->file, param->pattern, param->linenumber, 0);
            } else {
                retval = param->cb(param->file, param->pattern, param->linenumber, 1);
            }
            break;
        case 1:
            // do report filename
            retval = param->cb(param->file, param->pattern, param->linenumber, 1);
            break;
        case 2:
            // do NOT report filename, regardless of the number of files to match
            retval = param->cb(param->file, param->pattern, param->linenumber, 0);
            break;
        default:
            // not supported, should return error
            perror("Unsupported filename");
            retval = -1;
    }

    // exit will also terminate all other threads
    if (retval) exit(-1);

    return NULL;
}

/**
 * MultithreadDo:
 * @grep: Grep structure
 * @pattern: pattern to match
 * @linenumber: whether to report line numbers
 * @filename: whether to report filenames
 * @cb: pattern matching callback
 *
 * Feeds @cb with each path to match in multiple thread mode
 *
 * Returns: 0 on success,
 *         -1 otherwise.
 */
int MultithreadDo(Grep *grep, const char *pattern, int linenumber, int filename,
                  GrepCallback cb) {
    int len = g_slist_length(*(grep->paths));
    pthread_t threads[MAX_THREAD_NUM];
    GSList *iterator = NULL;

    if (len == 0) {
        perror("empty grep paths");
        return -1;
    }
    int i = 0, thread_num = 0;
    for (iterator = *(grep->paths);
         iterator && i < len && thread_num <= MAX_THREAD_NUM;
         iterator = iterator->next, thread_num++) {
        char *file = (char *)iterator->data;
        struct Param_t param;
        param.file = file;
        param.pattern = pattern;
        param.linenumber = linenumber;
        param.filename = filename;
        param.len = len;
        param.cb = cb;
        int retval = pthread_create(&threads[i++], NULL, MultithreadHelper, &param);
        if (retval) {
            return -1;
        }

        if (thread_num == MAX_THREAD_NUM - 1) {
            // recycle threads
            int j = 0;
            while (j < MAX_THREAD_NUM) {
                if (pthread_join(threads[j++], NULL)) {
                    perror("unable to recycle threads");
                    return -1;
                }
            }
        }
        thread_num = 0;
    }

    // recycle all threads
    int j = 0;
    while (j <= thread_num) {
        if (pthread_join(threads[j++], NULL)) {
            perror("unable to recycle threads");
            return -1;
        }
    }

    return 0;
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
int GrepDo(Grep *grep, const char *pattern, int linenumber, int filename,
           GrepCallback cb) {
    if (ENABLE_MUILTITHREAD) {
        return MultithreadDo(grep, pattern, linenumber, filename, cb);
    }

    return SingleThreadDo(grep, pattern, linenumber, filename, cb);
}
