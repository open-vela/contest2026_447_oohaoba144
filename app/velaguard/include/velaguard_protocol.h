#ifndef VELAGUARD_PROTOCOL_H
#define VELAGUARD_PROTOCOL_H

#include <stddef.h>

#include "velaguard_core.h"

#define VG_PROTOCOL_VERSION 1
#define VG_PROTOCOL_MAX_LINE_BYTES 1024

vg_result_t vg_protocol_parse_create(const char *line, size_t length,
                                     vg_create_request_t *out);

#endif
