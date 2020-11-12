all: sender_test receiver_test

sender_test: sender_test.cpp
	g++  -std=c++11 sender_test.cpp -Wall -O2 -o sender_test -libverbs -lpthread

receiver_test: receiver_test.cpp
	g++  -std=c++11 receiver_test.cpp -Wall  -O2 -o receiver_test -libverbs

clean:
	rm -f sender_test receiver_test
