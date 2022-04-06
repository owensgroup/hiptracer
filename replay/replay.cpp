#include <cstdio>
#include <iostream>

// header_t, event_t
#include "trace.h"

/*
void read_event_list(std::FILE* fp, std::vector<event_t> &events)
{
    events.reserve()
    std::fread(events.data(), sizeof(events[0]), events.size(), fp);
}


void read_event(std::FILE* fp, event_t* event)
{
    std::fread(&e, sizeof(event_t), 1, fp);
}
*/

int main(int argc, char** argv)
{
    // Open trace file
    std::FILE* fp = std::fopen(argv[1], "rb");
    
    if (fp == nullptr) {
        std::printf("Unable to open file\n");
    }

    header_t header;
    std::fread(&header, sizeof(header), 1, fp);

    std::printf("magic: %x\n", header.magic);

    // Iterate over events 
    /*
    for (int i = 0; i < num_events; i++)
    {
        // Print event name
        // Begin timing
        // Execute event type
        if (event.type == EVENT_DEVICE) {

        } else if (event.type == EVENT_MEM) {

        } else if (event.type == EVENT_LAUNCH) {

        } else {
            // Unhandled event type
        }
    }
    */

    return 0;
}
