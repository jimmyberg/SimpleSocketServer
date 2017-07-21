
#include "SocketPp.h"

using namespace std;

Connection::Connection(int isocketfd, bool useSsl):socketfd(isocketfd), usesSsl(useSsl){
	assignedSocketfd = socketfd;
	th = thread(&Connection::threadFunction, this, socketfd);
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
void Connection::threadFunction(bool useSsl){
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
	// This is a weird but optimized if statement. It means it will check
	// ssl_err only if usesSsl == true.
	if(usesSsl == false || ssl_err > 0){
		state = Connection::State::running;
		char socketBuffer[1024];
		ssize_t readLen;
		while(keepRunning){
			readLen = read(socketBuffer, sizeof(socketBuffer) - 1);
			if(readLen > 0){
				socketBuffer[readLen] = 0;
				dataHandler(socketBuffer, readLen);
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
			while((curErrorCode = ERR_get_error())){
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
