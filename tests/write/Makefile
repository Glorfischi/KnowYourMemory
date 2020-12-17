PREFIX = ../..

CC = g++
LDFLAGS =  -libverbs -lpthread -lrdmacm
CFLAGS += -g -Wall -I$(PREFIX)/include  -I$(PREFIX)/src -I$(PREFIX)/external
CXXFLAGS = $(CFLAGS)

BINDIR = ./bin
SRCPATH = $(PREFIX)/src

BIN = $(BINDIR)/write
SRCS=  ./main.cpp ./write.cpp ./reverse.cpp ./acknowledge.cpp $(SRCPATH)/endpoint.cpp $(SRCPATH)/mm/dumb_allocator.cpp \
			 $(SRCPATH)/bench/bench.cpp  $(SRCPATH)/receive_queue.cpp $(SRCPATH)/ring_buffer/ring_buffer.cpp  \
			 $(SRCPATH)/ring_buffer/magic_buffer.cpp $(SRCPATH)/ring_buffer/reverse_buffer.cpp $(SRCPATH)/async_events.cpp
OBJS= $(SRCS:.cpp=.o)

TSRCS=  ./write_test.cpp ./write.cpp ./reverse.cpp ./acknowledge.cpp $(SRCPATH)/endpoint.cpp $(SRCPATH)/mm/dumb_allocator.cpp \
			 $(SRCPATH)/bench/bench.cpp  $(SRCPATH)/receive_queue.cpp $(SRCPATH)/ring_buffer/ring_buffer.cpp  \
			 $(SRCPATH)/ring_buffer/magic_buffer.cpp  
TOBJS= $(TSRCS:.cpp=.o)

all: CFLAGS += -O2
all: $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BIN) $(OBJS) $(LDFLAGS)

debug: CFLAGS += -DDEBUG -g -O2
debug: $(OBJS)
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


