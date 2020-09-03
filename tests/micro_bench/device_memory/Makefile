PREFIX = ../../..

CC = g++
LDFLAGS =  -libverbs -lpthread -lrdmacm
CFLAGS += -Wall -std=c++11 -I$(PREFIX)/include  -I$(PREFIX)/src -I$(PREFIX)/external
CXXFLAGS = $(CFLAGS)

BINDIR = ./bin
SRCPATH = $(PREFIX)/src

SRCS =  ./local.cpp $(SRCPATH)/endpoint.cpp 

OBJS = $(SRCS:.cpp=.o)

local:  $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BINDIR)/local $(OBJS) $(LDFLAGS)



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
.PHONY: all clean


