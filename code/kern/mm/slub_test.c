#ifdef SLUB_SELFTEST

#include <slub.h>
#include <pmm.h>
#include <stdio.h>

#define SLUB_TEST_REUSE_SIZE 64
#define SLUB_TEST_REUSE_LIMIT (PGSIZE / SLUB_TEST_REUSE_SIZE + 1)
#define SLUB_TEST_MULTIPAGE_COUNT (3 * (PGSIZE / 128) + 1)

/* Temporary PMM reservations carry their own links, without extra allocation. */
struct slub_test_held_page {
    struct slub_test_held_page *next;
};

static uintptr_t
slub_test_page_address(void *ptr) {
    return ROUNDDOWN((uintptr_t)ptr, PGSIZE);
}

static void
slub_test_aligned(void *ptr) {
    assert(ptr != NULL);
    assert(((uintptr_t)ptr & 15) == 0);
}

static uint8_t
slub_test_byte(size_t offset, unsigned int tag) {
    return (uint8_t)(tag + offset * 13 + (offset >> 4));
}

static void
slub_test_fill(void *ptr, size_t size, unsigned int tag) {
    uint8_t *data = ptr;
    for (size_t i = 0; i < size; i++) {
        data[i] = slub_test_byte(i, tag);
    }
}

static void
slub_test_check(void *ptr, size_t size, unsigned int tag) {
    const uint8_t *data = ptr;
    for (size_t i = 0; i < size; i++) {
        assert(data[i] == slub_test_byte(i, tag));
    }
}

static void
slub_test_disjoint(void *a, size_t a_size, void *b, size_t b_size) {
    uintptr_t a_start = (uintptr_t)a, b_start = (uintptr_t)b;
    assert(a_size <= (uintptr_t)-1 - a_start);
    assert(b_size <= (uintptr_t)-1 - b_start);
    assert(a_start + a_size <= b_start || b_start + b_size <= a_start);
}

static void
slub_test_sizes(void) {
    static const size_t sizes[] = {
        1, 16, 24, 33, 100, 129, 500, 1024, 2048, 2049,
        PGSIZE, PGSIZE + 17, 3 * PGSIZE + 7,
    };
    void *objects[sizeof(sizes) / sizeof(sizes[0])];
    size_t count = sizeof(sizes) / sizeof(sizes[0]);
    size_t before = nr_free_pages();

    for (size_t i = 0; i < count; i++) {
        objects[i] = slub_alloc(sizes[i]);
        slub_test_aligned(objects[i]);
        for (size_t j = 0; j < i; j++) {
            slub_test_disjoint(objects[i], sizes[i], objects[j], sizes[j]);
        }
        slub_test_fill(objects[i], sizes[i], i + 1);
    }
    for (size_t i = 0; i < count; i++) {
        slub_test_check(objects[i], sizes[i], i + 1);
    }
    for (size_t i = count; i > 0; i--) {
        slub_free(objects[i - 1]);
    }
    assert(nr_free_pages() == before);
    cprintf("[SLUB] size classes, alignment and large objects: PASS\n");
}

static void
slub_test_reuse(void) {
    void *objects[SLUB_TEST_REUSE_LIMIT];
    void *overflow = NULL;
    uintptr_t first_page = 0;
    size_t count = 0, before = nr_free_pages();

    /* Discover page capacity through allocation, without reading SLUB headers. */
    for (size_t i = 0; i < SLUB_TEST_REUSE_LIMIT; i++) {
        void *ptr = slub_alloc(SLUB_TEST_REUSE_SIZE);
        slub_test_aligned(ptr);
        if (i == 0) {
            first_page = slub_test_page_address(ptr);
        }
        if (slub_test_page_address(ptr) != first_page) {
            overflow = ptr;
            break;
        }
        for (size_t j = 0; j < count; j++) {
            slub_test_disjoint(ptr, SLUB_TEST_REUSE_SIZE,
                               objects[j], SLUB_TEST_REUSE_SIZE);
        }
        objects[count] = ptr;
        slub_test_fill(ptr, SLUB_TEST_REUSE_SIZE, count + 1);
        count++;
    }
    assert(overflow != NULL && count > 1);
    assert(nr_free_pages() + 2 == before);
    slub_test_fill(overflow, SLUB_TEST_REUSE_SIZE, 0xee);
    slub_test_check(overflow, SLUB_TEST_REUSE_SIZE, 0xee);
    slub_free(overflow);
    assert(nr_free_pages() + 1 == before);

    /* The first page is full; freeing one slot leaves exactly one reusable slot. */
    size_t victim_index = count / 2;
    void *victim = objects[victim_index];
    slub_test_check(victim, SLUB_TEST_REUSE_SIZE, victim_index + 1);
    slub_free(victim);
    assert(nr_free_pages() + 1 == before);
    void *replacement = slub_alloc(SLUB_TEST_REUSE_SIZE);
    slub_test_aligned(replacement);
    assert(replacement == victim);
    assert(nr_free_pages() + 1 == before);
    objects[victim_index] = replacement;
    slub_test_fill(replacement, SLUB_TEST_REUSE_SIZE, 0xe1);

    for (size_t i = 0; i < count; i++) {
        unsigned int tag = (i == victim_index) ? 0xe1 : i + 1;
        slub_test_check(objects[i], SLUB_TEST_REUSE_SIZE, tag);
    }
    for (size_t i = 0; i < count; i++) {
        unsigned int tag = (i == victim_index) ? 0xe1 : i + 1;
        slub_test_check(objects[i], SLUB_TEST_REUSE_SIZE, tag);
        slub_free(objects[i]);
    }
    assert(nr_free_pages() == before);
    cprintf("[SLUB] object independence and slot reuse: PASS\n");
}

static void
slub_test_multiple_pages(void) {
    void *objects[SLUB_TEST_MULTIPAGE_COUNT];
    size_t before = nr_free_pages(), distinct_pages = 0;

    for (size_t i = 0; i < SLUB_TEST_MULTIPAGE_COUNT; i++) {
        objects[i] = slub_alloc(100);
        slub_test_aligned(objects[i]);
        bool new_page = 1;
        for (size_t j = 0; j < i; j++) {
            slub_test_disjoint(objects[i], 100, objects[j], 100);
            if (slub_test_page_address(objects[i]) ==
                slub_test_page_address(objects[j])) {
                new_page = 0;
            }
        }
        distinct_pages += new_page;
        slub_test_fill(objects[i], 100, i + 1);
    }
    assert(distinct_pages >= 2);
    assert(nr_free_pages() + distinct_pages == before);

    for (size_t i = 0; i < SLUB_TEST_MULTIPAGE_COUNT; i += 2) {
        slub_test_check(objects[i], 100, i + 1);
        slub_free(objects[i]);
    }
    for (size_t i = 1; i < SLUB_TEST_MULTIPAGE_COUNT; i += 2) {
        slub_test_check(objects[i], 100, i + 1);
    }
    for (size_t i = 1; i < SLUB_TEST_MULTIPAGE_COUNT; i += 2) {
        slub_test_check(objects[i], 100, i + 1);
        slub_free(objects[i]);
    }
    assert(nr_free_pages() == before);

    void *again = slub_alloc(100);
    slub_test_aligned(again);
    slub_test_fill(again, 100, 0x5a);
    slub_test_check(again, 100, 0x5a);
    slub_free(again);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] multiple pages and page reclamation: PASS (%lu pages)\n",
            distinct_pages);
}

static void
slub_test_boundaries(void) {
    size_t before = nr_free_pages();
    void *guard = slub_alloc(100);
    slub_test_aligned(guard);
    slub_test_fill(guard, 100, 0x73);
    size_t available = nr_free_pages();

    assert(slub_alloc(0) == NULL);
    slub_free(NULL);
    assert(slub_alloc((size_t)-1) == NULL);
    assert(slub_alloc((size_t)-1 - (PGSIZE - 1)) == NULL);
    assert(available > 0 && available <= (size_t)-1 / PGSIZE);
    /* The payload alone consumes all free pages; its metadata cannot fit. */
    assert(slub_alloc(available * PGSIZE) == NULL);
    assert(nr_free_pages() == available);
    slub_test_check(guard, 100, 0x73);
    slub_free(guard);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] zero size, NULL free and oversized requests: PASS\n");
}

static void
slub_test_out_of_memory(void) {
    size_t before = nr_free_pages();
    void *guard = slub_alloc(24);
    slub_test_aligned(guard);
    slub_test_fill(guard, 24, 0x91);
    size_t available = nr_free_pages(), held_count = 0;
    struct slub_test_held_page *held = NULL;
    struct Page *page;

    while ((page = alloc_page()) != NULL) {
        struct slub_test_held_page *entry =
            (struct slub_test_held_page *)(page2pa(page) + va_pa_offset);
        entry->next = held;
        held = entry;
        held_count++;
        assert(held_count <= available);
    }
    assert(held_count == available && nr_free_pages() == 0);
    assert(slub_alloc(16) == NULL);
    assert(slub_alloc(2048) == NULL);
    assert(slub_alloc(PGSIZE + 1) == NULL);
    assert(nr_free_pages() == 0);
    slub_test_check(guard, 24, 0x91);

    /* An existing slab must still provide a free slot with no free PMM pages. */
    void *spare = slub_alloc(24);
    slub_test_aligned(spare);
    slub_test_disjoint(guard, 24, spare, 24);
    slub_test_fill(spare, 24, 0xb2);
    slub_test_check(guard, 24, 0x91);
    slub_test_check(spare, 24, 0xb2);
    assert(nr_free_pages() == 0);
    slub_free(spare);
    assert(nr_free_pages() == 0);

    while (held != NULL) {
        struct slub_test_held_page *entry = held;
        held = entry->next;
        free_page(pa2page((uintptr_t)entry - va_pa_offset));
    }
    assert(nr_free_pages() == available);
    slub_test_check(guard, 24, 0x91);
    slub_free(guard);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] out of memory and failure isolation: PASS (%lu pages held)\n",
            held_count);
}

void
slub_selftest(void) {
    size_t before = nr_free_pages();
    cprintf("[SLUB] selftest begin: %lu free pages\n", before);
    slub_test_sizes();
    slub_test_reuse();
    slub_test_multiple_pages();
    slub_test_boundaries();
    slub_test_out_of_memory();
    assert(nr_free_pages() == before);
    cprintf("[SLUB] free pages restored: %lu -> %lu\n", before, nr_free_pages());
    cprintf("[SLUB] ALL TESTS PASS\n");
}

#endif /* SLUB_SELFTEST */
