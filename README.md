# EZRelay - A C++11 TCP Relay Library

----
## What is EZRelay?
EZRelay is a C++11 library intended to make it simple to create a TCP relay or integrate it with your existing C++11 applications. Included is the EZRelay library for creating and managing relays and an EZRelayClient library which can be given to applications which want to work with an EZRelay server. The libraries handle connection management and only relies on the C++11 Standard Template Library. It is multi-tenant. Any registered clients can receive any number of requests and a relay can accept any number of clients.

Included is an example relay and an example echo server for testing.


----
## Compiling and using examples locally

You can make everything (relay and echo) using:

```bash
make all
```
And reset the build with:

```bash
make clean
```

### 1. Example relay server

#### To compile the relay

```bash
make relay
```

#### Using the relay server

```bash
./relay -n "127.0.0.1" -p 7018
> Relay operational, begin connecting to 127.0.0.1:7018
```

### 2. Example echo server

#### To compile the echo server

```bash
make echoserver
```

#### Using the echo server

```bash
./echoserver -n "127.0.0.1" -p 7018
> established relay address: 127.0.0.1:59201
```

### 3. Connecting to echo server through relay using telnet

```bash
telnet 127.0.0.1 59201
Trying 127.0.0.1...
Connected to 127.0.0.1.
Escape character is '^]'.
Hello World!
Hello World!
```

If everything is working you'll see whatever you type repeated back to you.

----

## Integrating EZRelayClient into your C++ applications

### EZRelayClient public API

```c++
//constructor
EZRelayClient();

//relay's hostname to connect through
void setRelayHostname(std::string hn);
std::string getRelayHostname();

//port to connect to relay over
void setCommsPort(int portnum);
int getCommsPort();

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
```

### Using EZRelayClient library

```c++
#include "ezrelayclient.h"

...

EZRelayClient relayclient;
relayclient.setRelayHostname(hostname);
relayclient.setRelayPort(port);
int relaysocket = relayclient.requestRelay();
std::string relayInfo; 
bool result = relayclient.readLine(relaysocket, relayInfo);
std::cout << "established relay address: " << relayInfo << std::endl;
while(relayclient.run(10000, echo)) {
	continue;
}
```

You'll also need to create a callback to handle requests. In the above example, the function "echo()" is the callback. See example callback below:

```c++
#include <fcntl.h>
void echo(int sockid, int echopipe[2]) {
	char buffer[4096];
	ssize_t len;
	len = splice(sockid, NULL, echopipe[1], NULL, sizeof(buffer), SPLICE_F_MOVE);
	if(len > 0) {
		ssize_t sent;
		sent = splice(echopipe[0], NULL, sockid, NULL, len, SPLICE_F_MOVE);
	}
}
```

## Integrating EZRelay into your C++ applications

### EZRelay public API

```c++
//constructor
EZRelay();

//relay's hostname to connect through
void setRelayHostname(std::string hn);
std::string getRelayHostname();

//port to connect to relay over
void setRelayPort(int portnum);
int getRelayPort();

//sets the backlog size for sockets
void setBacklogSize(int size);

//listens for new clients
void listen();
//stops listening for new clients
void stopListening();

//each call to run() will go through the process of checking for messages
//should be executed in a loop to poll for messages
bool run(int timeout);

//HELPERS
	//readLine() takes a socket and reads buffer until it finds a newline
	//returns entire set of buffers found to newline and extra data after newline
	bool readLine(int sockid, std::string &line);
	//sends a string to the socket passed
	void sendString(int sockid, std::string sendData);
```

### Using EZRelay library

Building a relay is pretty straight forward. Set the relay's information, listen, and call run().

```c++
#include "ezrelay.h"

...

EZRelay relay;
relay.setCommsPort(port);
relay.setRelayHostname(std::string(hostname));
relay.setBacklogSize(backlog);
relay.listen();
relay.run(5000);
std::cout << "Relay operational, begin connecting to " << relay.getRelayHostname() << ":" << std::to_string(relay.getCommsPort()) << std::endl;
while(1) {
	relay.run(10000);
}
```

----
## changelog
* 2019-02-27 Initial creation of README.
* 2019-03-08 Updated for changes in public interface and compilation