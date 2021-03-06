#include "grep_impl.h"

#include <errno.h>
#include <glib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ENABLE_MUILTITHREAD 1
#define MAX_THREAD_NUM 4
#define DEBUG 0

struct _Grep {
    GSList *paths;
    GSList *curr;
};

struct param_t {
    Grep *grep;
    const char *pattern;
    int linenumber;
    int filename;
    int len;
    GrepCallback cb;
};

pthread_mutex_t mutex;

/**
 * GrepFree:
 * @grep: Grep structure
 *
 * Free given Grep structure among with its members (that need
 * it). NOP if @g is NULL.
 */
void GrepFree(Grep *grep) {
    if (DEBUG) printf("********GrepFree*******\n");

    if (!grep || !grep->paths) {
        perror("grep is empty");
        return;
    }

    g_slist_free_full(grep->paths, (GDestroyNotify)g_free);
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

            if (addPaths(paths, newpath, recursive) < 0) {
                g_free(newpath);
                goto error;
            }
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
    if (DEBUG) printf("********GrepInit*******\n");
    Grep *grep = (Grep *)malloc(sizeof(Grep));
    GSList *processed_paths = NULL;
    char path[80];

    if (!grep) {
        perror("malloc");
        return NULL;
    }

    if (DEBUG) printf("check npaths\n");
    if (npaths == 0) {
        if (DEBUG) printf("no path is given to GrepInit()\n");
        if (!recursive) {
            if (DEBUG) printf("get standard input\n");
            // no path is given to GrepInit() then get standard input
            strcpy(path, "/dev/stdin");
        } else {
            // work through current directory
            if (DEBUG) printf("work through current directory\n");
            getcwd(path, sizeof(path));
        }

        if (addPaths(&processed_paths, path, recursive) == -1) {
            return NULL;
        }
        grep->paths = processed_paths;
        grep->curr = processed_paths;
        return grep;
    }

    if (DEBUG) printf("add Paths\n");
    for (size_t i = 0; i < npaths; i++) {
        if (addPaths(&processed_paths, paths[i], recursive) == -1) {
            return NULL;
        }
    }
    if (DEBUG) printf("update grep\n");
    grep->paths = processed_paths;
    grep->curr = processed_paths;
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
            // do NOT report filename, regardless of the number of files to
            // match
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
    if (DEBUG) printf("********SingleThreadDo*******\n");
    int len = g_slist_length(grep->paths);
    if (len == 0) {
        perror("empty grep paths");
        return -1;
    }

    // iterate grep and translate GSList to char *
    GSList *iterator = NULL;
    for (iterator = grep->paths; iterator; iterator = iterator->next) {
        char *file = (char *)iterator->data;
        if (SingleThreadHelper(file, pattern, linenumber, filename, len, cb) ==
            -1) {
            // If the callback fails, then no further files should be processed
            // and GrepDo() should return an error.
            return -1;
        }
    }
    if (DEBUG) printf("GrepDo finished\n");
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
    struct param_t *param = (struct param_t *)arg;
    while (1) {
        char *file = NULL;
        pthread_mutex_lock(&mutex);
        // get a path to be processed
        if (!param->grep->curr) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        file = (char *)param->grep->curr->data;
        // update curr
        param->grep->curr = param->grep->curr->next;
        pthread_mutex_unlock(&mutex);

        int retval = 0;
        switch (param->filename) {
            case 0:
                // if there is just one file to match do NOT report filename,
                // otherwise do report it
                if (param->len == 1) {
                    retval =
                        param->cb(file, param->pattern, param->linenumber, 0);
                } else {
                    retval =
                        param->cb(file, param->pattern, param->linenumber, 1);
                }
                break;
            case 1:
                // do report filename
                retval = param->cb(file, param->pattern, param->linenumber, 1);
                break;
            case 2:
                // do NOT report filename, regardless of the number of files to
                // match
                retval = param->cb(file, param->pattern, param->linenumber, 0);
                break;
            default:
                // not supported, should return error
                perror("Unsupported filename");
                retval = -1;
                // exit will also terminate all other threads
                if (retval) exit(-1);
        }
    }

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
    int len = g_slist_length(grep->paths);
    pthread_t threads[MAX_THREAD_NUM];
    pthread_mutex_init(&mutex, NULL);
    grep->curr = grep->paths;

    if (len == 0) {
        perror("empty grep paths");
        return -1;
    }

    struct param_t param;
    param.grep = grep;
    param.pattern = pattern;
    param.linenumber = linenumber;
    param.filename = filename;
    param.len = len;
    param.cb = cb;

    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        int retval =
            pthread_create(&threads[i], NULL, MultithreadHelper, &param);
        if (retval) {
            return -1;
        }
    }
    if (DEBUG) printf("recycle all threads\n");
    // recycle all threads
    int j = 0;
    while (j < MAX_THREAD_NUM) {
        if (pthread_join(threads[j++], NULL)) {
            perror("unable to recycle threads");
            return -1;
        }
    }
    if (DEBUG) printf("GrepDo finished\n");
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
    if (DEBUG) printf("********GrepDo*******\n");
    if (ENABLE_MUILTITHREAD) {
        if (DEBUG) printf("running in multithread mode\n");
        return MultithreadDo(grep, pattern, linenumber, filename, cb);
    }
    if (DEBUG) printf("running in single thread mode\n");
    return SingleThreadDo(grep, pattern, linenumber, filename, cb);
}
