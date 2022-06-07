#ifndef __HIP_TRACE_H__
#define __HIP_TRACE_H__

#include <vector>
#include <string>
#include <cstddef>

typedef enum HIP_EVENT {
    EVENT_UNDEFINED = 0,
    EVENT_DEVICE,
    EVENT_MALLOC,
    EVENT_MEMCPY,
    EVENT_FREE,
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

class Trace {
private:
    std::FILE* events_fp = nullptr;
public:
    std::FILE* data_fp = nullptr;
    header_t header;
    std::vector<event_t> events;

    std::vector<data_t> chunks;

    enum OPEN_STATUS { 
        OPEN_SUCCESS = 1,
        OPEN_FAILURE = 0,
    };
    OPEN_STATUS open(std::string filename) { 
        std::string events_filename = filename + ".events";
        std::string data_filename = filename + ".data";
        events_fp = std::fopen(events_filename.c_str(), "rb");
        data_fp = std::fopen(data_filename.c_str(), "rb");
    
        if (events_fp == nullptr) {
            std::fprintf(stderr, "Unable to open events file\n");
            return OPEN_FAILURE;
        }
        if (data_fp == nullptr) {
            std::fprintf(stderr, "Unable to open data file\n");
            return OPEN_FAILURE;
        }

        // Read file header to verify
        std::fread(&header, sizeof(header_t), 1, events_fp);
        if (header.magic != 0xDEADBEEF) {
            std::fprintf(stderr, "Invalid or corrupt trace file\n");
            return OPEN_FAILURE;
        }

        std::printf("magic: %lx\n", header.magic);
        std::printf("num events: %lu\n", header.num_events); 

        events.resize(header.num_events);
        std::fread(events.data(), sizeof(events[0]), header.num_events, events_fp);

        return OPEN_SUCCESS;
    }

    event_t event(uint64_t event_idx) {

    }

};

#endif // __HIP_TRACE_H__
