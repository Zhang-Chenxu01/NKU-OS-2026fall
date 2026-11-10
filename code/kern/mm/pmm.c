#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <kmalloc.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <../sync/sync.h>
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
static void check_pgdir(void);
static void check_boot_pgdir(void);
pde_t *boot_pgdir_va = NULL;
uintptr_t boot_pgdir_pa;

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &default_pmm_manager;
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
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

// Physical page initialization carried over from Lab2.
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
        list_init(&pages[i].pra_page_link);
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
    boot_pgdir_va = satp_virtual;
    boot_pgdir_pa = satp_physical;
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);

    // Lab4 checks remain active: unfinished exercises must fail visibly.
    check_pgdir();
    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);
    check_boot_pgdir();
    kmalloc_init();
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

// get_pte - get pte and return the kernel virtual address of this pte for la
//        - if the PT contians this pte didn't exist, alloc a page for PT
// parameter:
//  pgdir:  the kernel virtual base address of PDT
//  la:     the linear address need to map
//  create: a logical value to decide if alloc a page for PT
// return vaule: the kernel virtual address of this pte
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    if (*pdep1 & (PTE_R | PTE_W | PTE_X)) {
        return NULL; // 1GiB leaf: splitting boot mappings is outside this interface.
    }
    pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    if (!(*pdep0 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    if (*pdep0 & (PTE_R | PTE_W | PTE_X)) {
        return NULL; // 2MiB leaf, not a table of 4KiB mappings.
    }
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// get_page - get related Page struct for linear address la using PDT pgdir
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL)
    {
        *ptep_store = ptep;
    }
    if (ptep != NULL && *ptep & PTE_V)
    {
        return pte2page(*ptep);
    }
    return NULL;
}

//LAB4 function1 YOUR CODE
// Integration placeholders only. Replace these bodies in Lab4 module 1.
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    panic("Lab4 TODO: page_insert (module 1)");
}

void page_remove(pde_t *pgdir, uintptr_t la)
{
    panic("Lab4 TODO: page_remove (module 1)");
}

// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    // flush_tlb();
    // The flush_tlb flush the entire TLB, is there any better way?
    asm volatile("sfence.vma %0" : : "r"(la) : "memory");
}

static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store = nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(boot_pgdir_va[0] & PTE_U);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    nr_free_store = nr_free_pages();

    // Lab3 maps the high kernel range with one Sv39 1GiB leaf.
    // The archive's int loop truncates KERNBASE and never checks this mapping.
    pte_t boot_leaf = boot_pgdir_va[PDX1(KERNBASE)];
    assert((boot_leaf & (PTE_V | PTE_R | PTE_W | PTE_X)) ==
           (PTE_V | PTE_R | PTE_W | PTE_X));
    assert(PTE_ADDR(boot_leaf) == DRAM_BASE);
    assert(PTE_ADDR(boot_leaf) + (KERNBASE & ((1UL << PDX1SHIFT) - 1)) ==
           PADDR(KERNBASE));
    assert(PDX1(KERNBASE) == PDX1(KERNTOP - 1));

    assert(boot_pgdir_va[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}
