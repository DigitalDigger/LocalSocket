LocalSocket
===========

The main idea for the CLocalSocket class is to provide the functionality similar to standard sockets but without using TCP/IP. In complex solutions it is a quite frequent situation when several applications or services communicate with each other by means of sockets created within one local machine (i.e., within localhost). The CLocalSocket class is designed particulary for that reason - it substitutes the transport level of TCP/IP with the Windows IPC mapfile mechanism so all the data is passed through that mapped files.

The interface of the class is close to the windows sockets:

	bool create_local(const char * szLocalSocketName);
	bool accept(CLocalSocket * c_LocSocket);
	bool connect(char * szLocalSocketName, int nTimeout);
	int write(const char * szDataBlock, int nSizeOfBlock);
	int read(char * szDataBlock, int nSizeOfBlock);
	bool cancel_accept(void);
	bool disconnect(void);

create_local 	- creates a named socket on server side;
accept 		- launching the incoming connection accepting routine, each new connection will be accessible through the CLocalSocket object that is passed as a parameter to this method;
connect		- connects to the server from client side;
write		- sends data to the socket;
read		- receives data from the socket;
cancel_accept	- stops the accepting routine on the server side;
disconnect	- closes connection;

The class exploits mutexes and events for IPC synchronization and signaling. Both events and mutexes are encapsulated within special wrapper classes which allows effortless portability in case if it will be required.

For the moment, it exploits only one buffer without queueing the messages so they represent blocking sockets functionality. In the future, I'd like to extent it to the asyncronous sockets.
