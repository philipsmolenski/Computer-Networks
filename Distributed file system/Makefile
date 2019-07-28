TARGET: netstore-client netstore-server

CC = g++
CFLAGS = -std=c++17 -Wall -Wextra -O2 
LFLAGS = -std=c++17 -Wall -Wextra -O2

err.o: err.cpp err.h

helper.o: helper.cpp
	$(CC) $(LFLAGS) $^ -c

netstore-client: netstore-client.cpp err.o helper.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_program_options -lstdc++fs

netstore-server: netstore-server.cpp err.o helper.o	
	$(CC) $(LFLAGS) $^ -o $@ -lboost_program_options -lstdc++fs

.PHONY: clean TARGET
clean:
	rm -f netstore-client netstore-server *.o *~ *.bak