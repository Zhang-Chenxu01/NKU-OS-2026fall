#include <slub.h>
#include <pmm.h>
#include <list.h>

#define SLUB_ALIGNMENT 16
#define SLUB_NR_CLASSES 8
#define SLUB_MAGIC 0x534c554250414745ULL

struct slub_free_object {
    struct slub_free_object *next;
};

struct slub_cache {
    size_t object_size;
    /* Only pages with available slots are linked here. */
    list_entry_t partial;
};

/* Stored at the start of the first page, outside the object/payload area. */
struct slub_page {
    uint64_t magic;
    struct slub_cache *cache;  /* NULL denotes a large allocation. */
    size_t nr_pages;
    size_t inuse;
    size_t capacity;
    struct slub_free_object *free;
    list_entry_t page_link;
};

#define SLUB_HEADER_SIZE \
    ((sizeof(struct slub_page) + SLUB_ALIGNMENT - 1) & \
     ~(size_t)(SLUB_ALIGNMENT - 1))

static struct slub_cache slub_caches[SLUB_NR_CLASSES];
static bool slub_initialized;

static void *
slub_page_data(struct slub_page *slab) {
    return (char *)slab + SLUB_HEADER_SIZE;
}

static struct slub_page *
slub_alloc_page_block(struct slub_cache *cache, size_t nr_pages) {
    struct Page *page = alloc_pages(nr_pages);
    if (page == NULL) {
        return NULL;
    }

    struct slub_page *slab =
        (struct slub_page *)(page2pa(page) + va_pa_offset);
    slab->magic = SLUB_MAGIC;
    slab->cache = cache;
    /* Pass the same count back to free_pages; the PMM handles order rounding. */
    slab->nr_pages = nr_pages;
    slab->inuse = 0;
    slab->capacity = 0;
    slab->free = NULL;
    list_init(&slab->page_link);
    return slab;
}

static void
slub_release_page_block(struct slub_page *slab) {
    struct Page *page = pa2page((uintptr_t)slab - va_pa_offset);
    size_t nr_pages = slab->nr_pages;
    slab->magic = 0;
    free_pages(page, nr_pages);
}

static struct slub_page *
slub_new_small_page(struct slub_cache *cache) {
    struct slub_page *slab = slub_alloc_page_block(cache, 1);
    if (slab == NULL) {
        return NULL;
    }

    slab->capacity = (PGSIZE - SLUB_HEADER_SIZE) / cache->object_size;
    char *data = slub_page_data(slab);
    /* Store next pointers only in free slots, in ascending address order. */
    for (size_t i = slab->capacity; i > 0; i--) {
        struct slub_free_object *object =
            (struct slub_free_object *)(data + (i - 1) * cache->object_size);
        object->next = slab->free;
        slab->free = object;
    }
    list_add(&cache->partial, &slab->page_link);
    return slab;
}

static void *
slub_alloc_large(size_t size) {
    /* Include the header and page rounding without overflowing size_t. */
    if (size > (size_t)-1 - SLUB_HEADER_SIZE - (PGSIZE - 1)) {
        return NULL;
    }
    size_t nr_pages = (size + SLUB_HEADER_SIZE + PGSIZE - 1) >> PGSHIFT;
    if (nr_pages > nr_free_pages()) {
        return NULL;
    }

    struct slub_page *slab = slub_alloc_page_block(NULL, nr_pages);
    if (slab == NULL) {
        return NULL;
    }
    slab->inuse = slab->capacity = 1;
    return slub_page_data(slab);
}

void
slub_init(void) {
    if (slub_initialized) {
        return;
    }

    static_assert(SLUB_HEADER_SIZE +
                  (SLUB_ALIGNMENT << (SLUB_NR_CLASSES - 1)) <= PGSIZE);
    static_assert(sizeof(struct slub_free_object) <= SLUB_ALIGNMENT);
    assert((va_pa_offset & (PGSIZE - 1)) == 0);
    for (size_t i = 0; i < SLUB_NR_CLASSES; i++) {
        slub_caches[i].object_size = (size_t)SLUB_ALIGNMENT << i;
        list_init(&slub_caches[i].partial);
    }
    slub_initialized = 1;
}

void *
slub_alloc(size_t size) {
    assert(slub_initialized);
    if (size == 0) {
        return NULL;
    }

    struct slub_cache *cache = NULL;
    for (size_t i = 0; i < SLUB_NR_CLASSES; i++) {
        if (size <= slub_caches[i].object_size) {
            cache = &slub_caches[i];
            break;
        }
    }
    if (cache == NULL) {
        return slub_alloc_large(size);
    }

    struct slub_page *slab;
    if (list_empty(&cache->partial)) {
        slab = slub_new_small_page(cache);
        if (slab == NULL) {
            return NULL;
        }
    } else {
        slab = to_struct(list_next(&cache->partial), struct slub_page, page_link);
    }

    assert(slab->free != NULL && slab->inuse < slab->capacity);
    struct slub_free_object *object = slab->free;
    slab->free = object->next;
    slab->inuse++;
    if (slab->free == NULL) {
        list_del_init(&slab->page_link);
    }
    return object;
}

void
slub_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    assert(slub_initialized);

    /* Every returned address starts in the block's first physical page. */
    struct slub_page *slab =
        (struct slub_page *)ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    assert(slab->magic == SLUB_MAGIC);
    assert(slab->inuse > 0);
    if (slab->cache == NULL) {
        assert(ptr == slub_page_data(slab));
        slub_release_page_block(slab);
        return;
    }

    struct slub_cache *cache = slab->cache;
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)slub_page_data(slab);
    assert((uintptr_t)ptr >= (uintptr_t)slub_page_data(slab));
    assert(offset % cache->object_size == 0);
    assert(offset / cache->object_size < slab->capacity);

    bool was_full = (slab->free == NULL);
    struct slub_free_object *object = ptr;
    object->next = slab->free;
    slab->free = object;
    slab->inuse--;

    if (slab->inuse == 0) {
        /* Full pages have a self-linked node; both cases can be unlinked. */
        list_del_init(&slab->page_link);
        slub_release_page_block(slab);
    } else if (was_full) {
        list_add(&cache->partial, &slab->page_link);
    }
}
