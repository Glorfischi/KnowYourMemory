PREFIX = ../..

CC = g++
LDFLAGS =  -libverbs -lpthread -lrdmacm -lmlx5
CFLAGS += -Wall -I$(PREFIX)/include  -I$(PREFIX)/src -I$(PREFIX)/external -I$(PREFIX)/src/bench
CXXFLAGS = $(CFLAGS)

BINDIR = ./bin
PLOTDIR = ./plot
DATADIR = ./data
SRCPATH = $(PREFIX)/src

BIN = $(BINDIR)/sendrecv
SRCS =  ./main.cpp ./send_receive.cpp $(SRCPATH)/endpoint.cpp $(SRCPATH)/receive_queue.cpp $(SRCPATH)/mm/dumb_allocator.cpp  \
				$(SRCPATH)/bench/bench.cpp $(SRCPATH)/async_events.cpp


OBJS = $(SRCS:.cpp=.o)
PLOT = $(PLOTDIR)/latency.png

all: CFLAGS += -O2
all: $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BIN) $(OBJS) $(LDFLAGS)

plot: $(PLOT)

debug: CFLAGS += -DDEBUG -g -O0
debug: $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BIN) $(OBJS) $(LDFLAGS)
	
clean:
	@echo "##### CLEAN-UP #####"
	-rm -f $(OBJS)
	-rm -f $(BIN)
	-rm -f $(PLOT)
	@echo "####################"
	@echo

%.png: %.p $(DATADIR)
	gnuplot $<

%.o: %.cpp 
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<

.DELETE_ON_ERROR:
.PHONY: all clean plot


