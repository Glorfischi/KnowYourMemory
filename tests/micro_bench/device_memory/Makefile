PREFIX = ../../..

CC = g++
LDFLAGS =  -libverbs -lpthread -lrdmacm
CFLAGS += -Wall -std=c++11 -I$(PREFIX)/include  -I$(PREFIX)/src -I$(PREFIX)/external
CXXFLAGS = $(CFLAGS)

BINDIR = ./bin
SRCPATH = $(PREFIX)/src

SRCS =  $(SRCPATH)/endpoint.cpp 

OBJS = $(SRCS:.cpp=.o)

local:  ./local.o $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BINDIR)/local ./local.o $(OBJS) $(LDFLAGS)

write:  ./write.o $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BINDIR)/write ./write.o $(OBJS) $(LDFLAGS)

atomic:  ./atomic.o $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BINDIR)/atomic ./atomic.o $(OBJS) $(LDFLAGS)




clean:
	@echo "##### CLEAN-UP #####"
	-rm -f $(OBJS)
	-rm -f $(BINDIR)/local
	@echo "####################"
	@echo

%.png: %.p $(DATADIR)
	gnuplot $<

%.o: %.cpp 
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<

.DELETE_ON_ERROR:
.PHONY: all clean


