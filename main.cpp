/* A simple server in the Internet domain using TCP
   The port number is passed as an argument */
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include "SocketPp.h"

using namespace std;

void printHelp();

void error(const char *msg)
{
	perror(msg);
	exit(1);
}

int main(int argc, char *argv[])
{
	int port = 2500;
	if(argc == 2){
		port = atoi(argv[1]);
	}
	WelcomingSocket socket(port, true);
	bool running = true;
	string inputCLI;
	cout << "Welcome the the server socket example program.\n"
	"Enter \"help\" to display help message." << endl;
	while(running){
		cout << ":> " << flush;
		getline(cin, inputCLI);
		if(inputCLI == ""){

		}
		else if(inputCLI == "help"){
			printHelp();
		}
		else if(inputCLI == "quit" || inputCLI == "q"){
			cout << "Closing...";
			cout.flush();
			running = false;
		}
		else if(inputCLI == "ls"){
			socket.printConnections();
		}
		else if(inputCLI == "clear"){
			cout << "\e[1;1H\e[2J" << endl;
		}
		else if(inputCLI == "kill"){
			int socketfd;
			do{
				cout << "Please specify the socket number you want to kill.\n:> ";
				cout.flush();
				cin.clear();
				cin >> socketfd;
				cin.get();
			}while(cin.good() == false);
			socket.kill(socketfd);
		}
		else{
			cout << "Invalid command \"" << inputCLI << "\"\n";
			printHelp();
		}
	}
	cout << "done" << endl;
	return 0; 
}

void printHelp(){
	cout << "Available commands are:\n\n"

	"help : Prints this help message.\n"
	"quit : Quits the server and closes the clients.\n"
	"ls   : List current connections and there statuses.\n"
	"kill : Kill specific connection." << endl;
}
