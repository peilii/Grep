#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <glib.h>

#include "grep_impl.h"

static void
printUsage(const char *progname)
{
    fprintf(stderr, "Usage: %s [OPTION]... PATTERN [FILE]...\n", progname);
    fprintf(stderr, "\n");
    fprintf(stderr, " -h | --help                display this help text and exit\n");
    fprintf(stderr, " -n | --line-number         print line number with output lines\n");
    fprintf(stderr, " -f | --with-filename       print file name with output lines\n");
    fprintf(stderr, " -F | --no-filename         suppress the file name prefix on output\n");
    fprintf(stderr, " -r | --recursive           read all files under each directory, recursively\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "PATTERN is a single string that is searched for in FILE\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "When FILE is '-', read standard input.  With no FILE, read '.' if\n");
    fprintf(stderr, "recursive, '-' otherwise.  With fewer than two FILEs, assume -F.\n");
    fprintf(stderr, "Exit status is 0 if any line is selected, 1 otherwise;\n");
}


static void
printUsageShort(const char *progname)
{
    fprintf(stderr, "Usage: %s [OPTION]... PATTERN [FILE]...\n", progname);
    fprintf(stderr, "Try %s --help for more information\n", progname);
}


static int
grepCallback(const char *file,
             const char *pattern,
             int linenumber,
             int filename)
{
    FILE *fp = NULL;
    char *line = NULL;
    size_t linelen = 0;
    size_t linenr = 0;
    int ret = -1;

    if (!(fp = fopen(file, "r"))) {
        perror(file);
        goto cleanup;
    }

    while (1) {
        ssize_t n;

        n = getline(&line, &linelen, fp);
        if (n < 0) {
            if (feof(fp)) {
                break;
            } else {
                perror(file);
                goto cleanup;
            }
        }

        linenr++;
        if (strstr(line, pattern)) {
            if (filename)
                printf("%s:", file);
            if (linenumber)
                printf("%zu:", linenr);
            printf("%s", line);

            if (line[strlen(line) - 1] != '\n')
                printf("\n");
        }
    }

    ret = 0;
 cleanup:
    if (fp)
        fclose(fp);
    free(line);
    return ret;
}

typedef struct _grepDoHelperData grepDoHelperData;
struct _grepDoHelperData {
    Grep *grep;
    int linenumber;
    int filename;
    int error;
};

static void
grepDoHelper(void *pattern,
             void *opaque)
{
    grepDoHelperData *data = opaque;

    if (data->error != 0)
        return;

    if (GrepDo(data->grep, pattern, data->linenumber, data->filename, grepCallback) < 0)
        data->error = 1;
}


int main(int argc, char *argv[])
{
    int ret = EXIT_FAILURE;
    Grep *grep = NULL;
    int recursive = 0;
    int linenumber = 0;
    int filename = 0;
    GSList *patterns = NULL;
    grepDoHelperData data = { 0 };
    struct option opts[] = {
        { "help", no_argument, NULL, 'h' },
        { "line-number", no_argument, NULL, 'n' },
        { "with-filename", no_argument, NULL, 'f' },
        { "no-filename", no_argument, NULL, 'F' },
        { "recursive", no_argument, NULL, 'r' },
        { "pattern", required_argument, NULL, 'e' },
        {0, 0, 0, 0}
    };

    while (1) {
        int optidx = 0;
        int c;

        c = getopt_long(argc, argv, "+hnfFre:", opts, &optidx);

        if (c == -1)
            break;

        switch (c) {
        case 'h':
            printUsage(argv[0]);
            exit(EXIT_SUCCESS);

        case 'n':
            linenumber = 1;
            break;

        case 'f':
            filename = 1;
            break;

        case 'F':
            filename = 2;
            break;

        case 'r':
            recursive = 1;
            break;

        case 'e':
            patterns = g_slist_prepend(patterns, optarg);
            break;

        case '?':
        default:
            printUsageShort(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* Next one is PATTERN. */
    if (!patterns) {
        if (optind < argc) {
            patterns = g_slist_prepend(patterns, argv[optind++]);
        } else {
            /* But if none found then it's clearly usage error. */
            printUsageShort(argv[0]);
            goto cleanup;
        }
    }

    grep = GrepInit(recursive, (const char **) argv + optind, argc - optind);
    if (!grep)
        goto cleanup;

    data.grep = grep;
    data.linenumber = linenumber;
    data.filename = filename;
    data.error = 0;

    g_slist_foreach(patterns, grepDoHelper, &data);

    if (data.error != 0)
        goto cleanup;

    ret = EXIT_SUCCESS;
 cleanup:
    GrepFree(grep);
    g_slist_free(patterns);
    return ret;
}
