CC=gcc
CFLAGS=-Wall -g -fsanitize=undefined
DEPS = string.h linkedList.h resourceStack.h gopherConn.h serveRequest.h
OBJ = string.o linkedList.o resourceStack.o gopherConn.o serveRequest.o main.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

main: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o
