#ifndef MULTIMEDIA_SERVICE_CAM_UTILS_H
#define MULTIMEDIA_SERVICE_CAM_UTILS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned long long cm_tick_milli(void);
int cm_read_file_data(const char *file, char **data, int *len);

#ifdef __cplusplus
}
#endif

#endif
