Donald Pagan
Jonathan Chen
machines:	cs352-5, cs352-6
user:		csuser
password:	cs198352

	The program was organized to separate the core functions and processes of the program from the program's interface. cs352proxy.c held all of the core functions, and cs352proxy.h had all of the include statements, typedefs and function declarations, and acted as an interface for main() to access the functions of cs352proxy.c and all of the other library functions and structures.
	
	We separated main() in two cases depending on its parameter set via a switch-case statement. In case 3, that is, if there were 2 parameters passed to the program, the program would act as a server with one socket that would listen to the specified port, and another that would listen to the tap device. In case 4, with three parameters, the program would act as a client, connecting to the remote host at the specified remote port, and connecting to the tap device with another file descriptor. The procedures to extract the port number from the argument list and to create the threads that would listen to each socket was the same between each of the cases; the only main differences were in which functions were called to create the ethernet socket.
	
	I, Donald, worked on the cases for the main function, and wrote the open_listenfd() and open_clientfd() functions, whose fundamental ideas can be found in the textbook "Computer Systems: A Programmer's Perspective" in chapter 11, which covers network programming. Jon Chen worked on the functions that the threads individually invoke, which read packets and, depending on which device from which they originate, either encapsulates or de-encapsulates the packet and writes it to the other device.
	
	The main difficulties that we had were in learning how threads worked, as this was the first time that we have ever done any concurrent programming, and in understanding how to read and write packets from and to devices. Once we found the MTU's to be 1500 for our machines, and how read() and write() function the same for socket and packet I/O as for file I/O, programming the thread code was simple. However, we came across the following linking error:
	
	/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../../lib32/crt1.o: In function `_start':
(.text+0x18): undefined reference to `main'
collect2: error: ld returned 1 exit status

	We do not understand what this error implies or how to fix it, but we are confident in the content of our code.
