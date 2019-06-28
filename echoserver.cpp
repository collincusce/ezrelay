#include "ezrelayclient.h"
#include <exception>
#include <stdexcept>
#include <string>
#include <iostream>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <sys/types.h>
#include <sys/socket.h>

#include <fstream>

void usage() {
	std::cout << "Echo's back any information recieved through relay." << std::endl;
	std::cout << "Usage: ./echoserver -n <relay hostname:string> -p <relay port:integer>" << std::endl;
	std::cout << "Optional arguments:" << std::endl;
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

void echo(int sockid, int echopipe[2]) {
	char buffer[4096];
	ssize_t len;
	try {
		len = splice(sockid, NULL, echopipe[1], NULL, sizeof(buffer), SPLICE_F_MOVE);
	} catch(...) {
		std::throw_with_nested(
			std::runtime_error("echoserver::echo: Error produced in receiving splice(" + std::to_string(sockid) + ", buffer, " + std::to_string(len) + ", 0).")
		);
	}
	if(len > 0) {
		ssize_t sent;
		try {
			sent = splice(echopipe[0], NULL, sockid, NULL, len, SPLICE_F_MOVE);
		} catch(...) {
			std::throw_with_nested(
				std::runtime_error("echoserver::echo: Error produced in sending splice(" + std::to_string(sockid) + ", buffer, " + std::to_string(sent) + ", 0).")
			);
		}
	}
}

int main(int argc, char *argv[]) {
	std::size_t posp, pose;
	std::string hostname = "";
	int port = -1;
	int verbose = false;
	int c;
	while ((c = getopt (argc, argv, "p:n:hv")) != -1) {
    	switch (c) {
			case 'p':
				port = std::stoi(optarg, &posp);
				break;
			case 'n':
				hostname = optarg;
				break;
			case 'v':
				verbose = true;
				break;
			case 'h':
				usage();
				return 1;
			case '?':
				if (optopt == 'p' ||  optopt == 'n') {
					fprintf (stderr, "Option -%c requires an argument.\n", optopt);
				}
				else if (isprint (optopt)) {
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				}
				else {
					fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
				}
				usage();
				return 1;
			default:
				abort ();
		}
	}

	EZRelayClient relayclient;

	if(port == -1) {
		std::cout << "Missing Argument: Relay port required" << std::endl;
		usage();
		return 1;
	}
	if(port < 1001 || port > 65535) {
		std::cout << "Invalid port (1001-65535): " << port << std::endl;
		usage();
		return 1;
	}
	if(hostname == "") {
		std::cout << "Missing Argument: Relay hostname required" << std::endl;
		usage();
		return 1;
	}
	if(verbose) {
		relayclient.setVerboseOutput(true);
	}
	relayclient.setRelayHostname(hostname);
	relayclient.setRelayPort(port);
	int relaysocket = relayclient.requestRelay();
	std::string relayInfo; 
	bool result = relayclient.readLine(relaysocket, relayInfo);
	std::cout << "established relay address: " << relayInfo << std::endl;
	try {
		while(relayclient.run(10000, echo)) {
			continue;
		}
	} catch (const std::exception& e) {
		print_exception(e);
        return 1;
	}

}