#include "ezrelay.h"
#include "logger.h"

#define DEFAULT_PORT 8000
#define DEFAULT_BACKLOG 10
#define RCVBUFSIZE 32
#define FLUSH_LIMIT 10000000

EZRelay::EZRelay() {
	comms_port = DEFAULT_PORT;
	backlog_size = DEFAULT_BACKLOG;
	relay_hostname = "localhost";
	verbose = false;
	is_listening = false;
	if (pipe(ezpipe) == -1) {
		Log(Log::err, 1) << "Critical Error: Unable to create pipe, unable to start relay.\n";
		exit(1);
	}
}

int EZRelay::getPortFromSocket(int sockid) {
	struct sockaddr_in sin;
	socklen_t len = sizeof(sin);
	if (getsockname(sockid, (struct sockaddr *)&sin, &len) != -1) {
	    return ntohs(sin.sin_port);
	} else {
	    return 0;
	}
}

std::string EZRelay::getAddressFromSocket(int sockid) {
	struct sockaddr_in someaddr;
	socklen_t len;
	len = sizeof(someaddr);
	getpeername(sockid, (struct sockaddr*)&someaddr, &len);
	return(std::string(inet_ntoa(someaddr.sin_addr)));
}

//Not the greatest because it doesn't check silent drops, but will work for this case I think.
bool EZRelay::isConnected(int sockid) {
	int optval;
	socklen_t optlen = sizeof(optval);
	int res = getsockopt(sockid, SOL_SOCKET, SO_ERROR, &optval, &optlen);
	if(optval==0 && res==0) {
		return true;
	}
	return false;
}

//Creates a listener at the port specified
//Returns a socket for the listener
int EZRelay::createListener(int portnum, int blsize) {
	addrinfo hints, *res;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; //AF_UNSPEC; // use IPv4 or IPv6, whichever
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // fill in my IP for me
	getaddrinfo(NULL, std::to_string(portnum).c_str(), &hints, &res);
	int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	int enable = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		std::throw_with_nested(
			std::runtime_error("EZRelay::createListener: Error produced in setsockopt(" + std::to_string(s) + ", SOL_SOCKET, SO_REUSEADDR, " + std::to_string(enable) + ").")
		);
	}
	//fcntl(s, F_SETFL, O_NONBLOCK); //Stops blocking on listeners
	bind(s, res->ai_addr, res->ai_addrlen); // -1 on good, errno on bad
	::listen(s, blsize); // -1 on good, errno on bad
	return s;
}

//Adds socket to the list of sockets to be polled
void EZRelay::addPollSocket(int sockid) {
	struct pollfd new_pfd;
	new_pfd.fd = sockid;
	new_pfd.events = POLLIN;
	poll_sockets.push_back(new_pfd);
	Log(Log::dbg, verbose) << "Added poll_socket: " << std::to_string(sockid) << "\n";
}

//Remove socket from the list of sockets to be polled
void EZRelay::removePollSocket(int sockid) {
	Log(Log::dbg, verbose) << "Removing poll_socket: " << std::to_string(sockid) << "\n";
	poll_sockets.erase(std::remove_if(poll_sockets.begin(), poll_sockets.end(), [&](pollfd const& v) { return (v.fd == sockid); }), poll_sockets.end());
	auto iter = std::find_if(poll_sockets.begin(), poll_sockets.end(), [&](const pollfd& pf){return pf.fd == sockid;});
	std::string is_removed = (iter == poll_sockets.end() ? "YES" : "NO");
	Log(Log::dbg, verbose) << "Is poll_socket " << std::to_string(sockid) <<  " removed? -- " << is_removed << "\n";
}

//Registers request with system
void EZRelay::registerRequest(int new_listener) {
	Log(Log::dbg, verbose) << "Entering registerRequest" << '\n';
	Log(Log::dbg, verbose) << "using listener: " << std::to_string(new_listener) << '\n';
	int portnum = listener_nr_ports[new_listener];
	int newrequest = listener_newrequests[new_listener];
	struct sockaddr_storage their_addr;
	socklen_t addr_size;
	int cli_receiver = -1;
	try {
		cli_receiver = accept(new_listener, (struct sockaddr *)&their_addr, &addr_size);
		int optval = 1;
		setsockopt(new_listener, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
	} catch(...) {
		std::throw_with_nested(
			std::runtime_error("EZRelay::registerRequest: Error produced in accept( " + std::to_string(new_listener) +  ", their_addr, addr_size).")
		);
	} 
	if(cli_receiver != -1) {
		Log(Log::dbg, verbose) << "accepted cli_socket: " << std::to_string(cli_receiver) << '\n';
		socket_ports[portnum] = newrequest;
		socket_ports[portnum] = cli_receiver;
		socket_requests[cli_receiver] = newrequest;
		socket_requests[newrequest] = cli_receiver;
		addPollSocket(newrequest);
		addPollSocket(cli_receiver);
		addToCloseQueue(new_listener);
		closeConnection(new_listener);
		listener_newrequests.erase(new_listener);
		listener_nr_ports.erase(new_listener);
	} else {
		try {
			std::throw_with_nested(
				std::runtime_error("error on accept() which returned cli_receiver of " + std::to_string(cli_receiver) + '\n')
			);
		} catch(...) {
			std::throw_with_nested(
				std::runtime_error("EZRelay::registerRequest: Error produced in accept( " + std::to_string(new_listener) +  ", their_addr, addr_size).")
			);
		}
		Log(Log::err, verbose) << "registerRequest: accept() returned: " << std::to_string(cli_receiver) << '\n';
	}
	Log(Log::dbg, verbose) << "Leaving registerRequest" << '\n';
}

void EZRelay::addToCloseQueue(int sockid) {
	if(close_queue.count(sockid) == 0) {
		close_queue[sockid] = false;
		Log(Log::dbg, verbose) << "Added socket to close_queue: " << std::to_string(sockid) << "\n";
	}
}

void EZRelay::processCloseQueue() {
	for(std::pair<int, bool> element : close_queue){
		int sockid = element.first;
		Log(Log::dbg, verbose) << "Processing close_queue " << (close_queue[sockid] ? "(true)" : "(false)") << " for socket: " << std::to_string(sockid) << "\n";
		if(close_queue[sockid] == true){
			//skip sockets already closed
			continue;
		}
		if(listener_client.count(sockid) > 0) {
			//is an client listener that needs to close
			removeClientListener(sockid);
		} 
		if(socket_requests.count(sockid) > 0){
			//this is a socket_request that must close
			int to_socket = socket_requests[sockid];
			Log(Log::dbg, verbose) << "Closing socket_requests: " << std::to_string(sockid) << " and " << std::to_string(to_socket) << "\n";
			addToCloseQueue(sockid);
			closeConnection(sockid);
			addToCloseQueue(to_socket);
			closeConnection(to_socket);
		}
		if(listener_newrequests.count(sockid) > 0){
			Log(Log::dbg, verbose) << "Closing listener_newrequests: " << std::to_string(sockid) << " and " << std::to_string(listener_newrequests[sockid]) << "\n";
			addToCloseQueue(listener_newrequests[sockid]);
			closeConnection(listener_newrequests[sockid]);
			addToCloseQueue(sockid);
			closeConnection(sockid);
		}
	}
	close_queue.clear();
}

void EZRelay::runHandler(pollfd tmp_pfd) {
	Log(Log::dbg, verbose) << "reading revent: " << std::to_string(tmp_pfd.revents) << '\n';
	int from_fd = tmp_pfd.fd;
	int to_fd = -1;
	if (tmp_pfd.revents & POLLIN) {
		Log(Log::dbg, verbose) << "in POLLIN with socket: " << tmp_pfd.fd  <<  '\n';
		//can read data here
		if(from_fd == comms_socket) {
			Log(Log::dbg, verbose) << "start comms_socket" << '\n';
			acceptClient();
			Log(Log::dbg, verbose) << "end comms_socket" << '\n';
		} else if(listener_newrequests.count(from_fd) > 0) {
			registerRequest(from_fd);
		} else if(listener_client.count(from_fd) > 0) {
			Log(Log::dbg, verbose) << "start acceptRequest" << '\n';
			acceptRequest(from_fd);
			Log(Log::dbg, verbose) << "end acceptRequest" << '\n';
		} else if(socket_requests.count(from_fd) > 0) {
			Log(Log::dbg, verbose) << "start socket_requests" << '\n';
			to_fd = socket_requests[from_fd];
			Log(Log::dbg, verbose) << "to_fd: " << std::to_string(to_fd) << '\n';
			Log(Log::dbg, verbose) << "from_fd: " << std::to_string(from_fd) << '\n';
			forwardRequest(from_fd, to_fd);
			Log(Log::dbg, verbose) << "end socket_requests" << '\n';
		} else {
			//houston we have a problem
			//skipping for the moment, but should be handled
			Log(Log::err, verbose) << "The polled socket doesn't exist." << '\n';
			addToCloseQueue(from_fd);
		}
	} else if(tmp_pfd.revents & POLLHUP || tmp_pfd.revents & POLLERR || tmp_pfd.revents & POLLNVAL) {
		Log(Log::dbg, verbose) << "Detected closed connection." << '\n';
		if(from_fd == comms_socket) {
			try {
				std::throw_with_nested(
					std::runtime_error("ERROR ON MAIN COMMINICATION SOCKET, EXIT!\n")
				);
			} catch(...) {
				std::throw_with_nested(
					std::runtime_error("EZRelay::runHandler: Communication listening to port " + std::to_string(comms_port) +  " has terminated.")
				);
			}
		} else if(socket_requests.count(from_fd) > 0) {
			to_fd = socket_requests[from_fd];
			Log(Log::dbg, verbose) << "Connection closed, flushing." << '\n';
			int flush_limit = FLUSH_LIMIT;
			while(forwardRequest(from_fd, to_fd) && flush_limit > 0) {
				flush_limit--;
				continue;
			}
			Log(Log::dbg, verbose) << "Flushed" << '\n';
		}
		addToCloseQueue(from_fd);
	}
}

//While listening for new client requests
//Adds new clients to the relay
void EZRelay::acceptClient() {
	if(is_listening) {
		struct sockaddr_storage their_addr;
		socklen_t addr_size;
		int newsocket; 
		try {
			newsocket = accept(comms_socket, (struct sockaddr *)&their_addr, &addr_size); 
			int optval = 1;
			//setsockopt(new_listener, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
		} catch(...) {
			std::throw_with_nested( 
				std::runtime_error("EZRelay::acceptClient: Error produced in accept(" + std::to_string(comms_socket) + ", their_addr, addr_size).")
			);
		}
		if(newsocket != -1) {
			int cli_listener = addClientListener();
			int portnum = listener_client[cli_listener];
			std::string sendData = relay_hostname + ":" + std::to_string(portnum) + "\n";
			sendString(newsocket, sendData);
			client_socket[portnum] = newsocket;
		}
	}
}

//Adds an client to the client pool. 
//Allocates a port and listener for the client.
//Returns socket for the client. 
int EZRelay::addClientListener() {
	//Binding to port 0 will return a random open port
	//Not a big fan of selecting random ports, but leaving that as a TODO
	int sockid = createListener(0, backlog_size);
	int portnum = getPortFromSocket(sockid);
	client_ports.push_back(portnum);
	client_listeners[portnum] = sockid;
	listener_client[sockid] = portnum;
	addPollSocket(sockid);
	return sockid;
}

//Closes socket connection at the port specified.
//Removes an client from the client pool.
void EZRelay::removeClientListener(int cli_listener) {
	int portnum = listener_client[cli_listener];
	addToCloseQueue(cli_listener);
	closeConnection(cli_listener);
	listener_client.erase(cli_listener);
	client_listeners.erase(portnum);
	std::unordered_map<int, int>::iterator it = socket_requests.begin();
	while (it != socket_requests.end()) {
		if (socket_ports[it->first] == portnum) {
			socket_ports.erase(it->first);
			addToCloseQueue(it->first);
			closeConnection(it->first);
			it = socket_requests.erase(it);
		} else {
			it++;
		}
	}
	addToCloseQueue(client_socket[portnum]);
	closeConnection(client_socket[portnum]);
	client_socket.erase(portnum);
	client_ports.erase(std::remove(client_ports.begin(), client_ports.end(), portnum), client_ports.end());
}

//Accepts requests for an client open at listener socket sent
void EZRelay::acceptRequest(int sockid) {
	int portnum = listener_client[sockid];
	Log(Log::dbg, verbose) << "acceptRequest: portnum for client = " << std::to_string(portnum) << '\n'; 
	struct sockaddr_storage their_addr;
	socklen_t addr_size;
	Log(Log::dbg, verbose) << "accepting newrequest" << '\n';
	int newrequest; 
	try {
		newrequest = accept(sockid, (struct sockaddr *)&their_addr, &addr_size);
		int optval = 1;
		setsockopt(newrequest, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
	} catch(...) {
		std::throw_with_nested( 
			std::runtime_error("EZRelay::acceptRequest: Error produced in accept(" + std::to_string(sockid) + ", their_addr, addr_size).")
		);
	}
	Log(Log::dbg, verbose) << "accepted newrequest" << '\n'; 
	if(newrequest != -1){
		int cli_socket = client_socket[portnum];
		Log(Log::dbg, verbose) << "accepting cli_socket" << '\n';

		int newcon_listener = createListener(0, backlog_size);
		int newcon_port = getPortFromSocket(newcon_listener);
		addPollSocket(newcon_listener);
		listener_newrequests[newcon_listener] = newrequest;
		listener_nr_ports[newcon_listener] = portnum;
		//tell the client to open a new connection for this request
		std::string cmd = std::to_string(newcon_port) + "\n";
		sendString(cli_socket, cmd);
		Log(Log::dbg, verbose) << "sent OPEN " <<  std::to_string(newcon_port) << '\n';
	} else {
		Log(Log::err, verbose) << "New request rejected." << '\n';
	}
}

bool EZRelay::forwardRequest(int from_socket, int to_socket) { 
	Log(Log::dbg, verbose) << "forwarding" << '\n';
	char buffer[4096];
	ssize_t len;
	try {
		len = splice(from_socket, NULL, ezpipe[1], NULL, sizeof(buffer), SPLICE_F_MOVE);
	} catch(...) {
		std::throw_with_nested(
				std::runtime_error("EZRelay::forwardRequest: Error produced in receiving splice(" + std::to_string(to_socket) + ", buffer, " + std::to_string(len) + ", 0).")
			);
	}
	if(len > 0) {
		ssize_t sent;
		try {
			sent = splice(ezpipe[0], NULL, to_socket, NULL, len, SPLICE_F_MOVE);
		} catch(...) {
			std::throw_with_nested(
				std::runtime_error("EZRelay::forwardRequest: Error produced in sending splice(" + std::to_string(to_socket) + ", buffer, " + std::to_string(sent) + ", 0).")
			);
		}
		return true;
	} else {
		Log(Log::dbg, verbose) << "forwardRequest, recv, len: " << std::to_string(len) << '\n';
		addToCloseQueue(from_socket);
		addToCloseQueue(to_socket);
		return false;
	}
}

void EZRelay::closeConnection(int sockid) {
	if(close_queue.count(sockid) > 0) {
		if(!close_queue[sockid]){
			Log(Log::dbg, verbose) << "Closing connection: " << std::to_string(sockid) << "\n";
			try {
				shutdown(sockid, SHUT_RDWR);
				close(sockid);
			} catch(...) {
				std::throw_with_nested(
					std::runtime_error("EZRelay::closeConnection: Error produced in shutdown and close of socket #" + std::to_string(sockid) + ".")
				);
			}
			close_queue[sockid] = true;
			removePollSocket(sockid);
		}
	}
}

void EZRelay::doPoll(std::vector<pollfd> pollers, int timeout, std::function<void(pollfd)> callback){
	int pollers_len = pollers.size();
	pollfd *pollers_ptr;
	pollers_ptr = &pollers[0];
	if(pollers_len > 0) {
		Log(Log::dbg, verbose) << "calling poll(), poll 0 is comms_socket? " << ((pollers[0].fd == comms_socket) ? "YES" : "NO") << '\n';
		int poll_reads = 0;
		try {
			poll_reads = poll(pollers_ptr, pollers_len, timeout);
		} catch(...) {
			std::throw_with_nested( 
				std::runtime_error("EZRelay::doPoll: Error produced in poll(pollers_ptr, " + std::to_string(pollers_len) + ", " + std::to_string(timeout) + ").")
			);
		}
		Log(Log::dbg, verbose) << "poll reads: " << std::to_string(poll_reads) << '\n';
		if (poll_reads > 0){
			Log(Log::dbg, verbose) << "past poll reads check." << '\n';
			Log(Log::dbg, verbose) << "pollers_len: " << std::to_string(pollers_len) << '\n';
			for(int i = 0; i<pollers_len;i++){
				Log(Log::dbg, verbose) << "calling poll callback" << '\n';
				try {
					callback(pollers[i]);
				} catch(...) {
					std::throw_with_nested(
						std::runtime_error("EZRelay::doPoll: Error produced in callback(pollers[" + std::to_string(i) + "]).")
					);
				}
			}
		}
	}
}

//Sets public hostname for connections.
//Used to return data to the client to notify connections how to access the relay.
void EZRelay::setRelayHostname(std::string hn) {
	relay_hostname = hn;
}

std::string EZRelay::getRelayHostname() {
	return relay_hostname;
}

//Sets the communication port for EZRelay
//Stops listening for clients if called, listen() must be invoked again.
void EZRelay::setCommsPort(int portnum) {
	if(is_listening) {
		stopListening();
	}
	comms_port = portnum;
}

int EZRelay::getCommsPort() {
	return comms_port;
}

//Sets the backlog size for communication requests on each socket.
//Stops listening if called, listen() must be invoked again.
void EZRelay::setBacklogSize(int blsize) {
	if(is_listening) {
		stopListening();
	}
	backlog_size = blsize;
}

void EZRelay::setVerboseOutput(bool verbose_enabled){
	verbose = verbose_enabled;
}

//Listens on the inbound relay commsport for clients
void EZRelay::listen() {
	if(!is_listening){
		try{
			comms_socket = createListener(comms_port, backlog_size);
		} catch(...) {
			std::throw_with_nested(
				std::runtime_error("EZRelay::listen: Unable to createListener in listen(" + std::to_string(comms_port) + ", " + std::to_string(backlog_size) + ").")
			);
		}
		addPollSocket(comms_socket);
		is_listening = true;
	}
}

//Stops listening for new client clients.
void EZRelay::stopListening() {
	if(is_listening){
		addToCloseQueue(comms_socket);
		is_listening = false;
	}
}

void EZRelay::run(int timeout) {
	if(!is_listening){
		listen();
	}
	auto cb = [&](pollfd tmp_pfd) {
		runHandler(tmp_pfd);
	};
	try{
		processCloseQueue();
		doPoll(poll_sockets, timeout, cb);
	} catch(...) {
		std::throw_with_nested(
			std::runtime_error("EZRelay::run: Error in processCloseQueue or doPoll.")
		);
	}
}

bool EZRelay::readLine(int sockid, std::string &line) {
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

void EZRelay::sendString(int sockid, std::string sendData) {
	try {
		send(sockid, sendData.data(), sendData.size(), 0);
	} catch(...) {
		std::throw_with_nested(
			std::runtime_error("EZRelay::sendString: Error produced in send(" + std::to_string(sockid) + ", buffer, " + std::to_string(sendData.size()) + ", 0).")
		);
	}
}

//TODO: closeRelay()