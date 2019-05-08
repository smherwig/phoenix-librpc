INCLUDES= -I$(HOME)/include
STATIC_LIBS= $(addprefix $(HOME)/lib/, librpc.a librho.a)

CPPFLAGS= $(INCLUDES) -DRHO_DEBUG -DRHO_TRACE
#CPPFLAGS= $(INCLUDES)
CFLAGS= -Wall -Werror -Wextra
LDFLAGS= $(STATIC_LIBS) -lssl -lcrypto -lpthread

OBJS= benchserver.o benchclient.o

all: benchserver benchclient

benchserver: benchserver.o
	$(CC) -o $@ $^ $(LDFLAGS)

benchclient: benchclient.o
	$(CC) -o $@ $^ $(LDFLAGS)

benchserver.o: benchserver.c

benchclient.o: benchclient.c

clean:
	rm -f benchserver benchclient $(OBJS)

.PHONY: all clean
