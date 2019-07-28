TARGET: netstore-server netstore-client 

CC	= gcc
CFLAGS	= -Wall # -fsanitize=address -fsanitize=undefined
LFLAGS	= -Wall # -fsanitize=address -fsanitize=undefined

err.o helper.o: err.h helper.h

netstore-server: serwer.c err.o helper.o
	$(CC) $(LFLAGS) $^ -o $@

netstore-client: klient.c err.o helper.o
	$(CC) $(LFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f netstore-server netstore-client *.o *~ *.bak
