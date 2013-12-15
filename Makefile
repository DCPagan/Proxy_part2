cc=gcc -Wall -g 2>proxy.err
proxy: main.o rio.o proxy.o handlers.o route.o
	$(cc) -o proxy main.o rio.o proxy.o handlers.o route.o -pthread -lrt
main.o: main.c proxy.h
	$(cc) -c -o main.o main.c
rio.o: rio.c rio.h
	$(cc) -c -o rio.o rio.c
proxy.o: proxy.c proxy.h
	$(cc) -c -o proxy.o proxy.c
handlers.o: handlers.c proxy.h
	$(cc) -c -o handlers.o handlers.c
route.o: route.c proxy.h
	$(cc) -c -o route.o route.c
clean:
	rm proxy main.o rio.o proxy.o handlers.o route.o
