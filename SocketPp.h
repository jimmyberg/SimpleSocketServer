
#ifndef _SOCKET_H_
#define _SOCKET_H_ 

#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h> 
#include <thread>
#include <unistd.h>
#include <vector>

/**
 * @brief      Class for throw's concerning socket errors.
 */
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
 * @brief      Abstract Base Class for socket connections.
 *
 *             This class needs to be derived. After that the derived class can
 *             be used as an template argument for the WelcomingSocket class.
 *
 * @note       This class is not copyable due to the std::thread as member. If
 *             necessary it is recommended to use @p new and @p delete to
 *             maintain its real position in memory. The ConnectionManager class
 *             takes care of this problem.
 */
class Connection{
public:
	/**
	 *
	 * @param[in]  socketfd  The socketfd recieved from a socket accept
	 * @param[in]  usesSsl   Specify if the connection should have SSL.
	 */
	Connection(int isocketfd, bool useSsl);
	virtual ~Connection();
	/**
	 * @brief      Enum to specify the states for connection.
	 *
	 *             This will be used- or can be used by the ConnectionManager.
	 *             When available then the connection is clossed and this object
	 *             can be destroyed.
	 */
	enum class State{
		running,
		available
	};
	/**
	 * @brief      Gets the state as specfified by Connection::State.
	 *
	 * @return     The state of Connection.
	 */
	State getState(){return state;}
	/**
	 * @brief      Join the running thread meaning this function will wait for
	 *             the connection to be closed.
	 * @warning    This function will block as long as the connection is being
	 *             held up. If the closure is not guaranteed to happen it is
	 *             recommended to use Connection::joinThreadIfAvailable instead.
	 */
	void joinThread();
	/**
	 * @brief      Joint thread if state equals Connection::State::available.
	 *             Continues otherwise.
	 *
	 * @return     True if state equals Connection::State::available. False
	 *             otherwise.
	 */
	bool joinThreadIfAvailable();
	/**
	 * @brief      Gets the socket fd.
	 *
	 * @return     The socket fd.
	 */
	int getSocketFd(){return assignedSocketfd;}
	/**
	 * @brief      Kill thread not matter what.
	 */
	void kill();
	/**
	 * @brief      Custom data handler.
	 *
	 *             This function needs to be filled in by the derived class.
	 *             This function will be called by the ConnectionManager when
	 *             new data arrives from the socket.
	 *
	 * @param      inputData  Pointer to the data that has been received.
	 * @param[in]  size       The size of the array containing the received
	 *                        data.
	 */
	virtual void dataHandler(char *inputData, size_t size) = 0;
	/**
	 * @brief      Writes data to the connected socket.
	 *
	 * @param[in]  outputData  Pointer to the output data array
	 * @param[in]  size        The size of the output data array.
	 *
	 * @return     Bytes written to socket. Returns -1 on failure.
	 */
	ssize_t write(const char *outputData, const size_t size){
		writeMutex.lock();
		ssize_t ret = size;
		if(usesSsl){
			int sslRet;
			if((sslRet = SSL_write(cSSL, outputData, size)) <= 0){
				ret = -1;
			}
			else{
				ret = sslRet;
			}
		}
		else{
			// Use system write. Not own write which would cause infinite
			// recursion.
			ret = ::write(socketfd, outputData, size);
		}
		writeMutex.unlock();
		return ret;
	}
	const int socketfd;
protected:
	bool keepRunning = true;
private:
	ssize_t read(char *inputData, const size_t size){
		if(usesSsl){
			int sslRet;
			if((sslRet = SSL_read(cSSL, inputData, size)) <= 0){
				return -1;
			}
			else{
				return sslRet;
			}
		}
		else{
			return ::read(socketfd, inputData, size);
		}
	}
	unsigned int inputIndex = 0;
	const bool usesSsl;
	int assignedSocketfd;
	Connection();
	void threadFunction(bool useSsl);
	State state = Connection::State::running;
	std::thread th;
	std::mutex writeMutex;
	SSL_CTX *sslctx;
	SSL *cSSL;
	int ssl_err;
};

/**
 * @brief      Class for managing multiple Connection's.
 *
 *             This socket is able to open close and manage (practically) infinite number of
 *             connection sockets.
 */
class ConnectionManager{
public:
	/**
	 * @brief      Constructor
	 *
	 * @param[in]  useSsl  States if future Connections need to use SSL or not.
	 */
	ConnectionManager(bool useSsl);
	~ConnectionManager();
	/**
	 * @brief      Creates new connection object that will takeover the socket
	 *
	 * @param[in]  socketfd  The socketfd
	 *
	 * @tparam     T         Used to specify which Connection implementation to
	 *                       use. Can only be classes inherited from Connection.
	 */
	template<typename T>
	void assignConnection(int socketfd){
		managingLock.lock();
		std::cout << "Assigning connection " << socketfd << " ..." << std::flush;
		connections.push_back(new T(socketfd, usesSsl));
		std::cout << "done" << std::endl;
		managingLock.unlock();
	}
	/**
	 * @brief      Destroys Connections at which there states are marked
	 *             Connections::State::available.
	 */
	void cleanup();
	/**
	 * @brief      Closes/kills all connections. Used by destructor.
	 */
	void closeAllConnections();
	/**
	 * @brief      Print a list of connection to the terminal.
	 */
	void printConnections();
	/**
	 * @brief      Kill socket with socketfd if present.
	 *
	 * @param[in]  socketfd  The socketfd
	 */
	void kill(int socketfd);
private:
	const bool usesSsl;
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
 *
 * @tparam     T     Type of Connection it needs to use.
 */
template<typename T>
class WelcomingSocket{
public:
	/**
	 * @brief      Constructor
	 *
	 * @param[in]  portnumber  The portnumber it needs to listen to.
	 * @param[in]  useSsl      Specify if future Connection objects need to use
	 *                         SSL.
	 */
	WelcomingSocket(int portnumber, bool useSsl):manager(useSsl){
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) 
			throw SocketError("Could not open socket");
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(portnumber);
		if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
			int errorNumber = errno;
			static constexpr char text[] = "Could not bind socket ";
			char mess[sizeof(text) + 5];
			sprintf(mess, "%s%d", text, errorNumber);
			throw SocketError(mess);
		}
		listen(sockfd, 5);
		acceptSocket = std::thread(&WelcomingSocket<T>::acceptIncomming, this);
		std::cout << "Welcoming socket started. fd number = " << sockfd << std::endl;
	}
	void printConnections(){manager.printConnections();}
	void kill(int socketfd){manager.kill(socketfd);}
	~WelcomingSocket(){
	if(sockfd >= 0){
		std::cout << "Closing welcoming socket..." << std::flush;
		shutdown(sockfd, SHUT_RDWR);
		acceptSocket.join();
		std::cout << "done" << std::endl;
		close(sockfd);
		sockfd = -1;
	}
}
private:
	int sockfd;
	struct sockaddr_in serv_addr, cli_addr;
	std::thread acceptSocket;
	void acceptIncomming(){
		int newsockfd;
		socklen_t clilen = sizeof(cli_addr);
		while(1){
			newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
			if(newsockfd < 0){
				int errorNumber = errno;
				std::cerr << "socket accept error. Error number of errno = " << errorNumber << std::endl;
				break;
			}
			else{
				manager.assignConnection<T>(newsockfd);
			}
		}
	}
	ConnectionManager manager;
};

#endif
