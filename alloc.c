#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>


typedef struct meta_data {
    struct meta_data *next_struct;
    struct meta_data *prev_struct;
    size_t size_struct;
    char free_struct;
} pooky;

static int count_ops = 0;

static pooky* hf[16];
static pooky* tf[16];

int determine_list_index(size_t size);
void *allocate_and_copy(void *ptr, size_t old_size, size_t new_size);
void reintegrate(pooky* n);
void remove_from_free(pooky* n, int ln);
int try_merge_and_reuse(void *ptr, size_t required_size);
void adjust_heap_or_recycle(pooky *b, void *ptr);
void consolidate_adj(pooky* n);
void split(pooky* one, size_t size_updated);
pooky* initial_f(size_t size, int ln);
pooky* optimal(size_t size, int ln);

void *calloc(size_t num, size_t size) {
    size_t total_size = num * size;
    if (total_size == 0) {
        return NULL;
    }
    void *ptr_temp = malloc(total_size);
    if (!ptr_temp) {
        return NULL;
    }
    size_t *ptr_as_sizet = (size_t *)ptr_temp;
    size_t num_sizet = total_size / sizeof(size_t);
    for (size_t i = 0; i < num_sizet; i++) {
        ptr_as_sizet[i] = 0;
    }
    for (size_t i = num_sizet * sizeof(size_t); i < total_size; i++) {
        ((char *)ptr_temp)[i] = 0;
    }
    return ptr_temp;
}


void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    int ln = determine_list_index(size);
    pooky *b = NULL;

    if (ln == 15 || (ln == 14 && ++count_ops > 6)) {
        pooky* iterator = hf[ln];
        void* heap_end = sbrk(0);
        while (iterator) {
            if (iterator->free_struct) {
                consolidate_adj(iterator);
            }
            char* end_of_current = (char*)iterator + sizeof(pooky) + iterator->size_struct;
            if ((void*)end_of_current >= heap_end) {
                remove_from_free(iterator, ln);
                sbrk(-(iterator->size_struct + sizeof(pooky)));
                break;
            }
            iterator = iterator->next_struct;
        }
        count_ops = 0;
    }

    if ((b = hf[ln])) {
        b = determine_list_index(size) >= 5 ? optimal(size, ln) : initial_f(size, ln);
        if (b) {
            remove_from_free(b, ln);
            b->free_struct = 0;
            split(b, size);
            return (void *)(b + 1);
        }
    }

    void *allocated_space = sbrk(0);
    if (sbrk(sizeof(pooky) + size) == (void*) -1) {
        return NULL;
    }
    b = (pooky*) allocated_space;
    b->prev_struct = NULL;
    b->next_struct = NULL;
    b->free_struct = 0;
    b->size_struct = size;
    return (void *)(b + 1);

}


void free(void *ptr) {
    // implement free!
    if (!ptr) {
        return;
    }
    if (ptr >= sbrk(0)) {
        return;
    }
    pooky *b = (pooky *)ptr-1;
    if (b->free_struct) {
        return;
    }
    consolidate_adj(b);
    adjust_heap_or_recycle(b, ptr);
}



void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    pooky *meta = (pooky *)((char *)ptr-sizeof(pooky));
    if (size <= meta->size_struct) {
        split(meta, size);
        return ptr;
    } else if (try_merge_and_reuse(ptr, size)) {
        return ptr;
    } else {
        return allocate_and_copy(ptr, meta->size_struct, size);
    }
}


int determine_list_index(size_t size) {
    size_t many_sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072};
    int left = 0, right = 14, mid;
    while (left <= right) {
        mid = left + (right-left)/2;
        if (size > many_sizes[mid]) {
            left = mid + 1;
        } else if (mid > 0 && size <= many_sizes[mid-1]) {
            right = mid - 1;
        } else {
            return mid;
        }
    }
    return 15;
}

int try_merge_and_reuse(void *ptr, size_t required_size) {
    pooky *meta = (pooky *)((char *)ptr-sizeof(pooky));
    consolidate_adj(meta);
    return meta->size_struct >= required_size;
}

void *allocate_and_copy(void *ptr, size_t old_size, size_t new_size) {
    void *new_ptr = malloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
        free(ptr);
    }
    return new_ptr;
}

void reintegrate(pooky* n) {
    if (!n) {
        return;
    }
    int list_index = determine_list_index(n->size_struct);
    pooky** head = &hf[list_index];
    pooky** tail = &tf[list_index];

    n->prev_struct = NULL;
    n->next_struct = *head;

    if (*head) {
        (*head)->prev_struct = n;
    } else {
        *tail = n;
    }
    *head = n;
}


void remove_from_free(pooky* n, int ln) {
    if (n == hf[ln]) {
        hf[ln] = n->next_struct;
        if (hf[ln]) {
            hf[ln]->prev_struct = NULL;
        } else {
            tf[ln] = NULL;
        }
    } else if (n == tf[ln]) {
        tf[ln] = n->prev_struct;
        tf[ln]->next_struct = NULL;
    } else {
        n->prev_struct->next_struct = n->next_struct;
        n->next_struct->prev_struct = n->prev_struct;
    }
    n->prev_struct = NULL;
    n->next_struct = NULL;
}

void consolidate_adj(pooky* n) {
    char* next_block_ptr = (char*)n + sizeof(pooky) + n->size_struct;
    pooky* potential_next_block = (pooky*)next_block_ptr;
    void* heap_end = sbrk(0);
    if ((void*)potential_next_block >= heap_end) {
        return;
    }
    if (potential_next_block->free_struct) {
        int free_list_index = determine_list_index(potential_next_block->size_struct);
        remove_from_free(potential_next_block, free_list_index);
        n->size_struct += sizeof(pooky)+potential_next_block->size_struct;
    }
}

void adjust_heap_or_recycle(pooky *b, void *ptr) {
    if (determine_list_index(b->size_struct) >= 5 && (char *)ptr + b->size_struct >= (char *)sbrk(0)) {
        sbrk(-(b->size_struct + sizeof(pooky)));
    } else {
        b->free_struct = 1;
        reintegrate(b);
    }
}

void split(pooky* one, size_t size_updated) {
    if (one == NULL) {
        return;
    }
    size_t total_extra_space = one->size_struct - size_updated;
    size_t required_extra_space = sizeof(pooky)+4;
    if (total_extra_space > required_extra_space) {
        pooky* nb = (pooky*)((char*)(one+1)+size_updated);
        nb->size_struct = total_extra_space - sizeof(pooky);
        nb->free_struct = 1;
        reintegrate(nb);
        one->size_struct = size_updated;
    }
}

pooky* initial_f(size_t size, int ln) {
    for (pooky* block = hf[ln]; block; block = block->next_struct) {
        bool check = block->size_struct >= size;
        if (check) {
            return block;
        }
    }
    return NULL;
}

pooky* optimal(size_t size, int ln) {
    pooky* candidate = NULL;
    size_t smallest_diff = 100000000000000000;
    pooky* iter = hf[ln];
    while (iter != NULL) {
        if (iter->size_struct >= size) {
            size_t current_diff = iter->size_struct-size;
            if (candidate == NULL || current_diff < smallest_diff) {
                candidate = iter;
                smallest_diff = current_diff;
            }
        }
        iter = iter->next_struct;
    }
    return candidate;
}
