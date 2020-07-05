PREFIX = .

CC = g++
LDFLAGS =  -libverbs -lpthread -lrdmacm
CFLAGS += -Wall -I$(PREFIX)/include  -I$(PREFIX)/src -I$(PREFIX)/external
CXXFLAGS = $(CFLAGS)

BINDIR = $(PREFIX)/bin

LIBPATH = $(PREFIX)/lib
SRCPATH = $(PREFIX)/src
TESTPATH = $(PREFIX)/tests

HEADERS = $(shell echo $(PREFIX)/include/kym/*.hpp) $(shell echo $(PREFIX)/src/*.hpp)


TEST = $(BINDIR)/test
TEST_SRCS=  $(TESTPATH)/main_test.cpp $(SRCPATH)/mm/dumb_allocator.cpp $(SRCPATH)/conn/send_receive.cpp
TEST_OBJS= $(TEST_SRCS:.cpp=.o)

test: $(TEST_OBJS)
	mkdir -pm 755 $(BINDIR)
	$(CC) $(CFLAGS) -o $(TEST) $(TEST_OBJS) $(LDFLAGS)

debug: CFLAGS += -DDEBUG -g -O0
debug: ${APPS}
clean:
	@echo "##### CLEAN-UP #####"
	-rm -f $(TEST_OBJS)
	-rm -f $(TEST)
	@echo "####################"
	@echo

%.o: %.cpp $(HEADERS)
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<

.DELETE_ON_ERROR:
.PHONY: all clean


