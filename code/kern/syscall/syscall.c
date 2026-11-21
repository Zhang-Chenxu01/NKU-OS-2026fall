#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <assert.h>
#include <error.h>


//lab5 function1 YOUR CODE
// Integration stubs only; replace with the exercise implementations.
static int sys_exit(uint64_t arg[])
{
    return -E_UNIMP;
}

static int sys_fork(uint64_t arg[])
{
    return -E_UNIMP;
}

static int sys_wait(uint64_t arg[])
{
    return -E_UNIMP;
}

static int sys_exec(uint64_t arg[])
{
    return -E_UNIMP;
}

static int sys_yield(uint64_t arg[])
{
    return -E_UNIMP;
}

static int sys_kill(uint64_t arg[])
{
    return -E_UNIMP;
}

static int sys_getpid(uint64_t arg[])
{
    return -E_UNIMP;
}

static int sys_putc(uint64_t arg[])
{
    return -E_UNIMP;
}

static int sys_pgdir(uint64_t arg[])
{
    return -E_UNIMP;
}

static int (*syscalls[])(uint64_t arg[]) = {
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
    //lab5 function1 YOUR CODE: decode a0/a1..a5, dispatch, and store the result.
    current->tf->gpr.a0 = -E_UNIMP;
}
