CC_FLAGS = -g -O2 -Os -Wfatal-errors

LDNS_ARGS = -I/usr/include/ldns
GIT_ARGS = -I/usr/include/git2  -D__CLANG_INTTYPES_H


all: build

clean:
	rm -f *.o

build: main.o dns_bsd3.o dns_server.o common.o clone.o commit.o fetch.o push.o
	gcc main.o dns_bsd3.o dns_server.o common.o clone.o commit.o fetch.o push.o -L/usr/lib -lldns  -lgit2

dns_server.o: dns_server.c 
	gcc $(CC_FLAGS) $(LDNS_ARGS) -c dns_server.c 

dns_bsd3.o: dns_bsd3.c 
	gcc $(CC_FLAGS) $(LDNS_ARGS) -c dns_bsd3.c 

main.o: main.c
	gcc $(CC_FLAGS) $(LDNS_ARGS) -c main.c 

common.o: git/common.c 
	gcc $(CC_FLAGS) $(GIT_ARGS) -c git/common.c

clone.o: git/clone.c
	gcc $(CC_FLAGS) $(GIT_ARGS) -c git/clone.c

commit.o: git/commit.c
	gcc $(CC_FLAGS) $(GIT_ARGS) -c git/commit.c

fetch.o: git/fetch.c
	gcc $(CC_FLAGS) $(GIT_ARGS) -c git/fetch.c

push.o: git/push.c
	gcc $(CC_FLAGS) $(GIT_ARGS) -c git/push.c

