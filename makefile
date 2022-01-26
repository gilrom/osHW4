# CC=g++

# %.o: %.c
# 	$(CC) -c -o -g $@ $<

test3:
	g++ -std=c++14 -Wall -g -o test3 malloc_3.cpp malloc_3.h test.cpp


