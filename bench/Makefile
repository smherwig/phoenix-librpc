INCLUDES= -I$(HOME)/include
STATIC_LIBS= $(addprefix $(HOME)/lib/, librpc.a librho.a)

#CPPFLAGS= $(INCLUDES) -DRHO_DEBUG -DRHO_TRACE
CPPFLAGS= $(INCLUDES)
CFLAGS= -Wall -Werror -Wextra
LDFLAGS= $(STATIC_LIBS) -lssl -lcrypto -lpthread

OBJS= rpcbenchserver.o rpcbenchclient.o memcpy_bench.o

all: rpcbenchserver rpcbenchclient memcpy_bench

rpcbenchserver: rpcbenchserver.o
	$(CC) -o $@ $^ $(LDFLAGS)

rpcbenchclient: rpcbenchclient.o
	$(CC) -o $@ $^ $(LDFLAGS)

memcpy_bench: memcpy_bench.o
	$(CC) -o $@ $^ $(LDFLAGS)

rpcbenchserver.o: rpcbenchserver.c

rpcbenchclient.o: rpcbenchclient.c

memcpy_bench.o: memcpy_bench.c

clean:
	rm -f rpcbenchserver rpcbenchclient memcpy_bench $(OBJS)

.PHONY: all clean
