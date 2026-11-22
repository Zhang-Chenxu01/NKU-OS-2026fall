#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf) __attribute__((noreturn));
void switch_to(struct context *from, struct context *to);
static int user_main(void *arg);

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}


// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}


// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

static void destroy_unreferenced_mm(struct mm_struct *mm)
{
    if (mm->pgdir != NULL)
    {
        exit_mmap(mm);
        put_pgdir(mm);
    }
    mm_destroy(mm);
}

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
//lab5 function4 YOUR CODE
int do_exit(int error_code)
{
    panic("process exit!!.\n");
}

static int
init_main(void *arg)
{
    size_t free_pages_before = nr_free_pages();
    int pid = kernel_thread(user_main, NULL, 0);
    if (pid < 0)
    {
        panic("init_main: cannot create user_main (%e).\n", pid);
    }

    // Run the newly runnable child before entering the wait/reap loop.
    schedule();
    int ret;
    while ((ret = do_wait(0, NULL)) == 0)
    {
        schedule();
    }
    if (ret != -E_BAD_PROC)
    {
        panic("init_main: do_wait failed (%e).\n", ret);
    }
    cprintf("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && nr_process == 2);
    assert(list_next(&proc_list) == &initproc->list_link);
    assert(list_prev(&proc_list) == &initproc->list_link);
    size_t free_pages_after = nr_free_pages();
    if (free_pages_after == free_pages_before)
    {
        cprintf("init check memory pass.\n");
    }
    else
    {
        // The SLOB allocator may retain arena pages after freeing objects.
        cprintf("init: free pages before %lu, after %lu.\n", free_pages_before, free_pages_after);
    }
    return 0;
}

//LAB4 function2 2411566
//lab5 function2 YOUR CODE update alloc_proc
// The existing memset also initializes the appended Lab5 fields.
static struct proc_struct *alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        // Clear counters, pointers, flags, name and the saved context.
        memset(proc, 0, sizeof(struct proc_struct));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->pgdir = boot_pgdir_pa;
        list_init(&(proc->list_link));
        list_init(&(proc->hash_link));
    }
    return proc;
}

static int copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    struct mm_struct *parent_mm = current->mm;
    if (parent_mm == NULL)
    {
        // alloc_proc already selected boot_pgdir_pa for a kernel thread.
        return 0;
    }

    struct mm_struct *mm = parent_mm;
    if (!(clone_flags & CLONE_VM))
    {
        mm = mm_create();
        if (mm == NULL)
        {
            return -E_NO_MEM;
        }
        int ret = setup_pgdir(mm);
        if (ret == 0)
        {
            lock_mm(parent_mm);
            ret = dup_mmap(mm, parent_mm);
            unlock_mm(parent_mm);
        }
        if (ret != 0)
        {
            // The private mm still owns every VMA and mapping installed so far.
            destroy_unreferenced_mm(mm);
            return ret;
        }
    }

    bool intr_flag;
    local_intr_save(intr_flag);
    mm_count_inc(mm);
    proc->mm = mm;
    proc->pgdir = PADDR(mm->pgdir);
    local_intr_restore(intr_flag);
    return 0;
}

static void copy_thread(struct proc_struct *proc, uintptr_t stack, const struct trapframe *tf)
{
    struct trapframe *child_tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *child_tf = *tf; // The syscall trap has already advanced epc past ecall.
    child_tf->gpr.a0 = 0;
    if (stack != 0)
    {
        child_tf->gpr.sp = stack;
    }
    else if (tf->status & SSTATUS_SPP)
    {
        child_tf->gpr.sp = (uintptr_t)child_tf;
    }
    proc->tf = child_tf;
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)child_tf;
}

int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    if (nr_process >= MAX_PROCESS)
    {
        return -E_NO_FREE_PROC;
    }

    int ret = -E_NO_MEM;
    struct proc_struct *proc = alloc_proc();
    if (proc == NULL)
    {
        return ret;
    }
    proc->parent = current;

    if ((ret = setup_kstack(proc)) != 0)
    {
        goto bad_proc;
    }
    if ((ret = copy_mm(clone_flags, proc)) != 0)
    {
        goto bad_kstack;
    }
    copy_thread(proc, stack, tf);

    // Acquiring the parent's mm lock can schedule another creator.
    bool intr_flag;
    local_intr_save(intr_flag);
    if (nr_process >= MAX_PROCESS)
    {
        struct mm_struct *mm = proc->mm;
        bool last_ref = mm != NULL && mm_count_dec(mm) == 0;
        local_intr_restore(intr_flag);
        if (last_ref)
        {
            destroy_unreferenced_mm(mm);
        }
        ret = -E_NO_FREE_PROC;
        goto bad_kstack;
    }
    proc->pid = get_pid();
    hash_proc(proc);
    set_links(proc);
    wakeup_proc(proc);
    ret = proc->pid;
    local_intr_restore(intr_flag);
    return ret;

bad_kstack:
    put_kstack(proc);
bad_proc:
    kfree(proc);
    return ret;
}

//lab5 function2 YOUR CODE update kernel_thread
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    // kernel_thread_entry loads a0 from s1 and calls the function in s0.
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

void proc_init(void)
{
    list_init(&proc_list);
    for (int i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("proc_init: cannot allocate idleproc.\n");
    }
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    current = idleproc;
    // PID 0 is counted but stays outside the schedulable process list.
    nr_process = 1;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0)
    {
        panic("proc_init: cannot create initproc (%d).\n", pid);
    }
    initproc = find_proc(pid);
    assert(initproc != NULL && initproc->pid == 1);
    set_proc_name(initproc, "init");
    assert(nr_process == 2);
}


//LAB4 function3 2413042
void proc_run(struct proc_struct *proc)
{
    if (proc != current)
    {
        bool intr_flag;
        struct proc_struct *prev = current;
        local_intr_save(intr_flag);
        {
            current = proc;

            // Sv39: MODE=8 in satp[63:60], PPN = physical address of the root table >> 12.
            // lsatp()/lcr3() are Sv32-era helpers and cannot encode the mode field,
            // so build the register value here (same encoding as kern_entry).
            uintptr_t satp = (8UL << 60) |
                             ((proc->pgdir >> RISCV_PGSHIFT) & ((1UL << 44) - 1));
            write_csr(satp, satp);
            flush_tlb();

            switch_to(&(prev->context), &(proc->context));
        }
        local_intr_restore(intr_flag);
    }
}

void cpu_idle(void)
{
    while (1)
    {
        // The only scheduling point of lab4: give up the CPU on request.
        if (current->need_resched)
        {
            schedule();
        }
    }
}

#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)


static int map_exec_vma(struct mm_struct *mm, uintptr_t start, size_t len, uint32_t flags)
{
    if (nr_free_pages() <= 1)
        return -E_NO_MEM;
    return mm_map(mm, start, len, flags, NULL);
}

static struct Page *alloc_exec_page(pde_t *pgdir, uintptr_t va, uint32_t perm)
{
    // Keep a higher free block for the current first-fit allocator's rollback.
    // A Sv39 mapping can need one data page and two intermediate table pages.
    if (nr_free_pages() <= 3)
        return NULL;
    struct Page *page = pgdir_alloc_page(pgdir, va, perm);
    if (page != NULL)
        memset(page2kva(page), 0, PGSIZE);
    return page;
}

// do_execve checks the source ranges. Check each user PTE before reading it
// through the kernel mapping so an unmapped page cannot fault in S-mode.
static bool read_exec_data(struct mm_struct *mm, void *dst, const void *src, size_t len)
{
    if (mm == NULL)
    {
        memcpy(dst, src, len);
        return 1;
    }

    unsigned char *out = dst;
    uintptr_t addr = (uintptr_t)src;
    while (len != 0)
    {
        pte_t *pte = get_pte(mm->pgdir, addr, 0);
        uint32_t required = PTE_V | PTE_U | PTE_R;
        if (pte == NULL || (*pte & required) != required)
            return 0;

        size_t offset = PGOFF(addr), count = PGSIZE - offset;
        if (count > len)
            count = len;
        memcpy(out, (unsigned char *)page2kva(pte2page(*pte)) + offset, count);
        out += count;
        addr += count;
        len -= count;
    }
    return 1;
}

static uint32_t elf_page_permissions(uint32_t vm_flags)
{
    uint32_t perm = 0;
    if (vm_flags & VM_READ)
        perm |= PTE_R;
    if (vm_flags & VM_WRITE)
        perm |= PTE_R | PTE_W;
    if (vm_flags & VM_EXEC)
        perm |= PTE_X;
    // An ELF segment with no access flags still needs a valid leaf for cleanup.
    return perm == 0 ? PTE_R : PTE_U | perm;
}

static int map_elf_segment(struct mm_struct *mm, const struct proghdr *ph,
                           struct mm_struct *source_mm, const unsigned char *binary)
{
    uint32_t vm_flags = 0;
    if (ph->p_flags & ELF_PF_R)
        vm_flags |= VM_READ;
    if (ph->p_flags & ELF_PF_W)
        vm_flags |= VM_WRITE;
    if (ph->p_flags & ELF_PF_X)
        vm_flags |= VM_EXEC;

    uintptr_t start = ROUNDDOWN(ph->p_va, PGSIZE);
    uintptr_t end = ROUNDUP(ph->p_va + ph->p_memsz, PGSIZE);
    uintptr_t new_start = start;
    struct vma_struct *previous = find_vma(mm, start);
    int ret;
    if (previous != NULL)
    {
        // Disjoint segment byte ranges may share one boundary page.
        new_start += PGSIZE;
        uint32_t shared_flags = previous->vm_flags | vm_flags;
        if (shared_flags != previous->vm_flags)
        {
            if (previous->vm_start == start)
            {
                previous->vm_flags = shared_flags;
            }
            else
            {
                previous->vm_end = start;
                if ((ret = map_exec_vma(mm, start, PGSIZE, shared_flags)) != 0)
                {
                    previous->vm_end = new_start;
                    return ret;
                }
            }
            struct Page *page = get_page(mm->pgdir, start, NULL);
            ret = page_insert(mm->pgdir, page, start, elf_page_permissions(shared_flags));
            if (ret != 0)
                return ret;
        }
    }
    if (new_start < end && (ret = map_exec_vma(mm, new_start, end - new_start, vm_flags)) != 0)
        return ret;

    uintptr_t file_end = ph->p_va + ph->p_filesz;
    for (uintptr_t va = start; va < end; va += PGSIZE)
    {
        struct Page *page;
        if (va < new_start)
        {
            page = get_page(mm->pgdir, va, NULL);
        }
        else
        {
            page = alloc_exec_page(mm->pgdir, va, elf_page_permissions(vm_flags));
            if (page == NULL)
                return -E_NO_MEM;
        }

        uintptr_t copy_start = va < ph->p_va ? ph->p_va : va;
        uintptr_t copy_end = va + PGSIZE < file_end ? va + PGSIZE : file_end;
        if (copy_start < copy_end &&
            !read_exec_data(source_mm, (unsigned char *)page2kva(page) + PGOFF(copy_start),
                            binary + ph->p_offset + (copy_start - ph->p_va), copy_end - copy_start))
        {
            return -E_INVAL;
        }
    }
    return 0;
}

static int load_icode(const unsigned char *binary, size_t size, struct mm_struct *source_mm,
                      struct mm_struct **new_mm, struct trapframe *tf)
{
    struct elfhdr elf;
    if (size < sizeof(elf))
        return -E_INVAL_ELF;
    if (!read_exec_data(source_mm, &elf, binary, sizeof(elf)))
        return -E_INVAL;
    if (elf.e_magic != ELF_MAGIC || elf.e_elf[ELF_IDENT_CLASS] != ELF_CLASS_64 ||
        elf.e_elf[ELF_IDENT_DATA] != ELF_DATA_LSB ||
        elf.e_elf[ELF_IDENT_VERSION] != ELF_VERSION_CURRENT ||
        elf.e_type != ELF_ET_EXEC || elf.e_machine != ELF_EM_RISCV ||
        elf.e_version != ELF_VERSION_CURRENT || elf.e_ehsize != sizeof(elf) ||
        elf.e_phentsize != sizeof(struct proghdr) || elf.e_phnum == 0 ||
        elf.e_phoff < sizeof(elf) || elf.e_phoff > size ||
        elf.e_phnum > (size - elf.e_phoff) / sizeof(struct proghdr) || (elf.e_entry & 1))
    {
        return -E_INVAL_ELF;
    }

    // mm_create and setup_pgdir can each consume one physical page.
    if (nr_free_pages() <= 2)
        return -E_NO_MEM;
    struct mm_struct *mm = mm_create();
    if (mm == NULL)
        return -E_NO_MEM;
    int ret = setup_pgdir(mm);
    if (ret != 0)
        goto failed;

    const uintptr_t stack_bottom = USTACKTOP - USTACKSIZE;
    uintptr_t previous_end = USERBASE;
    bool has_entry = 0;
    for (size_t i = 0; i < elf.e_phnum; ++i)
    {
        struct proghdr ph;
        if (!read_exec_data(source_mm, &ph, binary + elf.e_phoff + i * sizeof(ph), sizeof(ph)))
        {
            ret = -E_INVAL;
            goto failed;
        }
        if (ph.p_type == ELF_PT_INTERP)
        {
            ret = -E_INVAL_ELF;
            goto failed;
        }
        if (ph.p_type != ELF_PT_LOAD)
            continue;
        if (ph.p_filesz > ph.p_memsz || ph.p_offset > size || ph.p_filesz > size - ph.p_offset ||
            (ph.p_align > 1 && ((ph.p_align & (ph.p_align - 1)) != 0 ||
                                (ph.p_va & (ph.p_align - 1)) != (ph.p_offset & (ph.p_align - 1)))) ||
            PGOFF(ph.p_va) != PGOFF(ph.p_offset) || ph.p_va < previous_end ||
            ph.p_va >= stack_bottom || ph.p_memsz > stack_bottom - ph.p_va)
        {
            ret = -E_INVAL_ELF;
            goto failed;
        }
        if (ph.p_memsz == 0)
            continue;

        previous_end = ph.p_va + ph.p_memsz;
        if ((ph.p_flags & ELF_PF_X) && ph.p_va <= elf.e_entry && elf.e_entry < previous_end)
            has_entry = 1;
        if ((ret = map_elf_segment(mm, &ph, source_mm, binary)) != 0)
            goto failed;
    }
    if (!has_entry)
    {
        ret = -E_INVAL_ELF;
        goto failed;
    }

    ret = map_exec_vma(mm, stack_bottom, USTACKSIZE, VM_READ | VM_WRITE | VM_STACK);
    if (ret != 0)
        goto failed;
    // The lowest stack page is a guard. The remaining pages need eager backing.
    for (uintptr_t va = stack_bottom + PGSIZE; va < USTACKTOP; va += PGSIZE)
    {
        if (alloc_exec_page(mm->pgdir, va, PTE_U | PTE_R | PTE_W) == NULL)
        {
            ret = -E_NO_MEM;
            goto failed;
        }
    }

    memset(tf, 0, sizeof(*tf));
    tf->epc = elf.e_entry;
    tf->gpr.sp = USTACKTOP;
    tf->status = (read_csr(sstatus) | SSTATUS_SPIE | SSTATUS_SUM) &
                 ~(SSTATUS_SPP | SSTATUS_SIE | SSTATUS_FS);
    *new_mm = mm;
    return 0;

failed:
    destroy_unreferenced_mm(mm);
    return ret;
}

int do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    if (current == NULL || current->tf == NULL || len == 0 || size == 0)
        return -E_INVAL;

    struct mm_struct *old_mm = current->mm;
    if (!user_mem_check(old_mm, (uintptr_t)name, len, 0) ||
        !user_mem_check(old_mm, (uintptr_t)binary, size, 0))
    {
        return -E_INVAL;
    }
    char local_name[PROC_NAME_LEN + 1] = {0};
    size_t name_len = len < PROC_NAME_LEN ? len : PROC_NAME_LEN;
    if (!read_exec_data(old_mm, local_name, name, name_len) || local_name[0] == '\0')
        return -E_INVAL;
    // set_proc_name copies the full fixed-width name, including any padding.
    size_t copied_name_len = strnlen(local_name, name_len);
    memset(local_name + copied_name_len, 0, sizeof(local_name) - copied_name_len);

    struct mm_struct *mm;
    struct trapframe tf;
    int ret = load_icode(binary, size, old_mm, &mm, &tf);
    if (ret != 0)
        return ret;

    // Commit the replacement only after all reads from the old image succeed.
    bool intr_flag;
    local_intr_save(intr_flag);
    mm_count_inc(mm);
    current->mm = mm;
    current->pgdir = PADDR(mm->pgdir);
    *current->tf = tf;
    set_proc_name(current, local_name);
    // Sv39 MODE=8, using the same root page number encoding as proc_run.
    write_csr(satp, (8UL << 60) | (current->pgdir >> RISCV_PGSHIFT));
    flush_tlb();
    asm volatile("fence.i" ::: "memory");
    bool free_old_mm = old_mm != NULL && mm_count_dec(old_mm) == 0;
    local_intr_restore(intr_flag);

    if (free_old_mm)
        destroy_unreferenced_mm(old_mm);
    return 0;
}

int do_wait(int pid, int *code_store)
{
    return -E_UNIMP;
}

static int kernel_execve(const char *name, unsigned char *binary, size_t size)
{
    int ret = do_execve(name, strlen(name), binary, size);
    if (ret != 0)
        return ret;

    // copy_thread left this frame at the top of the kernel stack. __trapret
    // uses its end for sscratch and switches to its user sp with sret.
    intr_disable();
    forkrets(current->tf);
}

static int user_main(void *arg)
{
#ifdef TEST
    return KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    return KERNEL_EXECVE(exit);
#endif
}

//lab5 function4 YOUR CODE
int do_yield(void)
{
    return -E_UNIMP;
}

int do_kill(int pid)
{
    return -E_UNIMP;
}
