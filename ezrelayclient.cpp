#include "ezrelayclient.h"
#include "logger.h"

#define DEFAULT_PORT 8000
#define RCVBUFSIZE 32

EZRelayClient::EZRelayClient() {
	relay_port = DEFAULT_PORT;
	relay_hostname = "localhost";
	verbose = false;
	if (pipe(ezpipe) == -1) {
		Log(Log::err, 1) << "Critical Error: Unable to create pipe, unable to start relay.\n";
		exit(1);
	}
}

int EZRelayClient::getPortFromSocket(int sockid) {
	struct sockaddr_in sin;
	socklen_t len = sizeof(sin);
	if (getsockname(sockid, (struct sockaddr *)&sin, &len) != -1) {
	    return ntohs(sin.sin_port);
	} else {
	    return 0;
	}
}

std::string EZRelayClient::getAddressFromSocket(int sockid) {
	struct sockaddr_in someaddr;
	socklen_t len;
	len = sizeof(someaddr);
	getpeername(sockid, (struct sockaddr*)&someaddr, &len);
	return(std::string(inet_ntoa(someaddr.sin_addr)));
}

bool EZRelayClient::isConnected(int sockid) {
	int optval;
	socklen_t optlen = sizeof(optval);
	int res = getsockopt(sockid, SOL_SOCKET, SO_ERROR, &optval, &optlen);
	if(optval==0 && res==0) {
		return true;
	}
	return false;
}

//Adds socket to the list of sockets to be polled
void EZRelayClient::addPollSocket(int sockid) {
	struct pollfd new_pfd;
	new_pfd.fd = sockid;
	new_pfd.events = POLLIN;
	poll_sockets.push_back(new_pfd);
	Log(Log::dbg, verbose) << "Added poll_socket: " << std::to_string(sockid) << "\n";
}

//Remove socket from the list of sockets to be polled
void EZRelayClient::removePollSocket(int sockid) {
	Log(Log::dbg, verbose) << "Removing poll_socket: " << std::to_string(sockid) << "\n";
	poll_sockets.erase(std::remove_if(poll_sockets.begin(), poll_sockets.end(), [&](pollfd const& v) { return (v.fd == sockid); }), poll_sockets.end());
	auto iter = std::find_if(poll_sockets.begin(), poll_sockets.end(), [&](const pollfd& pf){return pf.fd == sockid;});
	std::string is_removed = (iter == poll_sockets.end() ? "YES" : "NO");
	Log(Log::dbg, verbose) << "Is poll_socket " << std::to_string(sockid) <<  " removed? -- " << is_removed << "\n";
}

void EZRelayClient::addToCloseQueue(int sockid) {
	if(close_queue.count(sockid) == 0) {
		close_queue[sockid] = false;
		Log(Log::dbg, verbose) << "Added socket to close_queue: " << std::to_string(sockid) << "\n";
	}
}

void EZRelayClient::processCloseQueue() {
	for(std::pair<int, bool> element : close_queue){
		int sockid = element.first;
		if(close_queue[sockid] == true){
			//skip sockets already closed
			continue;
		}
		closeConnection(sockid);
	}
	close_queue.clear();
}

void EZRelayClient::runHandler(pollfd tmp_pfd, std::function<void(int, int *)> callback) {
	int from_fd = tmp_pfd.fd;
	if (tmp_pfd.revents & POLLIN) {
		if(from_fd == comms_socket) {
			//handle request from relay
			int newport = 0;
			std::string line;
			bool res = readLine(comms_socket, line);
			std::stringstream ss;
			ss << line;
			ss >> newport;
			if(newport){
				Log(Log::dbg, verbose) << "Recieved port: " << std::to_string(newport) << '\n';
				int newcon = connectToAddress(relay_hostname, newport);
				Log(Log::dbg, verbose) << "Created new connection: " << std::to_string(newcon) << '\n';
				addPollSocket(newcon);
			}
		} else {
			//handle all other requests
			callback(from_fd, ezpipe);
		}
	} else if(tmp_pfd.revents & POLLHUP || tmp_pfd.revents & POLLERR || tmp_pfd.revents & POLLNVAL){
		addToCloseQueue(tmp_pfd.fd);
		if(tmp_pfd.fd == comms_socket) {
			Log(Log::err, verbose) << "ERROR ON MAIN RELAY SOCKET, EXIT!" << '\n';
			exit(1);
		}
	}
}

//Creates new socket based on address info from parameter socket to port provided
//Returns socket
int EZRelayClient::connectToAddress(const std::string &address, int port) { 
	char ipstr[address.size()+1]; 
	strcpy(ipstr, address.c_str());

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // use IPv4 or IPv6, whichever
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // fill in my IP for me
	getaddrinfo(ipstr, std::to_string(port).c_str(), &hints, &res);
	int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	int enable = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		throw "setsockopt(SO_REUSEADDR) failed in createListener";
	}
	//fcntl(s, F_SETFL, O_NONBLOCK); //Stops blocking on connection
	connect(s, res->ai_addr, res->ai_addrlen);
	return s;
}

//Just good practice to wrap this in case I need clean up.
void EZRelayClient::closeConnection(int sockid) {
	if(close_queue.count(sockid) > 0) {
		if(!close_queue[sockid]){
			Log(Log::dbg, verbose) << "Closing connection: " << std::to_string(sockid) << "\n";
			shutdown(sockid, SHUT_RDWR);
			close(sockid);
			close_queue[sockid] = true;
			removePollSocket(sockid);
		}
	}
}

void EZRelayClient::doPoll(std::vector<pollfd> pollers, int timeout, std::function<void(pollfd)> callback){
	int pollers_len = pollers.size();
	pollfd *pollers_ptr;
	pollers_ptr = &pollers[0];
	if(pollers_len > 0) {

		int poll_reads = poll(pollers_ptr, pollers_len, timeout);
		if (poll_reads > 0){
			for(int i = 0; i<pollers_len;i++){
				callback(pollers[i]);
			}
		}
	}
}

bool EZRelayClient::readLine(int sockid, std::string &line) {
	char buffer[1024];
	ssize_t len;
	while ((len = recv(sockid, buffer, sizeof(buffer), 0)) > 0) {
		line.append(buffer, len);
		if(std::find(buffer, buffer + len, '\n') <= buffer + len) {
			// found the \n character!
			break;
		}
	}
	return (len > 0);
}

void EZRelayClient::sendString(int sockid, std::string sendData) {
	Log(Log::dbg, verbose) << "sending: " << sendData << '\n';
	send(sockid, sendData.data(), sendData.size(), 0);
	Log(Log::dbg, verbose) << "sent: " << sendData << '\n';
}

//Sets public hostname for connections.
//Used to return data to the client to notify connections how to access the relay.
void EZRelayClient::setRelayHostname(std::string hn) {
	relay_hostname = hn;
}

std::string EZRelayClient::getRelayHostname() {
	return relay_hostname;
}

//Sets the communication port for EZRelay
//Stops listening for clients if called, listen() must be invoked again.
void EZRelayClient::setRelayPort(int portnum) {
	relay_port = portnum;
}

int EZRelayClient::getRelayPort() {
	return relay_port;
}

void EZRelayClient::setVerboseOutput(bool verbose_enabled){
	verbose = verbose_enabled;
}

//Takes a function, runs it unless socket polled is the relay socket
bool EZRelayClient::run(int timeout, std::function<void(int, int *)> callback) {
	if(!isConnected(comms_socket)){
		return false;
	}
	auto cb = [&](pollfd tmp_pfd) {
		runHandler(tmp_pfd, callback);
	};
	processCloseQueue();
	doPoll(poll_sockets, timeout, cb);
	return true;
}

//Opens a connection to a relay at a port num
//Returns the socket connected to the relay for your client
int EZRelayClient::requestRelay() {
	comms_socket = connectToAddress(relay_hostname, relay_port);
	addPollSocket(comms_socket);
	return comms_socket;
}

//TODO: closeRelay()