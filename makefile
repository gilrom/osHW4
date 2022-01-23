CC=g++

%.o: %.c
	$(CC) -c -o -g $@ $<

test2: malloc_2.o tamuz_modified_tests_for_malloc_2.o 
	$(CC) -g -o test2 malloc_2.o tamuz_modified_tests_for_malloc_2.o


