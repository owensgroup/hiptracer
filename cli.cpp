#include "subprocess.hpp"
#include "cmdline.h"

#include <cstdlib>

int main(int argc, char *argv[])
{
	cmdline::parser parser;

	parser.add<std::string>("library-location",  // Long name
                            'l',  // Short name
                            "location of libhipcapture library",  // Description (help)
                            false, // Required?
                            "./libhipcapture.so"); // Default value
	parser.add<std::string>("output", 'o', "output capture name", false, "./tracer-default.db");
	parser.add<std::string>("debug", 'd', "debug mode", false, "false");
	parser.add<std::string>("progressbar", 'p', "enable progress bar", false, "true");

	parser.parse_check(argc, argv);

	int sb = subprocess::Popen(parser.rest(), 
		subprocess::environment {{ {"LD_PRELOAD", parser.get<std::string>("library-location")},
								   {"HIPTRACER_DEBUG", parser.get<std::string>("debug")},
								   {"HIPTRACER_OUTPUT", parser.get<std::string>("output")},
                                   {"HIPTRACER_PROGRESS", parser.get<std::string>("progressbar")} }},
		subprocess::output{{stdout}}).wait();

	return 0;
}
