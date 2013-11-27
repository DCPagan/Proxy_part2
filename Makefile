cc=gcc -Wall -m32 -g 2>proxy.err
proxy: main.o rio.o proxy.o
	$(cc) -pthread -lrt -o proxy main.o rio.o proxy.o
main.o: main.c proxy.h
	$(cc) -c -o main.o main.c
rio.o: rio.c rio.h
	$(cc) -c -o rio.o rio.c
proxy.o: proxy.c proxy.h
	$(cc) -c -o proxy.o proxy.c
clean:
	rm proxy main.o rio.o proxy.o
