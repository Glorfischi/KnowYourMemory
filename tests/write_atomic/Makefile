PREFIX = ../..

CC = g++
LDFLAGS =  -libverbs -lpthread -lrdmacm
CFLAGS += -Wall -I$(PREFIX)/include  -I$(PREFIX)/src -I$(PREFIX)/external
CXXFLAGS = $(CFLAGS)

BINDIR = ./bin
SRCPATH = $(PREFIX)/src

BIN = $(BINDIR)/writeatomic
SRCS=  ./main.cpp ./write_atomic.cpp $(SRCPATH)/endpoint.cpp $(SRCPATH)/receive_queue.cpp \
			 $(SRCPATH)/ring_buffer/magic_buffer.cpp $(SRCPATH)/mm/dumb_allocator.cpp 
OBJS= $(SRCS:.cpp=.o)

all: $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BIN) $(OBJS) $(LDFLAGS)

clean:
	@echo "##### CLEAN-UP #####"
	-rm -f $(OBJS)
	-rm -f $(BIN)
	@echo "####################"
	@echo

%.o: %.cpp 
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<

.DELETE_ON_ERROR:
.PHONY: all clean


