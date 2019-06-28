# the compiler to use
CXX = clang++
CXXFLAGS  = -std=c++11
RM = rm

all : relay echoserver

relay: relay.cpp ezrelay.cpp logger.cpp
	$(CXX) $(CXXFLAGS) relay.cpp ezrelay.cpp logger.cpp -o relay

echoserver: echoserver.cpp ezrelayclient.cpp logger.cpp
	$(CXX) $(CXXFLAGS) echoserver.cpp ezrelayclient.cpp logger.cpp -o echoserver

clean:
	$(RM) relay
	$(RM) echoserver
