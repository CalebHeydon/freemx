CFLAGS=
LDFLAGS=-lpthread

OBJS=freemx.o

default: freemx

freemx: ${OBJS}
	$(CC) ${OBJS} -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $< -c $(CFLAGS)

clean:
	rm -rf freemx *.o
