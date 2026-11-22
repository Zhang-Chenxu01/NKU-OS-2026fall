# 功能模块一——用户态系统调用

`````
[PROMPT]

**任务**：基于当前已合并的 Lab5 工程，实现用户态系统调用功能模块，完成从用户程序执行 `ecall`、内核识别异常、系统调用分发、执行对应内核服务，到返回用户态的处理链路。

主要修改文件：

- `code/kern/trap/trap.c`：完善用户态系统调用异常处理。
- `code/kern/syscall/syscall.c`：实现系统调用分发及各 `sys_*` 处理函数。

**操作要求**：

1. 必须直接修改项目中的实际代码，完成函数实现，而非仅提供代码片段或实现建议。
2. 保留 Lab1–Lab4 已完成的代码及现有异常处理逻辑，尽量只修改本模块涉及的函数和必要依赖。
3. 只负责成员 A 的功能模块一，不修改其他成员负责的 `do_*` 进程管理函数、虚拟内存实现或 `trapentry.S`。对于尚未完成的依赖，应记录并留待集成验证。
4. 实现前检查已有代码、函数声明及系统调用 ABI，以工程中的实际接口为准，避免重复定义或修改公开接口。
5. **Git 仅允许只读操作**，例如 `git status`、`git diff`、`git log`、`git show`。禁止执行 `git add`、`git commit`、`git push`、`git checkout`、`git reset`、`git stash` 及其他 Git 写操作，不创建或切换分支。源码与文档可以正常修改，但不进行 Git 暂存、提交或其他状态操作。

**输出要求**：

完成代码实现后，进行编译及必要的功能检查，并在 `docs/lab5_member_a_module1.md` 中输出交付文档，说明：

- 各函数的完成状态及对应代码位置。
- 修改的文件、主要实现内容与关键逻辑。
- 系统调用的完整执行流程。
- 关键 Case 及其处理方式。
- 实际执行的编译、测试命令和结果。
- 当前尚未完成或需要其他成员配合验证的内容。

文档必须依据真实实现与测试结果，不编造测试通过情况。

[RELY]

实现前读取以下相关文件，确认实际接口：

**1. 系统调用相关**

- `libs/unistd.h`：系统调用编号。
- `user/libs/syscall.c`：用户态系统调用参数约定。
- `kern/syscall/syscall.h`：系统调用入口声明。

**2. 异常及上下文相关**

- `kern/trap/trap.h`：`trapframe`、`pushregs` 等结构。
- `kern/trap/trap.c`：现有异常处理逻辑。
- `kern/trap/trapentry.S`：寄存器保存与恢复逻辑。
- `libs/riscv.h`：RISC-V 相关 CSR 和状态位定义。

**3. 进程与内存管理相关**

- `kern/process/proc.h`：`current`、`proc_struct` 及 `do_*` 接口。
- `kern/mm/vmm.h`：`mm_struct`、`user_mem_check` 等接口。
- 控制台输出及页表打印相关已有函数声明。

可以依赖工程中已有的 `do_exit`、`do_fork`、`do_wait`、`do_execve`、`do_yield`、`do_kill`、`cputchar` 等函数。

上述依赖的具体签名、参数和返回值应通过实际代码确认，不自行假设不存在的接口。

[GUARANTEE]

必须实现或完善以下函数：

**文件：**`**kern/trap/trap.c**`

```
void exception_handler(struct trapframe *tf);
```

**文件：**`**kern/syscall/syscall.c**`

```
void syscall(void);

static int sys_exit(uint64_t arg[]);
static int sys_fork(uint64_t arg[]);
static int sys_wait(uint64_t arg[]);
static int sys_exec(uint64_t arg[]);
static int sys_yield(uint64_t arg[]);
static int sys_kill(uint64_t arg[]);
static int sys_getpid(uint64_t arg[]);
static int sys_putc(uint64_t arg[]);
static int sys_pgdir(uint64_t arg[]);
```

要求所有函数均具有实际实现，保持与工程已有声明一致。允许在模块内部增加必要的 `static` 辅助函数，但不引入无关功能。

[SPECIFICATION]

## 1. exception_handler

**Pre-Condition**：

- 已通过 Trap 入口保存异常上下文。
- `tf` 指向当前有效的异常上下文。

**Post-Condition**：

- 用户态系统调用异常被正确转发。
- 异常返回地址和上下文保持正确。
- 原有异常处理功能不受影响。

**Case 1：用户态系统调用**

当异常原因为 `CAUSE_USER_ECALL` 时，将 `epc` 前移 4 字节，并调用 `syscall()` 完成处理，避免返回后重复执行 `ecall`。

**Case 2：其他异常**

保留已有异常处理路径，不改变 Lab3、Lab4 的原有行为。

## 2. syscall

**Pre-Condition**：

- 当前进程具有有效的 `trapframe`。
- 用户程序已按照约定将系统调用号和参数放入对应寄存器。

**Post-Condition**：

- 合法系统调用被分发到对应的 `sys_*` 函数。
- 对于正常返回的系统调用，返回值写入用户可见的返回寄存器。
- 非法请求得到正确处理，不破坏当前进程上下文。

**Case 1：合法系统调用**

读取 `a0` 中的系统调用号，以及 `a1`–`a5` 中的参数，调用对应处理函数。

**Case 2：非法或未实现的系统调用**

包括编号越界、未注册处理函数等情况，应按照工程已有错误码约定返回错误，不发生数组越界或空函数指针调用。

**Requirements**：

- 保证分发表下标及有效范围正确。
- 返回值按照用户态 ABI 写回。
- 注意 `exit`、`exec` 等可能改变当前执行上下文的特殊路径。

## 3. sys_exit / sys_fork / sys_wait / sys_exec

**Pre-Condition**：

- 系统调用参数已由 `syscall()` 获取。
- 当前进程及相关进程管理接口处于有效状态。

**Post-Condition**：

- 对应操作正确转交内核进程管理层。
- 参数及返回值符合用户态接口语义。
- 进程上下文及资源状态保持一致。

**Case 1：sys_exit**

调用现有退出接口，传递退出码。成功退出后当前进程不应继续执行原用户程序。

**Case 2：sys_fork**

调用现有进程创建接口，正确传递当前执行上下文及所需参数。保证调用约定支持父进程获得子 PID、子进程获得 0 的语义。

**Case 3：sys_wait**

传递目标 PID 和退出码存储地址，处理合法等待请求。对非空用户指针进行必要的地址及写权限检查，遵循底层接口的内存访问约定。

**Case 4：sys_exec**

正确传递程序名称、长度、二进制地址及大小等实际接口要求的参数。检查用户地址、长度及访问权限，避免非法用户指针导致内核异常。

**Requirements**：

- 不重复实现其他成员负责的进程创建、复制、退出和程序装载逻辑。
- 用户指针验证必须与工程当前 `user_mem_check`、`do_wait`、`do_execve` 的真实实现相匹配。
- 考虑 `fork` 返回值、`exec` 成功后的上下文变化及 `exit` 不返回等特殊情况。

## 4. sys_yield / sys_kill / sys_getpid

**Pre-Condition**：

- 当前进程及所需进程管理接口有效。

**Post-Condition**：

- 系统调用请求正确转交对应内核接口。

**Case 1：sys_yield**

请求当前进程主动让出 CPU，正确处理对应接口的返回语义。

**Case 2：sys_kill**

传递目标 PID，正确处理合法进程、无效 PID、目标不存在等情况，具体错误码遵循已有进程管理接口。

**Case 3：sys_getpid**

返回当前进程 PID，不修改进程状态。

## 5. sys_putc / sys_pgdir

**Pre-Condition**：

- 系统调用参数有效。
- 控制台及相关页表接口已初始化。

**Post-Condition**：

- 请求通过内核已有接口完成。
- 不直接暴露不允许访问的内核资源。

**Case 1：sys_putc**

读取字符参数，调用现有控制台输出接口，按照已有约定返回结果。

**Case 2：sys_pgdir**

按照工程已有页表查看接口完成系统调用，保持其参数、输出及返回值语义，不自行创建新的页表管理机制。

## 6. 验证与验收

完成实现后，执行以下检查：

1. 所有规定函数均有完整实现，函数签名与现有声明一致。
2. 执行项目已有的编译流程，检查编译和链接结果。
3. 检查系统调用分发表的编号映射、数组边界和返回值写回。
4. 验证 `CAUSE_USER_ECALL` 的处理及 `epc` 更新。
5. 检查非法系统调用号、非法用户指针、空指针及异常参数等典型情况。
6. 在现有环境允许的情况下执行用户态系统调用测试，并记录实际结果。

若其他成员尚未完成依赖函数，允许先完成静态检查、编译及可独立执行的测试。需要联合验证的功能应在交付文档中明确标记。

不修改或重写 Lab5 现有测试脚本，必要的临时测试应放在项目外的临时目录。

**最终交付**：实际修改后的代码，以及 `docs/lab5_member_a_module1.md`。完成后简要汇报实现情况、测试结果及剩余集成问题。
`````

<a name="lab5-user-elf"></a>

# 功能模块二——用户进程创建与 ELF 程序装载

`````
[PROMPT]

基于现有南开大学 2026 uCore Lab5 工程，完成用户进程创建、ELF 程序装载及首次进入用户态的完整实现。

工作目录：

`/mnt/e/Operating_System/ucore/NKU-OS-2026fall-lab5`

当前 Git 分支为 `lab5`。直接在现有代码上开发，保留此前 Lab1–Lab4 已完成的功能。

本次主要修改 `code/kern/process/proc.c`，必要时允许修改 `proc.h` 和直接相关的 ELF 头文件。

**操作限制：**

1. 只实现本提示词明确指定的函数及必要辅助代码，不进行无关重构。
2. 不修改 `do_exit`、`do_wait`、`do_yield`、`do_kill`、`dup_mmap`、`copy_range`。
3. 不修改 `syscall.c`、`trap.c`、`trapentry.S`、现有测试文件及评分规则。
4. Git 只允许 `status`、`diff`、`log` 等只读操作，禁止 commit、push、checkout、reset、stash、pull、fetch 等写操作。
5. 必须实际修改源码、编译并尽可能执行测试，不能只给出实现建议。

[RELY]

实现前阅读并确认以下文件的实际接口：

- `kern/process/proc.c`、`proc.h`、`entry.S`、`switch.S`
- `kern/mm/vmm.c`、`vmm.h`、`pmm.c`、`pmm.h`
- `kern/mm/memlayout.h`、`mmu.h`
- `libs/elf.h`
- `kern/trap/trap.h`、`trapentry.S`
- `kern/syscall/syscall.c`
- `user/hello.c`、`user/testbss.c`
- `tools/user.ld`、`Makefile`

以现有 RISC-V64 Sv39 实现为准，核对 ELF 格式、用户地址空间布局、页表权限、内存管理接口和 Trap 返回机制。

[GUARANTEE]

### 1. 实现 load_icode

完成 ELF 用户程序装载：

- 验证 ELF Magic、文件头、程序头及边界合法性。
- 遍历 `PT_LOAD` 程序段。
- 根据 ELF 段的虚拟地址、大小和权限建立 VMA。
- 创建用户地址空间和页表。
- 分配并映射代码段、数据段所需的物理页。
- 正确复制 ELF 文件内容。
- 清零 BSS，处理非页对齐的数据边界。
- 建立用户栈及对应的物理页面映射。
- 正确设置 `PTE_U`、读写执行权限。
- 初始化用户态 `trapframe`，设置程序入口地址、用户栈指针和 `sstatus`。
- 正确维护 `mm`、`pgdir` 和地址空间引用计数。
- 完善所有失败路径的内存资源回收。

### 2. 实现 do_execve

完成程序映像替换：

- 校验程序名称、二进制地址、大小等参数。
- 正确释放或替换原有用户地址空间。
- 调用 `load_icode` 装载新程序。
- 更新进程名称和执行上下文。
- 防止释放旧地址空间后继续访问失效的用户指针。
- 正确处理装载失败和资源回收。

### 3. 实现 kernel_execve

完成从内核线程启动内嵌用户 ELF 的过程：

- 保留现有函数签名。
- 正确调用程序装载接口。
- 与现有 `__trapret` 配合进入用户态。
- 正确处理内核栈、用户栈、`trapframe`、`sscratch` 和 `sstatus`。
- 确保最终通过 `sret` 切换到 U 态。
- 不得直接在 S 态执行用于 U 态系统调用的 `ecall` 来模拟用户态执行。

### 4. 完善用户进程启动

检查和完善以下函数：

- `alloc_proc`
- `kernel_thread`
- `proc_init`
- `init_main`
- `user_main`

实现完整调用链：

`proc_init → init_main → user_main → kernel_execve → do_execve → load_icode → __trapret → sret`

要求：

- 保持 idleproc、initproc 的正确初始化。
- `init_main` 能够创建执行 `user_main` 的内核线程，并按照现有接口进入等待与回收流程。
- `user_main` 能够启动内嵌 ELF 用户程序。
- 保留 `TEST` 宏选择测试程序的机制。
- 保证现有 PID 管理、进程链表、进程哈希表、内核线程和上下文切换机制不受破坏。

[SPECIFICATION]

### Pre-Condition

系统已经具备基本的内核线程管理、调度、物理页分配、Sv39 页表操作、VMA 管理和 Trap 处理机制。

### Post-Condition

能够创建第一个用户进程，正确装载 ELF，初始化用户地址空间，并从 S 态进入 U 态执行用户程序。

### Case 1：合法 ELF

正确装载代码段、数据段、BSS 和用户栈，保证入口地址及页表权限正确。

### Case 2：非法 ELF

处理 Magic 错误、程序头截断、非法偏移、段大小异常、用户地址越界及整数溢出，不允许非法内存访问。

### Case 3：内存不足

页表、物理页面或用户栈分配失败时，必须正确回收资源，不能发生内存泄漏。

### Case 4：用户态切换

保证 `trapframe`、`sstatus`、`sepc`、`sp` 和 `sscratch` 与现有 Trap 实现一致，确保 `sret` 正确进入 U 态。

### Case 5：程序替换

正确处理旧地址空间释放、新地址空间装载及页表切换，避免悬空指针和错误引用计数。

### 测试与验收

1. 编译整个 Lab5 工程，记录编译和链接结果。
2. 优先通过 `hello` 测试程序验证用户 ELF 装载、进入 U 态及系统调用输出。
3. 使用 `testbss` 验证 BSS 清零、页面映射及数据可写性。
4. 在条件允许时使用 QEMU 运行，并记录实际运行结果。
5. 检查是否影响 Lab4 原有功能。
6. 使用 `git diff` 检查最终改动范围。
7. 不修改评分脚本；如存在 CRLF 问题，可使用仓库外的临时 LF 副本进行检查。
8. 对尚未实现的依赖接口，记录其造成的测试阻塞，不擅自修改、不使用假实现绕过。
9. 不伪造任何测试输出或通过结果。

**最终输出：**

- 实际修改的文件与函数。
- ELF 装载、程序替换和用户进程启动的实现说明。
- 编译及测试命令与真实结果。
- 发现的错误及修复情况。
- 尚未解决的依赖问题。
- 当前代码是否已经具备进一步实现用户态 fork 的条件。

本次只完成上述功能，不提前实现用户态 fork、地址空间复制、进程退出和等待机制，也不增加无关功能。
`````

<a name="lab5-fork-mm"></a>

# 功能模块三——用户进程 fork 与地址空间复制

`````
[PROMPT]

**任务：完成南开大学 2026 uCore Lab5 的用户进程 fork 主流程，实现地址空间共享或复制、子进程上下文初始化及完整的失败回滚机制。**

工作目录：

`/mnt/e/Operating_System/ucore/NKU-OS-2026fall-lab5`

当前分支：`lab5`

本次直接在现有代码上开发，不重新搭建工程，不覆盖已有实现。

**主要修改文件：**

`code/kern/process/proc.c`

**需要实现或完善的函数：**

- `copy_mm`
- `do_fork`
- `copy_thread`

必要时可以调整上述函数直接使用的局部辅助代码，但尽量不修改其他函数。

**操作限制：**

1. 不修改 `do_exit`、`do_wait`、`do_yield`、`do_kill`、`dup_mmap`、`copy_range`。
2. 不修改 `syscall.c`、`trap.c`、`trapentry.S`。
3. 保留现有的 ELF 装载、exec、内核线程创建、PID 管理和调度机制。
4. 不修改现有测试文件、评分脚本及实验报告。
5. Git 只允许 `status`、`diff`、`log` 等只读操作，禁止执行 `add`、`commit`、`push`、`pull`、`fetch`、`checkout`、`reset`、`stash` 等写操作。
6. 不进行无关代码重构，不提前实现 COW。

[RELY]

实现前认真阅读并核对以下文件：

**进程管理：**

- `kern/process/proc.c`
- `kern/process/proc.h`
- `kern/process/entry.S`
- `kern/process/switch.S`
- `kern/schedule/sched.c`

**虚拟内存与物理内存：**

- `kern/mm/vmm.c`
- `kern/mm/vmm.h`
- `kern/mm/pmm.c`
- `kern/mm/pmm.h`
- `kern/mm/memlayout.h`

**Trap 与系统调用：**

- `kern/trap/trap.h`
- `kern/trap/trapentry.S`
- `kern/syscall/syscall.c`

**用户态测试：**

- `user/forktest.c`
- `user/forktree.c`
- `user/exit.c`

重点确认：

1. `proc_struct` 中 `mm`、`pgdir`、`kstack`、`tf`、`context`、`parent` 等字段的实际含义。
2. `mm_create`、`setup_pgdir`、`dup_mmap`、`exit_mmap`、`put_pgdir`、`mm_destroy` 的具体接口和资源管理约定。
3. `mm_count`、`mm_count_inc`、`mm_count_dec` 的使用方式。
4. `CLONE_VM` 对共享地址空间的影响。
5. `copy_range` 当前的接口及普通 fork 所需的物理页复制语义。
6. `forkret → forkrets → __trapret → sret` 的上下文恢复过程。
7. 当前已经实现的 `load_icode` 和 `do_execve` 对 `mm`、`pgdir` 的管理方式，保证 fork 与 exec 一致。

所有实现以本地实际代码为准，使用现有 RISC-V64 Sv39 页表与内存管理机制。

[GUARANTEE]

### 1. 完善 copy_mm

实现进程地址空间的共享与复制。

**情况一：当前进程没有用户地址空间**

- 保留已有内核线程创建语义。
- 不分配多余的用户地址空间。
- 正确使用现有内核页表。

**情况二：设置 CLONE_VM**

- 共享当前进程的 `mm_struct`。
- 正确增加地址空间引用计数。
- 为子进程设置正确的 `mm` 和 `pgdir`。
- 不重复复制物理页面。
- 确保之后的资源释放不会造成重复释放。

**情况三：普通用户进程 fork**

- 创建新的 `mm_struct`。
- 使用 `setup_pgdir` 建立独立页表。
- 调用现有 `dup_mmap` 复制虚拟内存区域和页面映射。
- 保持正确的地址空间引用计数。
- 为子进程设置正确的 `mm` 和 `pgdir`。
- 保证父子进程拥有独立的用户地址空间。
- 不使用 COW，不直接共享普通 fork 的可写物理页。

对于 `dup_mmap` 尚未实现的情况，应正确传递错误码并回收已经分配的资源，不允许通过假成功绕过。

### 2. 完善 copy_thread

正确复制并初始化子进程执行上下文：

- 在子进程内核栈上建立独立的 `trapframe`。
- 复制父进程的相关寄存器状态。
- 将子进程 `a0` 设置为 0。
- 设置正确的用户栈指针。
- 正确初始化 `context.ra` 和 `context.sp`。
- 保证首次调度后通过 `forkret` 恢复执行。
- 保持内核线程创建的原有语义。
- 确保普通用户进程 fork 后能够返回原用户程序的下一条指令。

不修改现有 Trap 恢复机制。

### 3. 完善 do_fork

在现有实现基础上完善 fork 主流程：

1. 检查进程数量限制。
2. 分配并初始化子进程控制块。
3. 设置父子进程关系。
4. 为子进程分配独立的内核栈。
5. 调用 `copy_mm` 处理地址空间。
6. 调用 `copy_thread` 初始化执行上下文。
7. 分配唯一 PID。
8. 将子进程正确加入进程链表和 PID 哈希表。
9. 将完全初始化的子进程设置为可运行状态。
10. 向父进程返回子进程 PID。

重点保证：

- 子进程获得的 fork 返回值为 0。
- 父进程获得的 fork 返回值为子进程 PID。
- 不允许尚未初始化完成的子进程提前参与调度。
- 进程链表、哈希表和父子关系保持一致。
- PID 分配及进程登记期间正确处理临界区。
- 不破坏 Lab4 原有的内核线程创建功能。

### 4. 完善失败回滚

检查所有可能失败的步骤，包括：

- 超过最大进程数量。
- 进程控制块分配失败。
- 内核栈分配失败。
- 地址空间创建失败。
- 页表分配失败。
- `dup_mmap` 返回错误。
- 地址空间复制过程中的物理内存不足。

要求：

- 正确释放已经分配的资源。
- 正确维护 `mm_count`。
- 不发生重复释放、悬空指针或内存泄漏。
- 不将失败的子进程登记为可运行进程。
- 不破坏父进程的地址空间及执行状态。
- 回滚责任应清晰，避免 `copy_mm` 与 `do_fork` 重复清理相同资源。

特别检查当前资源释放接口是否已经完整实现，不得假定调用一个尚未实现的清理函数就能正确回收页面。

[SPECIFICATION]

### Pre-Condition

- Lab4 的进程创建和调度机制已经存在。
- 用户进程创建、ELF 装载、exec 和首次进入 U 态已实现。
- `dup_mmap` 和 `copy_range` 接口已经声明，但具体实现可能尚未完成。
- 进程退出和等待相关功能可能仍是占位实现。

### Post-Condition

- `copy_mm` 能正确处理内核线程、共享地址空间和独立地址空间三种情况。
- `do_fork` 能完成子进程创建和执行上下文初始化。
- `copy_thread` 能保证正确的父子进程返回值和上下文恢复。
- 失败时资源正确回滚。
- 不影响已有 ELF 装载、exec 和内核线程功能。

### Case 1：内核线程创建

保持原有 `kernel_thread → do_fork` 执行路径，确保 Lab4 的进程初始化与调度功能不受影响。

### Case 2：CLONE_VM

验证共享 `mm_struct`、页表和引用计数的正确性。

### Case 3：普通 fork

验证独立地址空间的创建和 `dup_mmap` 调用流程。若依赖函数尚未实现，应正确返回错误，不制造虚假的成功结果。

### Case 4：父子执行上下文

检查父进程返回 PID、子进程返回 0，以及子进程恢复到用户态时的 PC、SP 和寄存器状态。

### Case 5：分配或复制失败

检查中途失败后的引用计数、内核栈、进程控制块、页表及进程链表状态。

### 测试要求

1. 完成源码修改后，执行编译和链接检查。
2. 检查现有内核线程创建功能是否仍正常。
3. 使用已有 `hello` 等程序检查是否破坏用户程序装载。
4. 尽可能测试 `CLONE_VM` 和普通 fork 的错误处理。
5. 检查 `forktest`、`forktree` 等现有测试所需条件，并在依赖具备时运行。
6. 对于尚未实现的 `dup_mmap`、`copy_range`、`do_exit`、`do_wait`，如实说明测试受阻情况，不擅自修改或使用临时假实现。
7. 使用 `git diff --check` 和 `git diff` 检查最终改动。
8. 如果原有脚本存在 CRLF 问题，允许使用仓库外的 LF 临时副本进行构建与测试，但不得修改原有评分脚本。
9. 所有测试结果必须来自真实执行，不得伪造成功日志。
10. 注意此前低内存测试中发现的物理页回收问题，避免为解决该问题而修改物理内存分配器；如果影响本轮验证，应单独说明。

### 最终交付

完成后请汇报：

1. 修改了哪些函数及主要实现内容。
2. `copy_mm` 的三种情况分别如何处理。
3. fork 的完整调用流程和父子返回值机制。
4. 失败回滚的具体实现。
5. 实际执行的编译和测试命令及结果。
6. 当前仍有哪些依赖问题。
7. 是否已经具备完整 fork 联合测试的条件。

**本轮必须实际完成上述三个函数的实现，不要只提供分析或开发建议。严格控制改动范围，不进行无关扩展。**
`````
