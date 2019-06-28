// ezrelayclient.h
#include <string>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <errno.h>
#include <exception>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <iostream>
#include <functional>
#include <sstream>
#ifndef _EZRELAYCLIENT_H
#define _EZRELAYCLIENT_H


class EZRelayClient {

private:
	std::string relay_hostname;
	int relay_port, comms_socket;
	bool verbose;
	int ezpipe[2]; //used for splice() to pipe, created on instantiation.

	std::vector<pollfd> poll_sockets;
	std::unordered_map<int, bool> close_queue; //items to be closed along with bool indicating if it has been close already

	int getPortFromSocket(int sockid);
	std::string getAddressFromSocket(int sockid);
	bool isConnected(int sockid);

	void addPollSocket(int sockid);
	void removePollSocket(int sockid);

	void addToCloseQueue(int sockid);
	void processCloseQueue();

	void runHandler(pollfd tmp_pfd, std::function<void(int, int *)> callback);

	int connectToAddress(const std::string &address, int sockid);
	void closeConnection(int sockid);

	void doPoll(std::vector<pollfd> pollers, int timeout, std::function<void(pollfd)> callback);

public:
	//constructor
	EZRelayClient();

	//relay's hostname to connect through
	void setRelayHostname(std::string hn);
	std::string getRelayHostname();

	//port to connect to relay over
	void setRelayPort(int portnum);
	int getRelayPort();

	//sets printing of debug info
	void setVerboseOutput(bool verbose_enabled);

	//each call to run() will go through the process of checking for messages
	//should be executed in a loop to poll for messages
	//each message found calls callback that takes the socket file descriptor and handles the request
	bool run(int timeout, std::function<void(int, int *)> callback);

	//requests a relay at the set hostname and port
	int requestRelay();

//HELPERS
	//readLine() takes a socket and reads buffer until it finds a newline
	//returns entire set of buffers found to newline and extra data after newline
	bool readLine(int sockid, std::string &line);
	//sends a string to the socket passed
	void sendString(int sockid, std::string sendData);
};

#endif // EZRELAYCLIENT.h