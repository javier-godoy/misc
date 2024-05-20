CC_FLAGS = -g -O2 -Os -Wfatal-errors 

all: main

main: minipool.o
	g++ minipool.o -o minipool-dynamic
	g++ minipool.o -o minipool-static -static
	
minipool.o: minipool.cpp
	g++ $(CC_FLAGS) -c minipool.cpp

clean:
	rm -f *.o
