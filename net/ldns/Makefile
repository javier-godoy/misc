CC_FLAGS = -g -O2 -Os -Wfatal-errors

LDNS_ARGS = -I/usr/include/ldns

all: main

main: dns_server.o

clean:
	rm -f *.o

dns_server.o: dns_server.c 
	gcc $(CC_FLAGS) $(LDNS_ARGS) -c dns_server.c 

main: dns_server.o
	gcc dns_server.o -L/usr/lib -lldns

