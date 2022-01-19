#include <unistd.h>

void* smalloc(size_t size)
{
    if(size == 0)
        return NULL;
    if(size > 1e8)
        return NULL;
    void* ptr;
    ptr = sbrk(size);
    if((long)ptr == -1)
        return NULL;
    return ptr;   
}