#include <unistd.h>
#define MAX_SIZE 100000000

void* smalloc(size_t size)
{
    if(size == 0)
        return NULL;
    if(size > MAX_SIZE)
        return NULL;
    void* ptr;
    ptr = sbrk(size);
    if(ptr == (void*)(-1))
        return NULL;
    return ptr;   
}