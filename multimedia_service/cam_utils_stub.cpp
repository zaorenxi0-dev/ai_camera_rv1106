#include "cam_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

unsigned long long cm_tick_milli(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)tv.tv_usec / 1000ULL;
}

int cm_read_file_data(const char *file, char **data, int *len)
{
    if (!file || !data || !len)
    {
        return -1;
    }

    FILE *fp = fopen(file, "rb");
    if (!fp)
    {
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }

    long size = ftell(fp);
    if (size < 0)
    {
        fclose(fp);
        return -1;
    }
    rewind(fp);

    char *buffer = static_cast<char *>(malloc((size_t)size + 1));
    if (!buffer)
    {
        fclose(fp);
        return -1;
    }

    size_t read_size = fread(buffer, 1, (size_t)size, fp);
    fclose(fp);
    buffer[read_size] = '\0';

    *data = buffer;
    *len  = (int)read_size;
    return 0;
}
