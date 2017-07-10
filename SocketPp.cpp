
#include "SocketPp.h"

using namespace std;

Connection::Connection(int socketfd){
	assignedSocketfd = socketfd;
	th = thread(&Connection::threadFuntion, this, socketfd);
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
void Connection::threadFuntion(int socketfd){
	state = Connection::State::running;
	char socketBuffer[1024];
	char inputConsideration[1024];
	unsigned int inputIndex = 0;
	ssize_t readLen;
	bool keepRunning = true;
	while(keepRunning){
		readLen = read(socketfd, socketBuffer, sizeof(socketBuffer) - 1);
		if(readLen > 0){
			socketBuffer[readLen] = 0;
			cout << "Received from socket nr " << socketfd << " : " << socketBuffer;
			cout.flush();
			//Add incoming data to input buffer.
			for (int i = 0; i < readLen; ++i){
				inputConsideration[inputIndex++] = socketBuffer[i];
				// If a \n character is received then the input data is checked.
				if(socketBuffer[i] == '\n'){
					inputIndex = 0;
					if(strncmp(inputConsideration, "quit", 4) == 0){
						keepRunning = false;
						break;
					}
				}
			}
		}
		if(readLen < 0){
			keepRunning = false;
		}
	}
	if(readLen >= 0){
		static constexpr char quitMsg[] = "Goodbye!\n";
		write(socketfd, quitMsg, sizeof(quitMsg));
	}
	close(socketfd);
	state = Connection::State::available;
	cout << "Connection " << socketfd << " closed." << endl;
}

ConnectionManager::ConnectionManager(){
	garbageCollectorThread = thread(&ConnectionManager::garbageCollectorFunction, this);
}
ConnectionManager::~ConnectionManager(){
	closeAllConnections();
	garbageColectorKeepRunning = false;
	garbageCollectorThread.join();
}
void ConnectionManager::assignConnection(int socketfd){
	managingLock.lock();
	cout << "Assigning connection " << socketfd << " ...";
	cout.flush();
	connections.push_back(new Connection(socketfd));
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

WelcomingSocket::WelcomingSocket(int portnumber){
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
