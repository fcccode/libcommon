/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ProcessName.hxx"

#include <assert.h>
#include <string.h>
#include <sys/prctl.h>

static struct {
    int argc;
    char **argv;
    size_t max_length;
} process_name;

void
InitProcessName(int argc, char **argv)
{
    assert(process_name.argc == 0);
    assert(process_name.argv == nullptr);
    assert(argc > 0);
    assert(argv != nullptr);
    assert(argv[0] != nullptr);

    process_name.argc = argc;
    process_name.argv = argv;
    process_name.max_length = strlen(argv[0]);
}

void
SetProcessName(const char *name)
{
    prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);

    if (process_name.argc > 0) {
        for (int i = 1; i < process_name.argc; ++i)
            memset(process_name.argv[i], 0, strlen(process_name.argv[i]));

        strncpy(process_name.argv[0], name, process_name.max_length);
    }
}
