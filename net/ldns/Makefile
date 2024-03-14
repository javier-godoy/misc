CC_FLAGS = -g -O2 -Os -Wfatal-errors

LDNS_ARGS = -DHAVE_ARPA_INET_H -DHAVE_NETINET_IN_H -DHAVE_NETINET_UDP_H -I/usr/include/ldns

all: main

main: dns_server.o

clean:
	rm -f *.o

dns_server.o: dns_server.c dns_server.h
	gcc $(CC_FLAGS) $(LDNS_ARGS) -c dns_server.c 
