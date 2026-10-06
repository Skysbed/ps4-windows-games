/*
 * Second translation unit used to prove that the layer handles a project,
 * not only one source file.
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>

void pcps4_runtime_demo(char *buffer, size_t buffer_size)
{
    const char message[] = "hello from PC source";

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    ZeroMemory(buffer, buffer_size);
    CopyMemory(buffer, message,
               (strlen(message) + 1 < buffer_size)
                   ? strlen(message) + 1
                   : buffer_size - 1);
}
