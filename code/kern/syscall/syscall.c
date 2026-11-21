#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <assert.h>
#include <error.h>
#include <vmm.h>

/* The merged tree declares this service but does not yet define it. */
extern void print_pgdir(void) __attribute__((weak));

//lab5 function1 YOUR CODE
static bool syscall_user_access(uintptr_t addr, size_t len, bool write)
{
    /* mm == NULL makes user_mem_check accept kernel addresses; user calls
     * must never use that trusted-kernel fallback. No pointer is dereferenced
     * here: do_wait/do_execve own the actual copy and its lifetime. */
    return current->mm != NULL && len != 0 &&
           user_mem_check(current->mm, addr, len, write);
}

static int sys_exit(uint64_t arg[])
{
    return do_exit((int)arg[0]);
}

static int sys_fork(uint64_t arg[])
{
    struct trapframe *tf = current->tf;
    return do_fork(0, tf->gpr.sp, tf);
}

static int sys_wait(uint64_t arg[])
{
    int *code_store = (int *)(uintptr_t)arg[1];
    if (code_store != NULL &&
        !syscall_user_access((uintptr_t)code_store, sizeof(*code_store), 1))
    {
        return -E_INVAL;
    }
    return do_wait((int)arg[0], code_store);
}

static int sys_exec(uint64_t arg[])
{
    const char *name = (const char *)(uintptr_t)arg[0];
    size_t len = arg[1];
    unsigned char *binary = (unsigned char *)(uintptr_t)arg[2];
    size_t size = arg[3];
    if (!syscall_user_access((uintptr_t)name, len, 0) ||
        !syscall_user_access((uintptr_t)binary, size, 0))
    {
        return -E_INVAL;
    }
    return do_execve(name, len, binary, size);
}

static int sys_yield(uint64_t arg[])
{
    return do_yield();
}

static int sys_kill(uint64_t arg[])
{
    return do_kill((int)arg[0]);
}

static int sys_getpid(uint64_t arg[])
{
    return current->pid;
}

static int sys_putc(uint64_t arg[])
{
    cputchar((int)arg[0]);
    return 0;
}

static int sys_pgdir(uint64_t arg[])
{
    if (print_pgdir == NULL)
    {
        return -E_UNIMP;
    }
    print_pgdir();
    return 0;
}

static int (*const syscalls[])(uint64_t arg[]) = {
    [SYS_exit]              sys_exit,
    [SYS_fork]              sys_fork,
    [SYS_wait]              sys_wait,
    [SYS_exec]              sys_exec,
    [SYS_yield]             sys_yield,
    [SYS_kill]              sys_kill,
    [SYS_getpid]            sys_getpid,
    [SYS_putc]              sys_putc,
    [SYS_pgdir]             sys_pgdir,
};

#define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))

//lab5 function1 YOUR CODE

void syscall(void)
{
    assert(current != NULL && current->tf != NULL);
    struct trapframe *tf = current->tf;
    uint64_t num = tf->gpr.a0;
    if (num >= NUM_SYSCALLS || syscalls[num] == NULL)
    {
        tf->gpr.a0 = -E_INVAL;
        return;
    }

    uint64_t arg[] = {
        tf->gpr.a1, tf->gpr.a2, tf->gpr.a3, tf->gpr.a4, tf->gpr.a5,
    };
    int ret = syscalls[num](arg);
    /* A successful exit never returns here. exec may replace the active
     * trapframe; write through current->tf after the service has returned. */
    current->tf->gpr.a0 = (intptr_t)ret;
}
