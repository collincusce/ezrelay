// ezrelay.h
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
#include <fcntl.h>
#include <functional>
#ifndef _EZRELAY_H
#define _EZRELAY_H


class EZRelay {

private:
	std::string relay_hostname;
	int comms_port, backlog_size, comms_socket;
	bool verbose;
	int ezpipe[2]; //used for splice() to pipe, created on instantiation.
	//These can be broken out into their own class definition for client tracking
	//Left this as-is for simplicity sake

	std::unordered_map<int, int> client_listeners; //maps port to client listeners
	std::unordered_map<int, int> listener_client; //maps client listeners to port
	std::unordered_map<int, int> client_socket; //maps port to client connection to relay
	std::unordered_map<int, int> socket_ports; //maps any socket to its corresponding client port
	std::unordered_map<int, int> socket_requests; //maps sockets to their corresponding connected socket 
	std::unordered_map<int, int> listener_newrequests; //temporary for new requests, maps listener for request to socket to connect with
	std::unordered_map<int, int> listener_nr_ports; //temporary for new requests, maps listener for request to port of client
	std::vector<pollfd> poll_sockets;
	std::vector<int> client_ports;
	std::unordered_map<int, bool> close_queue; //items to be closed along with bool indicating if it has been close already
	bool is_listening;

	int getPortFromSocket(int sockid);
	std::string getAddressFromSocket(int sockid);
	bool isConnected(int sockid);

	int createListener(int portnum, int blsize);
	
	void addPollSocket(int sockid);
	void removePollSocket(int sockid);

	void registerRequest(int new_listener);
	
	void addToCloseQueue(int sockid);
	void processCloseQueue();

	void runHandler(pollfd tmp_pfd);

	void acceptClient();
	int addClientListener();
	void removeClientListener(int sockid);

	void acceptRequest(int sockid);
	bool forwardRequest(int from_socket, int to_socket);

	void closeConnection(int sockid);

	void doPoll(std::vector<pollfd> pollers, int timeout, std::function<void(pollfd)> callback);

public:
	//constructor
	EZRelay();

	//relay's hostname to connect through
	void setRelayHostname(std::string hn);
	std::string getRelayHostname();

	//port to connect to relay over
	void setCommsPort(int portnum);
	int getCommsPort();

	//sets the backlog size for sockets
	void setBacklogSize(int size);

	//sets printing of debug info
	void setVerboseOutput(bool verbose_enabled);

	//listens for new clients
	void listen();
	//stops listening for new clients
	void stopListening();

	//each call to run() will go through the process of checking for messages
	//should be executed in a loop to poll for messages
	void run(int timeout);

//HELPERS
	//readLine() takes a socket and reads buffer until it finds a newline
	//returns entire set of buffers found to newline and extra data after newline
	bool readLine(int sockid, std::string &line);
	//sends a string to the socket passed
	void sendString(int sockid, std::string sendData);
};

#endif // EZRELAY.h