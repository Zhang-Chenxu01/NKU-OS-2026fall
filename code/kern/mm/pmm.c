#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 YOUR CODE
static void page_init(void) {
    extern char end[];
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    const uintptr_t addr_max = (uintptr_t)-1;

    /* nbase and the boot mapping require RAM to start at DRAM_BASE. */
    if (mem_base != DRAM_BASE || mem_size == 0 ||
        mem_size > addr_max - mem_base) {
        panic("page_init: invalid DTB memory base/size");
    }
    uintptr_t mem_end = mem_base + mem_size;
    if (mem_end > PHYSICAL_MEMORY_END || mem_end <= mem_base) {
        panic("page_init: DTB memory exceeds the supported physical range");
    }

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    uintptr_t kernel_pa = PADDR(KERNBASE);
    uintptr_t end_va = (uintptr_t)end;
    uintptr_t usable_end = ROUNDDOWN(mem_end, PGSIZE);
    if (kernel_pa < mem_base || kernel_pa >= usable_end ||
        end_va < KERNBASE || end_va > addr_max - (PGSIZE - 1)) {
        panic("page_init: invalid kernel address layout");
    }
    uintptr_t end_pa = PADDR(end_va);
    if (end_pa < kernel_pa || end_pa >= usable_end) {
        panic("page_init: physical memory cannot contain the kernel");
    }

    /* npage is an exclusive physical page number, not a page count. */
    npage = PPN(usable_end);
    if (npage <= nbase) {
        panic("page_init: no manageable physical pages");
    }
    size_t count = npage - nbase;
    if (count > (size_t)-1 / sizeof(struct Page)) {
        panic("page_init: Page array size overflow");
    }
    size_t descriptors_size = count * sizeof(struct Page);
    uintptr_t pages_va = ROUNDUP(end_va, PGSIZE);
    uintptr_t pages_pa = PADDR(pages_va);
    if (pages_pa >= usable_end ||
        descriptors_size > addr_max - pages_va ||
        descriptors_size > addr_max - pages_pa) {
        panic("page_init: Page array address overflow or no metadata space");
    }
    uintptr_t descriptors_end = pages_pa + descriptors_size;
    if (descriptors_end > usable_end ||
        descriptors_end > addr_max - (PGSIZE - 1)) {
        panic("page_init: physical memory cannot contain the Page array");
    }
    uintptr_t free_pa = ROUNDUP(descriptors_end, PGSIZE);
    if (free_pa >= usable_end) {
        panic("page_init: no complete free page after the Page array");
    }

    /* Keep firmware, kernel, metadata and alignment gaps reserved. */
    pages = (struct Page *)pages_va;
    memset(pages, 0, descriptors_size);
    for (size_t i = 0; i < count; i++) {
        SetPageReserved(&pages[i]);
        list_init(&pages[i].page_link);
    }

    size_t free_count = (usable_end - free_pa) >> PGSHIFT;
    init_memmap(pa2page(free_pa), free_count);

    cprintf("physical memory: [0x%016lx, 0x%016lx)\n", mem_base, mem_end);
    cprintf("page_init: offset=0x%016lx, nbase=%lu, npage=%lu\n",
            va_pa_offset, nbase, npage);
    cprintf("Page array: VA 0x%016lx, PA [0x%016lx, 0x%016lx), %lu pages\n",
            pages_va, pages_pa, descriptors_end, count);
    cprintf("free memory: PA [0x%016lx, 0x%016lx), %lu pages\n",
            free_pa, usable_end, free_count);
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
