PREFIX = ../../..

CC = g++
LDFLAGS =  -libverbs -lpthread -lrdmacm
CFLAGS +=  -O2 -Wall -std=c++11 -I$(PREFIX)/include  -I$(PREFIX)/src -I$(PREFIX)/external



CXXFLAGS = $(CFLAGS)

BINDIR = ./bin
SRCPATH = $(PREFIX)/src

SRCS =  

OBJS = $(SRCS:.cpp=.o)

status: ./status.o $(OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(BINDIR)/status ./status.o $(OBJS) $(LDFLAGS)


clean:
	@echo "##### CLEAN-UP #####"
	-rm -f $(OBJS)
	-rm -f $(BINDIR)/*
	-rm -f ./*.o
	@echo "####################"
	@echo

%.o: %.cpp 
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<

.DELETE_ON_ERROR:
.PHONY: all clean


