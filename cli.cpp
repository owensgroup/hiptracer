#include "subprocess.hpp"

#include <cstdlib>
#include <unistd.h>

int main(int argc, char *argv[])
{
    std::string libraryLocation = "./libhipcapture.so";
    std::string outputName = "./tracer-default.db";
    bool debug = false;
    bool progressbar = true;
    bool skiphostdata = false;
    std::string rest;

    int c;
    while ((c = getopt(argc, argv, "l:so:dp")) != -1) {
        switch (c) {
        case 'l':
           libraryLocation = optarg;
           break;
        case 's':
            skiphostdata = true;
            break;
        case 'o':
            outputName = optarg;
            break;
        case 'd':
            debug = true;
            break;
        case 'p':
            progressbar = false;
            break;
        default:
            return EXIT_FAILURE;
        }
    }
    std::map<std::string, std::string> env = 
                                    {{ {"LD_PRELOAD", libraryLocation},
    								   {"HIPTRACER_EVENTDB", outputName} }};

    std::vector<std::string> tail;
    for (int i = optind; i < argc; i++) {
        tail.push_back(std::string(argv[i]));
    }

    if (debug) {
        env["HIPTRACER_DEBUG"] = "1";
    } else if (skiphostdata) { 
        env["HIPTRACER_SKIPHOSTDATA"] = "1";
    }

    int sb = subprocess::Popen(tail, subprocess::environment{{env}}, subprocess::output{{stdout}}).wait();
	return sb;
}
