#include <buddy_pmm.h>
#include <list.h>
#include <stdio.h>

/*
 * Linux-style buddy allocator, reduced to ucore's single global state.
 *
 * buddy_free_area[o] holds the free blocks of 2^o pages; each block is
 * represented by its head page only (PageProperty set, property == o).
 * buddy_free_area[o].nr_free counts blocks, buddy_nr_free counts pages.
 */

#define BUDDY_MAX_ORDER 10
#define BUDDY_NR_ORDERS (BUDDY_MAX_ORDER + 1)

static free_area_t buddy_free_area[BUDDY_NR_ORDERS];
static size_t buddy_nr_free;

/* Smallest order whose block (2^order pages) can hold n pages. */
static unsigned int
buddy_order_for(size_t n) {
    assert(n > 0);

    unsigned int order = 0;
    while (order < BUDDY_MAX_ORDER && (1UL << order) < n) {
        order++;
    }
    return order;
}

/*
 * Insert one aligned block and merge it with free buddies as far as possible.
 * This is shared by init_memmap (no mergeable buddy exists yet, so it only
 * inserts) and free_pages (where the merge loop does the real work).
 */
static void
buddy_free_block(struct Page *base, unsigned int order) {
    assert(base != NULL);
    assert(order <= BUDDY_MAX_ORDER);

    uintptr_t pfn = page2ppn(base);
    assert(pfn >= nbase && pfn < npage);
    assert((pfn & ((1UL << order) - 1)) == 0);
    assert(pfn + (1UL << order) <= npage);
    assert(!PageProperty(base));

    /* Count the freed pages once, at the original order. */
    buddy_nr_free += 1UL << order;

    /*
     * Merge upward while the buddy is inside [nbase, npage), is a free block
     * head and describes a block of exactly the same order.
     */
    while (order < BUDDY_MAX_ORDER) {
        uintptr_t buddy_pfn = pfn ^ (1UL << order);

        if (buddy_pfn < nbase || buddy_pfn >= npage) {
            break;
        }

        struct Page *buddy = pa2page(buddy_pfn << PGSHIFT);

        if (!PageProperty(buddy) || buddy->property != order) {
            break;
        }

        list_del(&buddy->page_link);
        ClearPageProperty(buddy);
        buddy->property = 0;
        buddy_free_area[order].nr_free--;

        pfn &= buddy_pfn;
        order++;
    }

    base = pa2page(pfn << PGSHIFT);
    assert(!PageProperty(base));
    base->property = order;
    SetPageProperty(base);
    list_add(&buddy_free_area[order].free_list, &base->page_link);
    buddy_free_area[order].nr_free++;
}

static void
buddy_init(void) {
    for (int i = 0; i < BUDDY_NR_ORDERS; i++) {
        list_init(&buddy_free_area[i].free_list);
        buddy_free_area[i].nr_free = 0;
    }
    buddy_nr_free = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(base != NULL);
    assert(n > 0);

    /* Absolute page numbers, never "page - pages". */
    uintptr_t pfn = page2ppn(base);
    uintptr_t end = pfn + n;

    assert(pfn >= nbase);
    assert(end <= npage);

    /* page_init() only reserves pages; free pages enter the allocator here. */
    for (struct Page *p = base; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    /*
     * The incoming range is neither guaranteed to be aligned to a large
     * block nor a power of two, so carve it into the largest aligned
     * power-of-two blocks that fit, from the start of the range upwards.
     */
    while (pfn < end) {
        unsigned int order = BUDDY_MAX_ORDER;

        while (order > 0 &&
               (((pfn & ((1UL << order) - 1)) != 0) ||
                (pfn + (1UL << order) > end))) {
            order--;
        }

        buddy_free_block(pa2page(pfn << PGSHIFT), order);
        pfn += 1UL << order;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    if (n == 0 || n > (1UL << BUDDY_MAX_ORDER)) {
        return NULL;
    }

    unsigned int order = buddy_order_for(n);

    /* Take the first non-empty block of at least the requested order. */
    unsigned int cur = order;
    while (cur <= BUDDY_MAX_ORDER &&
           list_empty(&buddy_free_area[cur].free_list)) {
        cur++;
    }
    if (cur > BUDDY_MAX_ORDER) {
        return NULL;
    }

    struct Page *page =
        le2page(list_next(&buddy_free_area[cur].free_list), page_link);
    list_del(&page->page_link);
    ClearPageProperty(page);
    page->property = 0;
    buddy_free_area[cur].nr_free--;

    /*
     * Split from `cur` down to `order`: the upper half of every split goes
     * back to the next lower free list, the lower half stays the block that
     * will be handed out.  Splitting never changes the page total, only the
     * block layout.
     */
    for (unsigned int o = cur; o > order; o--) {
        struct Page *half = page + (1UL << (o - 1));

        assert(!PageProperty(half));
        half->property = o - 1;
        SetPageProperty(half);
        list_add(&buddy_free_area[o - 1].free_list, &half->page_link);
        buddy_free_area[o - 1].nr_free++;
    }

    assert(!PageProperty(page));
    assert(page->property == 0);
    buddy_nr_free -= 1UL << order;
    return page;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(base != NULL);
    assert(n > 0);
    assert(n <= (1UL << BUDDY_MAX_ORDER));

    buddy_free_block(base, buddy_order_for(n));
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

/*
 * ---------------------------------------------------------------------------
 * Test suite (prompt 2, S2-S6).
 *
 * The tests observe the allocator only through its public manager interface
 * and this file's own bookkeeping; they never change the algorithm.  Every
 * test takes a snapshot first and asserts that the exact same free-area
 * layout is restored afterwards.
 * ---------------------------------------------------------------------------
 */

#define BUDDY_TEST_MAX_BLOCKS 64

struct buddy_snapshot {
    size_t nr_free;
    unsigned int blocks[BUDDY_NR_ORDERS];
};

struct buddy_block_ref {
    uintptr_t pfn;
    unsigned int order;
};

/* Independent restatement of the request rounding, used as a test oracle. */
static unsigned int
buddy_test_order(size_t n) {
    unsigned int order = 0;

    while (((size_t)1 << order) < n) {
        order++;
    }
    return order;
}

static void
buddy_test_snapshot(struct buddy_snapshot *s) {
    s->nr_free = buddy_nr_free;
    for (unsigned int o = 0; o < BUDDY_NR_ORDERS; o++) {
        s->blocks[o] = buddy_free_area[o].nr_free;
    }
}

/* Collect and validate every free block: head flag, order, alignment, range. */
static unsigned int
buddy_test_collect(struct buddy_block_ref *refs, unsigned int capacity) {
    unsigned int n = 0;

    for (unsigned int o = 0; o < BUDDY_NR_ORDERS; o++) {
        list_entry_t *head = &buddy_free_area[o].free_list;
        unsigned int count = 0;

        for (list_entry_t *le = list_next(head); le != head;
             le = list_next(le)) {
            struct Page *p = le2page(le, page_link);
            uintptr_t pfn = page2ppn(p);

            assert(PageProperty(p));
            assert(p->property == o);
            assert((pfn & ((1UL << o) - 1)) == 0);
            assert(pfn >= nbase && pfn + (1UL << o) <= npage);
            assert(n < capacity);

            refs[n].pfn = pfn;
            refs[n].order = o;
            n++;
            count++;
        }
        assert(count == buddy_free_area[o].nr_free);
    }
    return n;
}

/* Counters, per-block metadata and the page total must all agree. */
static void
buddy_test_check_structure(void) {
    struct buddy_block_ref refs[BUDDY_TEST_MAX_BLOCKS];
    unsigned int n = buddy_test_collect(refs, BUDDY_TEST_MAX_BLOCKS);
    size_t total = 0;

    for (unsigned int i = 0; i < n; i++) {
        total += (size_t)1 << refs[i].order;
    }
    assert(total == buddy_nr_free);
    assert(total == buddy_nr_free_pages());
}

/* The free blocks must tile one contiguous range: no overlap, no hole. */
static void
buddy_test_check_tiling(void) {
    struct buddy_block_ref refs[BUDDY_TEST_MAX_BLOCKS];
    unsigned int n = buddy_test_collect(refs, BUDDY_TEST_MAX_BLOCKS);
    uintptr_t next;

    for (unsigned int i = 1; i < n; i++) {
        struct buddy_block_ref key = refs[i];
        unsigned int j = i;

        while (j > 0 && refs[j - 1].pfn > key.pfn) {
            refs[j] = refs[j - 1];
            j--;
        }
        refs[j] = key;
    }

    if (n == 0) {
        return;
    }

    next = refs[0].pfn;
    for (unsigned int i = 0; i < n; i++) {
        assert(refs[i].pfn == next);
        next += (uintptr_t)1 << refs[i].order;
    }
    assert(next - refs[0].pfn == buddy_nr_free);
}

static void
buddy_test_assert_restored(const struct buddy_snapshot *s) {
    for (unsigned int o = 0; o < BUDDY_NR_ORDERS; o++) {
        assert(buddy_free_area[o].nr_free == s->blocks[o]);
    }
    assert(buddy_nr_free == s->nr_free);
    buddy_test_check_structure();
    buddy_test_check_tiling();
}

/* A freshly allocated block: no head flag, aligned, entirely in range. */
static void
buddy_test_assert_allocated(struct Page *p, size_t n) {
    unsigned int order = buddy_test_order(n);
    uintptr_t pfn = page2ppn(p);

    assert(!PageProperty(p));
    assert(p->property == 0);
    assert((pfn & ((1UL << order) - 1)) == 0);
    assert(pfn >= nbase && pfn + (1UL << order) <= npage);
    for (size_t i = 0; i < ((size_t)1 << order); i++) {
        assert(!PageProperty(p + i));
    }
}

/* S2: the layout built by init_memmap. */
static void
buddy_test_init_layout(void) {
    struct buddy_snapshot s;

    buddy_test_snapshot(&s);
    assert(s.nr_free > 0);
    buddy_test_check_structure();
    buddy_test_check_tiling();

    cprintf("[BuddyTest] detail: free pages %d\n", (int)s.nr_free);
    for (unsigned int o = 0; o < BUDDY_NR_ORDERS; o++) {
        if (s.blocks[o] != 0) {
            cprintf("[BuddyTest] detail: order %d blocks %d\n", (int)o,
                    (int)s.blocks[o]);
        }
    }

    buddy_test_assert_restored(&s);
    cprintf("[BuddyTest] init layout: PASS\n");
}

/* S3: basic allocations of 1, 2, 4 and 8 pages, then limits. */
static void
buddy_test_alloc_basic(void) {
    static const size_t want[4] = {1, 2, 4, 8};
    struct buddy_snapshot s;
    struct Page *p[4];
    size_t freed = 0;

    buddy_test_snapshot(&s);

    for (unsigned int i = 0; i < 4; i++) {
        p[i] = buddy_alloc_pages(want[i]);
        assert(p[i] != NULL);
        buddy_test_assert_allocated(p[i], want[i]);
        freed += (size_t)1 << buddy_test_order(want[i]);
    }

    for (unsigned int i = 0; i < 4; i++) {
        for (unsigned int j = i + 1; j < 4; j++) {
            uintptr_t ei = page2ppn(p[i]) +
                           ((uintptr_t)1 << buddy_test_order(want[i]));
            uintptr_t ej = page2ppn(p[j]) +
                           ((uintptr_t)1 << buddy_test_order(want[j]));

            assert(p[i] != p[j]);
            assert(page2ppn(p[i]) >= ej || page2ppn(p[j]) >= ei);
        }
    }

    assert(buddy_nr_free == s.nr_free - freed);
    buddy_test_check_structure();

    /* Release in allocation order; free/merge below covers other orders. */
    for (unsigned int i = 0; i < 4; i++) {
        buddy_free_pages(p[i], want[i]);
    }
    buddy_test_assert_restored(&s);

    assert(buddy_alloc_pages(0) == NULL);
    assert(buddy_alloc_pages((1UL << BUDDY_MAX_ORDER) + 1) == NULL);
    assert(buddy_nr_free == s.nr_free);
    buddy_test_assert_restored(&s);

    /* 5 pages round up to an 8-page (order 3) block, then merge back. */
    p[0] = buddy_alloc_pages(5);
    assert(p[0] != NULL);
    assert(buddy_nr_free == s.nr_free - 8);
    buddy_test_assert_allocated(p[0], 5);
    buddy_free_pages(p[0], 5);
    buddy_test_assert_restored(&s);

    /* The maximum order is usable end to end. */
    p[0] = buddy_alloc_pages((size_t)1 << BUDDY_MAX_ORDER);
    assert(p[0] != NULL);
    assert(buddy_nr_free == s.nr_free - ((size_t)1 << BUDDY_MAX_ORDER));
    buddy_test_assert_allocated(p[0], (size_t)1 << BUDDY_MAX_ORDER);
    buddy_free_pages(p[0], (size_t)1 << BUDDY_MAX_ORDER);
    buddy_test_assert_restored(&s);

    cprintf("[BuddyTest] alloc basic: PASS\n");
}

/* S3: one page out of the smallest available block must split recursively. */
static void
buddy_test_split(void) {
    struct buddy_snapshot s;
    unsigned int src = BUDDY_MAX_ORDER + 1;
    struct Page *head;
    struct Page *p;

    buddy_test_snapshot(&s);

    for (unsigned int o = 0; o < BUDDY_NR_ORDERS; o++) {
        if (!list_empty(&buddy_free_area[o].free_list)) {
            src = o;
            break;
        }
    }
    assert(src <= BUDDY_MAX_ORDER);

    head = le2page(list_next(&buddy_free_area[src].free_list), page_link);
    p = buddy_alloc_pages(1);
    assert(p == head);
    buddy_test_assert_allocated(p, 1);
    assert(buddy_nr_free == s.nr_free - 1);

    /* Every level below `src` gains exactly the half split down to it. */
    for (unsigned int o = 0; o < src; o++) {
        assert(buddy_free_area[o].nr_free == s.blocks[o] + 1);
    }
    assert(buddy_free_area[src].nr_free == s.blocks[src] - 1);
    for (unsigned int o = src + 1; o < BUDDY_NR_ORDERS; o++) {
        assert(buddy_free_area[o].nr_free == s.blocks[o]);
    }

    for (unsigned int o = 0; o < src; o++) {
        struct Page *half = p + ((uintptr_t)1 << o);

        assert(PageProperty(half));
        assert(half->property == o);
    }

    buddy_test_check_structure();
    cprintf("[BuddyTest] detail: split from order %d to order 0\n", (int)src);

    buddy_free_pages(p, 1);
    buddy_test_assert_restored(&s);
    cprintf("[BuddyTest] split: PASS\n");
}

/* S4: release in arbitrary order, non-power-of-two sizes, range boundaries. */
static void
buddy_test_free_merge(void) {
    static const size_t want[4] = {1, 2, 4, 8};
    static const unsigned int release[4] = {1, 3, 0, 2};
    struct buddy_snapshot s;
    struct buddy_block_ref refs[BUDDY_TEST_MAX_BLOCKS];
    struct Page *blocks[BUDDY_TEST_MAX_BLOCKS];
    unsigned int orders[BUDDY_TEST_MAX_BLOCKS];
    unsigned int count = 0;
    struct Page *p[4];
    uintptr_t lo;
    uintptr_t hi;
    int low_seen = 0;
    int high_seen = 0;

    buddy_test_snapshot(&s);

    for (unsigned int i = 0; i < 4; i++) {
        p[i] = buddy_alloc_pages(want[i]);
        assert(p[i] != NULL);
    }
    for (unsigned int i = 0; i < 4; i++) {
        buddy_free_pages(p[release[i]], want[release[i]]);
    }
    buddy_test_assert_restored(&s);

    /* alloc_pages(3) must reserve 4 pages, free_pages(p, 3) must undo it. */
    p[0] = buddy_alloc_pages(3);
    assert(p[0] != NULL);
    assert(buddy_nr_free == s.nr_free - 4);
    buddy_test_assert_allocated(p[0], 3);
    buddy_free_pages(p[0], 3);
    buddy_test_assert_restored(&s);

    /* Lowest and highest page number of the free range. */
    unsigned int nrefs = buddy_test_collect(refs, BUDDY_TEST_MAX_BLOCKS);

    assert(nrefs > 0);
    lo = refs[0].pfn;
    hi = refs[0].pfn + ((uintptr_t)1 << refs[0].order);
    for (unsigned int i = 1; i < nrefs; i++) {
        uintptr_t end = refs[i].pfn + ((uintptr_t)1 << refs[i].order);

        if (refs[i].pfn < lo) {
            lo = refs[i].pfn;
        }
        if (end > hi) {
            hi = end;
        }
    }

    /* Bottom boundary: the first two pages, freed low-half-last. */
    p[0] = buddy_alloc_pages(1);
    assert(p[0] != NULL);
    assert(page2ppn(p[0]) == lo);
    p[1] = buddy_alloc_pages(1);
    assert(p[1] != NULL);
    assert(page2ppn(p[1]) == lo + 1);
    buddy_free_pages(p[1], 1);
    buddy_free_pages(p[0], 1);
    buddy_test_assert_restored(&s);

    /*
     * Top boundary: take the whole area largest block first, check that the
     * extremes really are allocated, then give everything back in allocation
     * order (the opposite of the exhaustion test's reverse release).
     */
    for (int o = BUDDY_MAX_ORDER; o >= 0; o--) {
        struct Page *blk;

        while ((blk = buddy_alloc_pages((size_t)1 << o)) != NULL) {
            uintptr_t pfn = page2ppn(blk);
            uintptr_t end = pfn + ((uintptr_t)1 << o);

            assert(count < BUDDY_TEST_MAX_BLOCKS);
            assert(pfn >= lo && end <= hi);
            blocks[count] = blk;
            orders[count] = (unsigned int)o;
            count++;
        }
    }
    assert(buddy_nr_free == 0);

    for (unsigned int i = 0; i < count; i++) {
        uintptr_t pfn = page2ppn(blocks[i]);

        assert(pfn >= lo);
        assert(pfn + ((uintptr_t)1 << orders[i]) <= hi);
        if (pfn == lo) {
            low_seen = 1;
        }
        if (pfn + ((uintptr_t)1 << orders[i]) == hi) {
            high_seen = 1;
        }
    }
    assert(low_seen && high_seen);

    for (unsigned int i = 0; i < count; i++) {
        buddy_free_pages(blocks[i], (size_t)1 << orders[i]);
    }
    buddy_test_assert_restored(&s);

    cprintf("[BuddyTest] free/merge: PASS\n");
}

/* S5: allocate every page, fail stably, release everything. */
static void
buddy_test_exhaustion(void) {
    struct buddy_snapshot s;
    struct Page *blocks[BUDDY_TEST_MAX_BLOCKS];
    unsigned int orders[BUDDY_TEST_MAX_BLOCKS];
    unsigned int count = 0;
    unsigned int expected = 0;

    buddy_test_snapshot(&s);
    for (unsigned int o = 0; o < BUDDY_NR_ORDERS; o++) {
        expected += s.blocks[o];
    }

    for (int o = BUDDY_MAX_ORDER; o >= 0; o--) {
        struct Page *p;

        while ((p = buddy_alloc_pages((size_t)1 << o)) != NULL) {
            assert(count < BUDDY_TEST_MAX_BLOCKS);
            assert(!PageProperty(p));
            assert(p->property == 0);
            blocks[count] = p;
            orders[count] = (unsigned int)o;
            count++;
        }
    }

    assert(buddy_nr_free == 0);
    for (unsigned int o = 0; o < BUDDY_NR_ORDERS; o++) {
        assert(buddy_free_area[o].nr_free == 0);
        assert(list_empty(&buddy_free_area[o].free_list));
    }
    /* Largest block first: no split happens, one block per initial block. */
    assert(count == expected);

    assert(buddy_alloc_pages(1) == NULL);
    assert(buddy_alloc_pages((size_t)1 << BUDDY_MAX_ORDER) == NULL);
    assert(buddy_nr_free == 0);

    cprintf("[BuddyTest] detail: exhausted in %d blocks\n", (int)count);

    for (unsigned int i = count; i > 0; i--) {
        buddy_free_pages(blocks[i - 1], (size_t)1 << orders[i - 1]);
    }
    buddy_test_assert_restored(&s);
    cprintf("[BuddyTest] exhaustion: PASS\n");
}

static void
buddy_test_all(void) {
    struct buddy_snapshot s;

    buddy_test_snapshot(&s);
    buddy_test_init_layout();
    buddy_test_alloc_basic();
    buddy_test_split();
    buddy_test_free_merge();
    buddy_test_exhaustion();
    buddy_test_assert_restored(&s);

    cprintf("[BuddyTest] all: PASS\n");
    cprintf("[BuddyTest] ALL TESTS PASS, free pages = %d\n",
            (int)buddy_nr_free);
}

static void
buddy_check(void) {
    buddy_test_all();
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
