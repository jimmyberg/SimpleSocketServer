
#ifndef _SOCKET_H_
#define _SOCKET_H_ 

#include <thread>
#include <string>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>
#include <mutex>

class SocketError{
public:
	SocketError(){
		std::cerr << "Socket Error: Reason not specified. Sorry." << std::endl;
	}
	SocketError(std::string what){
		std::cerr << "Socket Error: " << what << std::endl;
	}
};

/**
 * @brief      Class for socket connection.
 *
 *             Note that this class is not copyable due to thread as member. If
 *             necessary it is recommended to use new and delete to maintain its
 *             real position in memory.
 */
class Connection{
public:
	Connection(int socketfd);
	~Connection();
	enum class State{
		running,
		available
	};
	State getState(){return state;}
	void joinThread();
	bool joinThreadIfAvailable();
	int getSocketFd(){return assignedSocketfd;}
	void kill();
private:
	int assignedSocketfd;
	Connection();
	void threadFuntion(int socketfd);
	State state = Connection::State::running;
	std::thread th;
};

/**
 * @brief      Class for managing multiple connection.
 *
 *             This socket is able to open close and manage (practically) infinite number of
 *             connection sockets.
 */
class ConnectionManager{
public:
	ConnectionManager();
	~ConnectionManager();
	void assignConnection(int socketfd);
	void cleanup();
	void closeAllConnections();
	void printConnections();
	void kill(int socketfd);
private:
	void garbageCollectorFunction();
	std::vector<Connection*> connections;
	std::mutex managingLock;
	std::thread garbageCollectorThread;
	bool garbageColectorKeepRunning = true;
	static constexpr useconds_t garbageCollectIntervalUs = 10000000;
};

/**
 * @brief      Class for welcoming socket.
 *
 *             This socket will answer new connections and make opens new
 *             connection sockets dynamically.
 */
class WelcomingSocket{
public:
	WelcomingSocket(int portnumber);
	void printConnections(){manager.printConnections();}
	void kill(int socketfd){manager.kill(socketfd);}
	~WelcomingSocket();
private:
	int sockfd;
	struct sockaddr_in serv_addr, cli_addr;
	std::thread acceptSocket;
	void acceptIncomming();
	ConnectionManager manager;
};

#endif
