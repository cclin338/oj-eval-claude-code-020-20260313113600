#include "buddy.h"
#define NULL ((void *)0)

#define MAX_RANK 16
#define PAGE_SIZE 4096
#define MAX_PAGES (128 * 1024 / 4)  // 32768 pages

// Free list for each rank
typedef struct free_block {
    struct free_block *next;
} free_block_t;

static free_block_t *free_lists[MAX_RANK + 1];
static void *base_addr;
static int total_pages;

// Tracking allocated blocks
typedef struct {
    int rank;
    int allocated;
} page_info_t;

static page_info_t page_info[MAX_PAGES];

// Helper function to get the page index from address
static int get_page_index(void *p) {
    if (p < base_addr) return -1;
    long offset = (char*)p - (char*)base_addr;
    if (offset % PAGE_SIZE != 0) return -1;
    int page_idx = offset / PAGE_SIZE;
    if (page_idx >= total_pages) return -1;
    return page_idx;
}

// Helper function to get address from page index
static void *get_page_addr(int page_idx) {
    return (char*)base_addr + page_idx * PAGE_SIZE;
}

// Helper function to get buddy page index
static int get_buddy_index(int page_idx, int rank) {
    int block_size = (1 << (rank - 1));
    return page_idx ^ block_size;
}

int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;

    // Initialize free lists
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }

    // Initialize page info
    for (int i = 0; i < pgcount; i++) {
        page_info[i].rank = 0;
        page_info[i].allocated = 0;
    }

    // Build initial free list
    // Start with the largest possible blocks
    int remaining = pgcount;
    int current_page = 0;

    for (int rank = MAX_RANK; rank >= 1; rank--) {
        int block_size = (1 << (rank - 1));
        while (remaining >= block_size && (current_page % block_size == 0)) {
            free_block_t *block = (free_block_t*)get_page_addr(current_page);
            block->next = free_lists[rank];
            free_lists[rank] = block;
            page_info[current_page].rank = rank;
            current_page += block_size;
            remaining -= block_size;
        }
    }

    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }

    // Find a free block of the requested rank or larger
    int current_rank = rank;
    while (current_rank <= MAX_RANK && free_lists[current_rank] == NULL) {
        current_rank++;
    }

    if (current_rank > MAX_RANK) {
        return ERR_PTR(-ENOSPC);
    }

    // Split larger blocks if necessary
    while (current_rank > rank) {
        free_block_t *block = free_lists[current_rank];
        free_lists[current_rank] = block->next;

        current_rank--;
        int block_size = (1 << (current_rank - 1));

        // Split into two blocks of current_rank
        // Important: add them in the right order (lower address first in list)
        free_block_t *block1 = block;
        free_block_t *block2 = (free_block_t*)((char*)block + block_size * PAGE_SIZE);

        // Add block2 first, then block1, so block1 (lower address) will be at the head
        block2->next = free_lists[current_rank];
        free_lists[current_rank] = block2;

        block1->next = free_lists[current_rank];
        free_lists[current_rank] = block1;

        // Update page info
        int page_idx1 = get_page_index(block1);
        int page_idx2 = get_page_index(block2);
        page_info[page_idx1].rank = current_rank;
        page_info[page_idx2].rank = current_rank;
    }

    // Allocate the block
    free_block_t *block = free_lists[rank];
    free_lists[rank] = block->next;

    int page_idx = get_page_index(block);
    page_info[page_idx].allocated = 1;
    page_info[page_idx].rank = rank;

    return (void*)block;
}

int return_pages(void *p) {
    if (p == NULL) {
        return -EINVAL;
    }

    int page_idx = get_page_index(p);
    if (page_idx < 0 || !page_info[page_idx].allocated) {
        return -EINVAL;
    }

    int rank = page_info[page_idx].rank;
    page_info[page_idx].allocated = 0;

    // Try to merge with buddy
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_index(page_idx, rank);

        // Check if buddy exists and is free
        if (buddy_idx < 0 || buddy_idx >= total_pages) break;
        if (page_info[buddy_idx].allocated) break;
        if (page_info[buddy_idx].rank != rank) break;

        // Remove buddy from free list
        void *buddy_addr = get_page_addr(buddy_idx);
        free_block_t **prev = &free_lists[rank];
        int found = 0;
        while (*prev != NULL) {
            if ((void*)*prev == buddy_addr) {
                *prev = (*prev)->next;
                found = 1;
                break;
            }
            prev = &((*prev)->next);
        }

        if (!found) break;

        // Merge
        if (page_idx > buddy_idx) {
            page_idx = buddy_idx;
            p = buddy_addr;
        }
        rank++;
        page_info[page_idx].rank = rank;
    }

    // Add to free list
    free_block_t *block = (free_block_t*)get_page_addr(page_idx);
    block->next = free_lists[rank];
    free_lists[rank] = block;
    page_info[page_idx].rank = rank;

    return OK;
}

int query_ranks(void *p) {
    int page_idx = get_page_index(p);
    if (page_idx < 0) {
        return -EINVAL;
    }

    return page_info[page_idx].rank;
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }

    int count = 0;
    free_block_t *block = free_lists[rank];
    while (block != NULL) {
        count++;
        block = block->next;
    }

    return count;
}
