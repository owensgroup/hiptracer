#ifndef __HIP_TRACE_H__
#define __HIP_TRACE_H__

#include <vector>

typedef enum HIP_EVENT {
    EVENT_UNDEFINED = 0,
    EVENT_DEVICE,
    EVENT_MEM,
    EVENT_LAUNCH
} HIP_EVENT;

typedef struct event_t {
    uint64_t id;
    const char* name;

    HIP_EVENT type;

    uint64_t offset;
    uint64_t size;
} event_t;

typedef struct header_t {
    uint64_t magic;
    uint64_t num_events;
} header_t; 

typedef struct data_t {
    std::vector<std::byte> bytes;
    uint64_t size;
} data_t;

#endif // __HIP_TRACE_H__
