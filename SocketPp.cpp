
#include "SocketPp.h"

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

using namespace std;

Connection::Connection(int socketfd, bool useSsl):usesSsl(useSsl){
	assignedSocketfd = socketfd;
	th = thread(&Connection::threadFunction, this, socketfd, useSsl);
}
Connection::~Connection(){
	joinThread();
}
void Connection::joinThread(){
	if(th.joinable()){
		th.join();
	}
}
bool Connection::joinThreadIfAvailable(){
	if(state == Connection::State::available && th.joinable()){
		th.join();
		return true;
	}
	else{
		return false;
	}
}
void Connection::kill(){
	shutdown(assignedSocketfd, SHUT_RDWR);
	close(assignedSocketfd);
}
void Connection::threadFunction(int socketfd, bool useSsl){
	SSL_CTX *sslctx;
	SSL *cSSL;
	int ssl_err;
	if(usesSsl){
		ERR_clear_error();
		sslctx = SSL_CTX_new( SSLv23_server_method());
		SSL_CTX_set_options(sslctx, SSL_OP_SINGLE_DH_USE);
		static constexpr char certFilePath[] = "/etc/ssl/certs/192.168.2.30.cert.pem";
		static constexpr char keyFilePath[] = "/etc/ssl/private/192.168.2.30.key.pem";
		static constexpr char chainFilePath[] = "/etc/apache2/ssl.crt/ca-chain.cert.pem";
		int use_chain = SSL_CTX_use_certificate_chain_file(sslctx, chainFilePath);
		if(use_chain != 1){
			cerr << "Error: could not open " << chainFilePath << endl;
			return;
		}
		int use_cert = SSL_CTX_use_certificate_file(sslctx, certFilePath, SSL_FILETYPE_PEM);
		if(use_cert != 1){
			cerr << "Error: could not open " << certFilePath << endl;
			return;
		}
		int use_prv = SSL_CTX_use_PrivateKey_file(sslctx, keyFilePath, SSL_FILETYPE_PEM);
		if(use_prv != 1){
			cerr << "Error: could not open " << keyFilePath << endl;
			return;
		}
		cSSL = SSL_new(sslctx);
		SSL_set_fd(cSSL, socketfd );
		//Here is the SSL Accept portion.  Now all reads and writes must use SSL
		ssl_err = SSL_accept(cSSL);
	}
	if(usesSsl == false || ssl_err > 0){
		state = Connection::State::running;
		char socketBuffer[1024];
		char inputConsideration[1024];
		unsigned int inputIndex = 0;
		ssize_t readLen;
		bool keepRunning = true;
		while(keepRunning){
			if(usesSsl){
				readLen = SSL_read(cSSL, socketBuffer, sizeof(socketBuffer) - 1);
			}
			else{
				readLen = read(socketfd, socketBuffer, sizeof(socketBuffer) - 1);
			}
			if(readLen > 0){
				socketBuffer[readLen] = 0;
				cout.flush();
				//Add incoming data to input buffer.
				for (int i = 0; i < readLen; ++i){
					inputConsideration[inputIndex++] = socketBuffer[i];
					// If a \n character is received then the input data is checked.
					if(socketBuffer[i] == '\n'){
						inputConsideration[inputIndex] = 0;
						inputIndex = 0;
						cout << "Received from socket nr " << socketfd << " : " << inputConsideration;
						if(strncmp(inputConsideration, "quit", 4) == 0){
							keepRunning = false;
							static constexpr char quitMsg[] = "Goodbye!\n";
							if(usesSsl){
								SSL_write(cSSL, quitMsg, sizeof(quitMsg));
							}
							else{
								write(socketfd, quitMsg, sizeof(quitMsg));
							}
							break;
						}
					}
				}
			}
			else{
				keepRunning = false;
			}
		}
	}
	else{
		cerr << "Error: could not accept SSL connection.\nReason by SSL_get_error() = ";
		int get_error_num = SSL_get_error(cSSL, ssl_err);
		cout << get_error_num << endl;
		if(get_error_num == SSL_ERROR_SSL){
			cerr << "Printing SSL error stack:" << endl;
			int curErrorCode;
			while(curErrorCode = ERR_get_error()){
				cerr << ERR_error_string(curErrorCode, nullptr) << '\n';
			}
			cerr.flush();
		}
	}
	if(usesSsl){
		SSL_shutdown(cSSL);
		SSL_free(cSSL);
	}
	close(socketfd);
	state = Connection::State::available;
	cout << "Connection " << socketfd << " closed." << endl;
}

ConnectionManager::ConnectionManager(bool useSsl):usesSsl(useSsl){
	garbageCollectorThread = thread(&ConnectionManager::garbageCollectorFunction, this);
	// Initialize SSL
	if(usesSsl){
		SSL_load_error_strings();
		SSL_library_init();
		OpenSSL_add_all_algorithms();
	}
}
ConnectionManager::~ConnectionManager(){
	closeAllConnections();
	garbageColectorKeepRunning = false;
	garbageCollectorThread.join();
	// Destroy SSL
	if(usesSsl){
		ERR_free_strings();
		EVP_cleanup();
	}
}
void ConnectionManager::assignConnection(int socketfd){
	managingLock.lock();
	cout << "Assigning connection " << socketfd << " ...";
	cout.flush();
	connections.push_back(new Connection(socketfd, usesSsl));
	cout << "done" << endl;
	managingLock.unlock();
}
void ConnectionManager::cleanup(){
	managingLock.lock();
	unsigned int curIndex = 0;
	while(curIndex < connections.size()){
		// See if this connection is marked available
		if(connections.at(curIndex)->getState() == Connection::State::available){
			// Try to join thread
			if(connections.at(curIndex)->joinThreadIfAvailable()){
				// Thread successfully joined. Remove the object and erase pointer
				delete connections.at(curIndex);
				connections.erase(connections.begin() + curIndex);
			}
			else{
				// Thread was not available after all. (should not happen actually)
				cerr << "Error at cleanup: connection marked available could not be joined." << endl;
				curIndex++;
			}
		}
		else{
			curIndex++;
		}
	}
	managingLock.unlock();
}
void ConnectionManager::closeAllConnections(){
	managingLock.lock();
	cout << "Closing all connections...";
	cout.flush();
	while(connections.size()){
		connections.back()->kill();
		connections.back()->joinThread();
		delete connections.back();
		connections.pop_back();
	}
	cout << "done" << endl;
	managingLock.unlock();
}
void ConnectionManager::printConnections(){
	managingLock.lock();
	cout << "Current connections:" << endl;
	if(connections.size() == 0){
		cout << "none" << endl;
	}
	else{
		for(Connection *connection : connections){
			cout << "Connection " << connection->getSocketFd() << " status ";
			switch(connection->getState()){
				case Connection::State::available:
					cout << "available" << endl;
					break;
				case Connection::State::running:
					cout << "running" << endl;
					break;
				default:
					cout << (int)connection->getState() << endl;
			}
		}
	}
	managingLock.unlock();
}
void ConnectionManager::kill(int socketfd){
	managingLock.lock();
	for(auto connection : connections){
		if(connection->getSocketFd() == socketfd){
			connection->kill();
			break;
		}
	}
	managingLock.unlock();
}
void ConnectionManager::garbageCollectorFunction(){
	while(garbageColectorKeepRunning){
		usleep(garbageCollectIntervalUs);
		cleanup();
	}
}

WelcomingSocket::WelcomingSocket(int portnumber, bool useSsl):manager(useSsl){
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
	acceptSocket = thread(&WelcomingSocket::acceptIncomming, this);
	cout << "Welcoming socket started. fd number = " << sockfd << endl;
}
WelcomingSocket::~WelcomingSocket(){
	if(sockfd >= 0){
		cout << "Closing welcoming socket...";
		cout.flush();
		shutdown(sockfd, SHUT_RDWR);
		acceptSocket.join();
		cout << "done" << endl;
		close(sockfd);
		sockfd = -1;
	}
}

void WelcomingSocket::acceptIncomming(){
	int newsockfd;
	socklen_t clilen = sizeof(cli_addr);
	while(1){
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if(newsockfd < 0){
			int errorNumber = errno;
			cerr << "socket accept error. Error number of errno = " << errorNumber << endl;
			break;
		}
		else{
			manager.assignConnection(newsockfd);
		}
	}
}
