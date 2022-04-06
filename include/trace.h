#ifndef __HIP_TRACE_H__
#define __HIP_TRACE_H__

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

    uint64_t size;
    uint8_t data[1];
} event_t;

typedef struct header_t {
    uint64_t magic;

    uint64_t num_events;
    event_t events[1];
} header_t; 

#endif // __HIP_TRACE_H__
