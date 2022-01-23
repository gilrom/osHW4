#include <unistd.h>
#include <cstring>
#include "malloc_2.h"

//Macroes
#define MAX_SIZE 100000000

#define CHECK_SIZE(SIZE){\
    if (((SIZE) == 0) || ((SIZE) > MAX_SIZE)) return NULL;\
}

#define SBRK_META(PTR, SIZE){\
    PTR = (MallocMetadata*) sbrk(sizeof(*(PTR)) + (SIZE));\
    if((PTR) == (void*)(-1)) return NULL;\
}

typedef struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata *next;
    MallocMetadata *prev;
} MallocMetadata;

//static global varubles
static MallocMetadata* head = NULL; //head of mem blocks list
static MallocMetadata* tail = NULL;//tail of list
static size_t num_free_blocks = 0;
static size_t num_allocated_blocks = 0;
static size_t num_free_bytes = 0;
static size_t num_allocated_bytes = 0;

/**
 * @brief 
 * find the first free block with adequite size
 * @param size 
 * required allocation size
 * @return void* the adress of the free block, NULL if not found
 */
MallocMetadata* get_free_block(size_t size)
{
    if(!head)
        return NULL;
    MallocMetadata* curr = head;
    while(curr != NULL){
        if(curr->is_free && curr->size >= size)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

void* smalloc(size_t size)
{
    CHECK_SIZE(size);
    if(!head)//first allocation
    {
        SBRK_META(head, size);
        head->is_free = false;
        head->size = size;
        head->next = NULL;
        head->prev = NULL;
        tail = head;
        num_allocated_blocks++;
        num_allocated_bytes += size;
        return head + 1;
    }
    //find free block
    MallocMetadata* ptr = get_free_block(size);
    if(ptr)//if found
    {
        ptr->is_free = false;
        num_free_blocks--;
        num_free_bytes -= ptr->size;
        return ptr + 1;
    }
    //if not found
    SBRK_META(ptr, size);
    ptr->prev = tail;
    ptr->next = NULL;
    ptr->size = size;
    ptr->is_free = false;
    tail->next = ptr;
    tail = ptr;
    num_allocated_blocks++;
    num_allocated_bytes += size;
    return ptr + 1;
}

void* scalloc(size_t num, size_t size)
{
    CHECK_SIZE(size*num);
    void* ptr = smalloc(size*num);
    if(!ptr)
        return NULL;
    memset(ptr,0,size*num);//set bytes to zero
    return ptr;
}

void sfree(void* p)
{
    if(!p)
        return;
    MallocMetadata* meta = (MallocMetadata*)p - 1;
    if(meta->is_free)
        return;
    meta->is_free = true;
    num_free_blocks++;
    num_free_bytes += meta->size;  
}

void* srealloc(void* oldp, size_t size)
{
    CHECK_SIZE(size);
    if(!oldp)
        return smalloc(size);
    MallocMetadata* old_meta = ((MallocMetadata*)oldp) - 1;
    if(old_meta->size >= size)
        return oldp;
    void* newp = smalloc(size);
    if(!newp)
        return NULL;
    memcpy(newp, oldp, old_meta->size);//copy bytes
    sfree(oldp);
    return newp;
}

size_t _num_free_blocks()
{
    return num_free_blocks;
}

size_t _num_free_bytes()
{
    return num_free_bytes;
}

size_t _num_allocated_blocks()
{
    return num_allocated_blocks;
}

size_t _num_allocated_bytes()
{
    return num_allocated_bytes;
}

size_t _num_meta_data_bytes()
{
    return num_allocated_blocks * sizeof(MallocMetadata);
}

size_t _size_meta_data()
{
    return sizeof(MallocMetadata);
}