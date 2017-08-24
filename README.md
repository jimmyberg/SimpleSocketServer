# SimpleSocketServer
This repository contains a library that can be used to quickly setup a server that can handle (depending on the available recourses like RAM) unlimited connections.

# Install
Install this library like you would with every library. Either include the .cpp and .h file(s) to your project or install it globally as a dynamic linked library.

Using this library without SSL is very simple. When using SSL a certificate needs to be made and used. These certificates needs to be installed at your PC. See source code or potential error message where to install these certificates or change the file location in the source code. It is up to you.

# Using this library
This library can not be used directly since its implementation (what its needs to do when receiving data) is unspecified. You need to create a derived class and supply the implementation. An example can be found at:
https://github.com/overlord1123/ServerSocketExample.

Good luck!
