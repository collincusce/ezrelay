#include "ezrelay.h"
#include <exception>
#include <stdexcept>
#include <string>
#include <iostream>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void usage() {
	std::cout << "Behaves as a TCP relay for applications." << std::endl;
	std::cout << "Usage: ./relay" << std::endl;
	std::cout << "Optional arguments:" << std::endl;
	std::cout << "    -p <port:integer> -- port for the relay -- default value is 8000" << std::endl;
	std::cout << "    -n <hostname:string> -- hostname for the relay -- default value is 'localhost'" << std::endl;
	std::cout << "    -b <tcpbacklog:integer> -- backlog for tcp connections -- default value is 10" << std::endl;
	std::cout << "    -v -- prints debug and error information." << std::endl;
	std::cout << "    -h -- prints this usage information" << std::endl;
}

void print_exception(const std::exception& e, int level =  0) {
    std::cerr << std::string(level, ' ') << "exception: " << e.what() << '\n';
    try {
        std::rethrow_if_nested(e);
    } catch(const std::exception& e) {
        print_exception(e, level+1);
    } catch(...) {}
}

int main(int argc, char *argv[]) {
	std::size_t posp, posb;
	std::string hostname = "";
	int port = -1;
	int backlog = -1;
	int verbose = false;
	int c;
	while ((c = getopt (argc, argv, "p:n:b:hv")) != -1) {
    	switch (c) {
			case 'p':
				port = std::stoi(optarg, &posp);
				break;
			case 'n':
				hostname = optarg;
				break;
			case 'b':
				backlog = std::stoi(optarg, &posb);
				break;
			case 'v':
				verbose = true;
				break;
			case 'h':
				usage();
				return 1;
			case '?':
				if (optopt == 'b' || optopt == 'p' || optopt == 'n') {
					fprintf (stderr, "Option -%c requires an argument\n", optopt);
				}
				else if (isprint (optopt)) {
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				}
				else {
					fprintf (stderr, "Unknown option character `\\x%x'\n", optopt);
				}
				usage();
				return 1;
			default:
				abort ();
		}
	}
	EZRelay relay;

	if(port != -1) {
		if(port < 1001 || port > 65535) {
			std::cout << "Invalid port (1001-65535): " << port << std::endl;
			usage();
			return 1;
		} else {
			relay.setCommsPort(port);
		}
	}
	if(hostname != "") {
		relay.setRelayHostname(std::string(hostname));
	}
	if(backlog != -1) {
		if(backlog < 1 || backlog > 1023) {
			std::cout << "Invalid backlog (1-1023): " << backlog << std::endl;
			usage();
			return 1;
		} else {
			relay.setBacklogSize(backlog);
		}
	}
	if(verbose) {
		relay.setVerboseOutput(true);
	}
	try {
		relay.listen();
	} catch (const std::exception& e) {
		print_exception(e);
        return 1;
	}
	try {
		relay.run(5000);
		std::cout << "Relay operational, begin connecting to " << relay.getRelayHostname() << ":" << std::to_string(relay.getCommsPort()) << std::endl;
		while(1) {
			relay.run(10000);
		}
	} catch (const std::exception& e) {
		print_exception(e);
        return 1;
	}
	
}