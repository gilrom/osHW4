#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include <iostream>
#include "malloc_3.h"

using namespace std;

//Macroes
#define MAX_SIZE 100000000
#define BIN_RANGE 1024
#define HIST_SIZE 128
#define MIN_SPLIT_SIZE 128
#define LARGE_SIZE (HIST_SIZE*BIN_RANGE)

#define CHECK_SIZE(SIZE){\
    if (((SIZE) == 0) || ((SIZE) > MAX_SIZE)) return NULL;\
}

#define SBRK_META(PTR, SIZE){\
    PTR = (MallocMetadata*) sbrk(sizeof(*(PTR)) + (SIZE));\
    if((PTR) == (void*)(-1)) return NULL;\
}

#define MMAP_META(PTR, SIZE){\
    PTR = (MallocMetadata*)mmap(NULL, ((SIZE) + META_SIZE), PROT_WRITE|PROT_READ, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);\
    if((PTR) == (void*)(-1)) return NULL;\
}

typedef struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata *next;
    MallocMetadata *prev;
    MallocMetadata *next_free;
    MallocMetadata *prev_free;
} MallocMetadata;

#define META_SIZE sizeof(MallocMetadata)

//static global varubles

/*Blocks (heap) list*/
static MallocMetadata* head = NULL; //head of mem blocks list
static MallocMetadata* wilderness = NULL;//tail of list
/*Histogram*/
static MallocMetadata* histo[HIST_SIZE] = {NULL};
/*Statistics*/
static size_t num_free_blocks = 0;
static size_t num_allocated_blocks = 0;
static size_t num_free_bytes = 0;
static size_t num_allocated_bytes = 0;

//enlarges wilderness block by size
//assumes wilderness is free
//returns the wilderness or NULL if failed
MallocMetadata* _enlarge_wilderness(size_t size)
{
    if(!wilderness)
    {
        return NULL;
    }
    else
    {
        //increase sbrk
        MallocMetadata* ptr;
        SBRK_META(ptr, size - META_SIZE); //no need for extra meta
        num_allocated_bytes += size;
        wilderness->size += size;
    }
    return wilderness;
}


/**
 * @brief 
 * adds free block to free list (unfree)
 * @param to_add
 * block to add
 */
void _add_free_block(MallocMetadata* to_add)
{
    if(!to_add)
        return;
    num_free_blocks++;
    num_free_bytes += to_add->size;
    to_add->is_free = true;
    int i = to_add->size / BIN_RANGE; //index in histogram
    if(!histo[i])//first insertion
    {
        to_add->next_free = NULL;
        to_add->prev_free = NULL;
        histo[i] = to_add;
    }
    else//find place
    {
        MallocMetadata* curr = histo[i];
        while (curr != NULL)
        {
            if(curr->size > to_add->size)
            {
                to_add->prev_free = curr->prev_free;
                to_add->next_free = curr;
                curr->prev_free = to_add;
                if(curr == histo[i])//if head
                    histo[i] = to_add;//change head
                return;        
            }
            else if (curr->next_free == NULL)
            {
                to_add->prev_free = curr;
                curr->next_free = to_add;
                to_add->next_free = NULL;
                return;
            }
            curr = curr->next_free;
        }
    }
}

/**
 * @brief 
 * delete free block from free list (unfree)
 * @param to_del
 * block to delete
 * @param size optional param for wilderness block only
 */
void _delete_free_block(MallocMetadata* to_del, size_t size = 0)
{
    if(!to_del)
        return;
    num_free_blocks--;
    int i;
    if(size)
    {
        i = size / BIN_RANGE;
        num_free_bytes -= size;
    }
    else
    {
        i = to_del->size / BIN_RANGE;
        num_free_bytes -= to_del->size;
    }
    to_del->is_free = false;
    if(to_del->prev_free == NULL)//if head
    {
        if(to_del->next_free)
            to_del->next_free->prev_free = NULL;
        histo[i] = to_del->next_free;
        to_del->next_free = NULL;
        to_del->prev_free = NULL;
        return;
    }
    to_del->prev_free->next_free = to_del->next_free;
    if(to_del->next_free)
        to_del->next_free->prev_free = to_del->prev_free;
    to_del->next_free = NULL;
    to_del->prev_free = NULL;
}

/**
 * @brief 
 * try to split block to data block and free block
 * adds the free part to free list
 * @param blk
 * block to split
 * @param alloc_size size of allocated part
 */
void _try_split(MallocMetadata* blk, size_t alloc_size)
{
    if(!blk)
        return;
    if(blk->size - alloc_size >= MIN_SPLIT_SIZE + META_SIZE)
    {
        MallocMetadata* free_part = (MallocMetadata*)((char*)blk + META_SIZE + alloc_size);
        free_part->next = blk->next;
        free_part->prev = blk;
        free_part->size = blk->size - alloc_size - META_SIZE;
        free_part->is_free = true;
        if(blk == wilderness)
            wilderness = free_part;
        blk->next = free_part;
        blk->size = alloc_size;
        num_allocated_blocks++;
        num_allocated_bytes -= META_SIZE;
        _add_free_block(free_part);
    }
}

/**
 * @brief 
 * try to merge adjacent lower free block
 * assumes the given blk is not yet in free list (in histogram)
 * @param blk pointer to the block address (the adress changes if merge happens)
 * @return true if block was merged false otherwise
 */
bool _try_merge_lower(MallocMetadata** blk)
{
    if((*blk)->prev != NULL)
    {
        bool is_free = (*blk)->is_free;
        if ((*blk)->prev->is_free)
        {
            //remove block from histogram
            _delete_free_block((*blk)->prev);
            //merge blocks
            (*blk)->prev->next = (*blk)->next;
            if((*blk)->next)
                (*blk)->next->prev = (*blk)->prev;
            (*blk)->prev->size += META_SIZE + (*blk)->size;
            (*blk) = (*blk)->prev;
            if((*blk)->next == NULL)
                wilderness = (*blk);
            (*blk)->is_free = is_free;
            num_allocated_blocks--;
            num_allocated_bytes += META_SIZE;
            return true;
        }
    }
    return false;
}

/**
 * @brief 
 * try to merge adjacent upper free block
 * assumes the given blk is not yet in free list (in histogram)
 * @param blk block address
 * @return true if block was merged false otherwise
 */
bool _try_merge_upper(MallocMetadata* blk)
{
    if(blk->next != NULL)
    {
        bool is_free = blk->is_free;
        if(blk->next->is_free)
        {
            _delete_free_block(blk->next);
            blk->size += blk->next->size + META_SIZE;
            blk->next = blk->next->next;
            if(blk->next == NULL)
                wilderness = blk;
            else
                blk->next->prev = blk;
            blk->is_free = is_free;
            num_allocated_blocks--;
            num_allocated_bytes += META_SIZE;
            return true;
        }
    }
    return false;
}

/**
 * @brief 
 * try to merge adjacent free blocks
 * assumes the given blk is not yet in free list (in histogram)
 * @param blk
 * free block to try merging
 * @return Merged Block
 */
MallocMetadata* _try_merge(MallocMetadata* blk)
{
    if(!blk) return blk;
    bool merged = false;
    merged = _try_merge_lower(&blk); //by refrence so blk will change
    merged = merged || _try_merge_upper(blk);
    if(merged)
    {
        return _try_merge(blk);//recursive call
    }
    return blk;
}

/**
 * @brief 
 * find the first free block with adequite size
 * @param size 
 * required allocation size
 * @return the free block, NULL if not found
 */
MallocMetadata* _get_free_block(size_t size)
{
    MallocMetadata* curr;
    for (int i = size / BIN_RANGE; i < HIST_SIZE; i++)
    {
        curr = histo[i];
        while(curr != NULL){
            if(curr->size >= size)
                return curr;
            curr = curr->next_free;
        }
    }
    return NULL;
}

void* smalloc(size_t size)
{
    CHECK_SIZE(size);
    if(size >= LARGE_SIZE)//large block to be mapped
    {
        MallocMetadata* ptr;
        MMAP_META(ptr, size);
        ptr->size = size;
        num_allocated_blocks++;
        num_allocated_bytes += size;
        return ptr + 1;
    }
    MallocMetadata* blk = _get_free_block(size);
    if(blk)
    {
        blk->is_free = false;
        num_free_blocks--;
        num_free_bytes -= blk->size;
        _try_split(blk, size);
        return blk + 1;
    }
    else//no free block found
    {
        if(wilderness)
        {
            if(wilderness->is_free)
            {
                size_t old_size = wilderness->size;
                if(_enlarge_wilderness((size - old_size)))
                {
                    wilderness->is_free = false;
                    _delete_free_block(wilderness, old_size);
                    return wilderness+1;
                }
                return NULL;
            }
            //creates new block
            SBRK_META(blk, size);
            blk->prev = wilderness;
            blk->next = NULL;
            blk->size = size;
            blk->is_free = false;
            wilderness->next = blk;
            wilderness = blk;
            num_allocated_blocks++;
            num_allocated_bytes += size;
            return blk + 1;
        }
        else //first allocatiom
        {
            SBRK_META(head, size);
            head->is_free = false;
            head->size = size;
            head->next = NULL;
            head->prev = NULL;
            wilderness = head;
            num_allocated_blocks++;
            num_allocated_bytes += size;
            return head + 1;
        }
    }
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
    if (meta->size >= LARGE_SIZE)//mapped block
    {
        num_allocated_blocks--;
        num_allocated_bytes -= meta->size;
        munmap(meta, meta->size + META_SIZE);
        return;
    }
    meta->is_free = true;
    meta = _try_merge(meta);
    _add_free_block(meta);
}

void* srealloc(void* oldp, size_t size)
{
    CHECK_SIZE(size);
    if(!oldp)
        return smalloc(size);
    MallocMetadata* old_meta = ((MallocMetadata*)oldp) - 1;
    if(old_meta->size >= size)// a
    {
        if(size >= LARGE_SIZE)//mapped
        {
            munmap((char*)oldp+size, old_meta->size - size);
            num_allocated_bytes -= (old_meta->size - size);
            old_meta->size = size;
            return oldp;
        }
        _try_split(old_meta, size);
        return oldp;
    }
    if(old_meta->size >= LARGE_SIZE)//mapped block
    {
        MallocMetadata* ptr;
        MMAP_META(ptr, size);
        num_allocated_bytes += size - old_meta->size;
        memcpy(ptr+1, oldp, old_meta->size);
        ptr->size = size;
        munmap(old_meta, old_meta->size + META_SIZE);
        return ptr + 1;
    }
    if(old_meta == wilderness) //wilderness case
    {
        MallocMetadata* to_ret;
        _try_merge(wilderness);
        if(wilderness != old_meta)
        {
            if(wilderness->size >= size)
            {
                memmove(wilderness+1, oldp, old_meta->size);
                to_ret = wilderness;
                _try_split(wilderness, size);
                return to_ret + 1;
            }
        }
        _enlarge_wilderness(size - wilderness->size);
        memmove(wilderness+1, oldp, old_meta->size);
        return wilderness + 1;
    }
    if(old_meta->prev)//b
    {
        if (old_meta->prev->is_free && 
        (old_meta->prev->size + META_SIZE + old_meta->size) >= size)
        {
            MallocMetadata* new_blk = old_meta;
            _try_merge_lower(&new_blk); // merges(not just try)
            memmove(new_blk+1, oldp, old_meta->size);
            _try_split(new_blk, size);
            return new_blk + 1;
        }
    }
    if(old_meta->next)//c
    {
        if (old_meta->next->is_free && 
        (old_meta->next->size + META_SIZE + old_meta->size) >= size)
        {
            MallocMetadata* new_blk = old_meta;
            _try_merge_upper(new_blk); // merges(not just try)
            _try_split(new_blk, size);
            return new_blk + 1;
        }
    }
    if(old_meta->next && old_meta->prev) //d
    {
        if (old_meta->next->is_free && old_meta->prev->is_free &&
        (old_meta->next->size + META_SIZE + old_meta->prev->size + META_SIZE + old_meta->size) >= size)
        {
            MallocMetadata* new_blk = old_meta;
            _try_merge_lower(&new_blk); // merges(not just try)
            _try_merge_upper(new_blk); // merges(not just try)
            memmove(new_blk+1, oldp, old_meta->size);
            _try_split(new_blk, size);
            return new_blk + 1;
        }
    }
    void* newp = smalloc(size); //e-f
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


#define MAX_ALC 23
int main()
{
    int default_size = 2240;
    void* m = smalloc(1);
    sfree(m);
    void* g [MAX_ALC];
    for (int i=0; i<4; ++i)
    {
        g[i] = smalloc(default_size);

    }
    sfree(g[0]);
    sfree(g[2]);
    srealloc(g[3], default_size *3);
    // void* tmp = smalloc(default_size / 3);
    // tmp = srealloc(g[4], default_size + default_size / 3);
    cout << num_allocated_blocks;
}