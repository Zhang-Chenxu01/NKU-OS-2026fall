# 实现虚拟内存页操作

`````text

[PROMPT]

**任务**：修改 `code/kern/mm/pmm.c`，完成 Lab4 功能模块一中的虚拟内存页操作，实现 `page_insert()` 和 `page_remove()`，使内核能够正确建立、更新和撤销 4 KiB 虚拟内存页映射。

**操作要求**：

1. 必须直接修改项目中的实际代码文件，保留现有实现。
2. 首先阅读 `lab4 进程管理.md` 中功能模块一的要求，然后检查当前项目的实际代码和函数接口。
3. 仅修改 `pmm.c` 中与本任务直接相关的实现区域。`pmm.h`、`mmu.h`、`memlayout.h` 等文件仅供读取和接口核对。
4. 保留已有 Lab2、Lab3 的内存管理功能和启动页表映射，避免破坏 Sv39 1 GiB 大页。
5. 保留实验要求的 `lab4` 注释。仅处理本人负责区域中的 `YOUR CODE` 标记。
6. Git 只允许读取操作，包括 `git status`、`git diff`、`git log`、`git show`。禁止执行任何改变 Git 状态、索引、HEAD 或分支的命令，包括提交、暂存、推送、切换分支、重置等。
7. 本提示词以实现为主，可以进行必要的编译检查。不得创建需要纳入 Git 的测试文件，不得修改现有受版本管理的测试代码。

**输出要求**：

直接完成代码实现，并在结束时说明：

- 实际修改的函数和文件。
- 两个函数的主要执行逻辑。
- 关键边界情况与引用计数的处理方法。
- 编译检查结果（如已执行）。
- 当前仍需要在专项测试阶段验证的问题。

[RELY]

**基础数据结构**：

- `pde_t`、`pte_t`：项目中的页表项类型。
- `struct Page`：物理页描述符，包含引用计数等物理页管理信息。
- `pgdir`：待操作的页表根目录。
- `la`：目标线性地址。
- `perm`：目标页表项的权限参数。

**需要核对的宏和接口**：

在实际项目中读取并确认以下定义及其真实签名：

- `get_pte()`：查找目标页表项，必要时分配中间级页表。
- `pte_create()`、`pte2page()`：页表项构造与物理页解析。
- `page_ref_inc()`、`page_ref_dec()`、`page_ref()`：物理页引用计数操作。
- `alloc_page()`、`free_page()`：物理页分配与释放。
- `tlb_invalidate()`、`flush_tlb()`：TLB 失效与刷新。
- `PTE_V`、`PTE_R`、`PTE_W`、`PTE_X`、`PTE_U`：Sv39 页表权限位。
- `PGSIZE`、`PTE_ADDR` 及实际工程中已有的页表地址转换宏。
- `E_NO_MEM`：内存不足错误码。

这些名称来源于实验指导材料，使用前必须与当前工程核对。不得凭空定义替代接口或假设不存在的宏。

**依赖关系**：

1. 已有物理页分配器和 `struct Page` 初始化逻辑应保持不变。
2. 已有 `get_pte()`、`page_remove_pte()`、`get_page()` 等辅助函数优先复用。
3. `page_insert()` 和 `page_remove()` 必须遵守同一套引用计数与页表权限约定。
4. 如果发现已有辅助函数存在影响本任务的缺陷，应说明问题及其影响；除本任务授权的函数外，不擅自扩大修改范围。

[GUARANTEE]

必须完成以下接口，保持当前工程中的声明与函数签名一致：

```c
int page_insert(pde_t *pgdir, struct Page *page,
                uintptr_t la, uint32_t perm);

void page_remove(pde_t *pgdir, uintptr_t la);
```

允许在 `pmm.c` 内添加确有必要的 `static` 辅助函数，但应优先复用现有实现，不新增公共接口。

[SPECIFICATION]

## page_insert()

**Pre-Condition**:

- `pgdir` 指向有效的页表根目录。
- `page` 指向已经分配的物理页描述符。
- `la` 是需要建立映射的线性地址。
- 物理内存管理器及已有页表基础设施已经初始化。
- 映射操作符合当前项目对页表层级和物理页生命周期的约定。

**Post-Condition**:

- 成功时，目标线性地址应映射到指定物理页，页表项包含正确的物理页号及权限。
- 物理页引用计数应与实际映射关系一致。
- 旧映射应得到正确处理。
- 对映射或权限的修改应执行必要的 TLB 失效。
- 失败时返回项目规定的错误码，不能留下错误的有效叶子页表项。

**Case 1：目标地址尚无映射**

定位或创建目标页表项，建立从 `la` 到 `page` 的有效映射，并正确增加物理页引用计数。成功返回 `0`。

**Case 2：目标地址已映射同一物理页**

保留原有物理页及映射关系，按新的 `perm` 更新权限。物理页引用计数的最终值不得发生无意义变化，也不得错误释放该物理页。

**Case 3：目标地址已映射其他物理页**

将目标地址更新为新的物理页映射，正确增加新物理页的引用计数，并处理旧物理页的引用计数和必要回收。

更新过程中应避免悬空映射、重复释放或引用计数失衡。

**Case 4：中间级页表分配失败**

如果 `get_pte()` 无法创建需要的页表项，返回 `-E_NO_MEM`。

不得创建不完整的有效叶子映射，不得错误增加目标物理页引用计数。若底层 `get_pte()` 本身存在部分分配失败后的清理问题，应明确指出，不得擅自重构该函数。

**Case 5：映射权限发生变化**

按照当前项目的 Sv39 页表格式生成合法叶子页表项，确保权限与调用参数要求一致。

注意 RISC-V 页表的权限合法性，例如不能产生 `W=1, R=0` 的无效叶子 PTE 组合。

**Requirements**:

- 保持 Sv39 的三级页表组织方式。
- 正确区分中间级非叶子页表项和叶子映射项。
- 不把已有 1 GiB 大页错误当成下一级页表。
- 不破坏启动阶段已建立的内核映射。
- 根据当前项目的 TLB 接口实现必要的失效处理。
- 不修改物理页分配器及其管理算法。

## page_remove()

**Pre-Condition**:

- `pgdir` 指向有效的页表根目录。
- `la` 为需要取消映射的线性地址。
- 物理内存管理基础设施已经初始化。

**Post-Condition**:

- 如果存在目标映射，删除对应的有效叶子页表项。
- 正确减少被解除映射的物理页引用计数。
- 当引用计数归零时，按现有物理内存管理约定回收该物理页。
- 对已经失效的映射执行必要的 TLB 失效。
- 函数能够安全处理不存在映射的地址。

**Case 1：目标地址不存在有效映射**

直接返回，不应触发 panic，不释放任何物理页，也不改变无关页表项。

**Case 2：目标地址存在有效映射**

取得目标页表项，解除映射，减少对应物理页引用计数，并在引用计数归零时释放物理页。

**Case 3：同一物理页仍被其他虚拟地址引用**

只删除当前虚拟地址对应的映射。被映射物理页仍应保持有效，不得提前回收。

**Case 4：重复删除同一虚拟地址**

重复调用应具有幂等性，不得再次减少引用计数或者重复释放物理页。

**Requirements**:

- 优先复用已有 `page_remove_pte()`，避免重复实现引用计数和资源回收逻辑。
- 查询映射时不得因为删除操作而额外分配页表。
- 不擅自回收仍被其他映射或现有框架使用的中间级页表。
- 不改变其他虚拟地址的有效映射。

完成后，检查 `git diff`，确认修改内容严格限制在本人负责的区域。仅输出修改与检查结果，不执行任何 Git 写入操作。

`````

<a id="lab4-process-create-2411566"></a>

# Lab4：进程创建与初始化功能实现

## [PROMPT]

**任务：**

在本项目 Lab4 工程中，完成 Lab4 的进程创建及初始化功能。

主要修改文件：`code/kern/process/proc.c`。

需要实现六个函数：

- `alloc_proc()`
- `proc_init()`
- `kernel_thread()`
- `do_fork()`
- `copy_thread()`
- `copy_mm()`

最终目标是正确创建 `idleproc` 和 `initproc`，完成 PCB、内核栈、PID、Trap frame 和执行上下文的初始化，使创建出的内核线程能够被后续调度器正确执行。

**操作要求：**

1. 首先检查工程中相关的实际代码、数据结构和函数接口，确认现有实现及依赖关系。
2. 仅修改 `proc.c` 中 `//LAB4 function2 YOUR CODE` 对应的功能区域，必要时可以增加该模块内部的静态辅助函数。
3. 保留已有代码和接口，不修改其他功能模块，不引入无关功能。
4. 不修改 `proc_struct`、`context`、`trapframe` 的现有布局。
5. 保留 `lab4` 注释，将本次实现区域中的 `YOUR CODE` 替换为学号 `2411566`。
6. 不修改 `pmm.c`、`sched.c`、`switch.S`、`trapentry.S` 及现有测试代码。
7. 本次只实现 Lab4 内核线程创建功能，不提前实现 Lab5 用户进程管理。
8. 不擅自执行 Git 提交、推送、合并或重置操作，保留所有已有修改。

**输出要求：**

直接修改实际代码文件。完成后简要说明修改内容、关键实现逻辑、编译及测试结果，以及尚未解决的问题。

## [RELY]

以下是实现所依赖的现有数据结构和接口，使用时必须与项目中的实际定义保持一致。

**1. 核心数据结构**

`struct proc_struct`：进程控制块，包含：

- `state`：进程状态
- `pid`：进程标识符
- `runs`：运行次数
- `kstack`：内核栈地址
- `need_resched`：是否需要重新调度
- `parent`：父进程指针
- `mm`：内存管理结构
- `context`：进程切换上下文
- `tf`：Trap frame 指针
- `pgdir`：页表根节点物理地址
- `flags`、`name`：标志和名称
- `list_link`、`hash_link`：进程链表及哈希表节点

`struct context`：保存 `ra`、`sp`、`s0～s11`，用于进程上下文切换。

`struct trapframe`：保存通用寄存器以及 `status`、`epc` 等 Trap 恢复信息。

**2. 全局变量**

- `proc_list`、`hash_list`
- `current`、`idleproc`、`initproc`
- `nr_process`
- `boot_pgdir_pa`
- `bootstack`

**3. 可使用的函数和宏**

- 内存管理：`kmalloc()`、`kfree()`、`setup_kstack()`、`put_kstack()`
- 进程管理：`get_pid()`、`hash_proc()`、`set_proc_name()`、`wakeup_proc()`
- 链表管理：`list_init()`、`list_add()`
- 上下文初始化：`forkret()`、`forkrets()`、`kernel_thread_entry()`
- 中断保护：`local_intr_save()`、`local_intr_restore()`
- 状态和常量：`PROC_UNINIT`、`PROC_RUNNABLE`、`MAX_PROCESS`、`KSTACKSIZE`、`CLONE_VM`
- 状态寄存器位：`SSTATUS_SPP`、`SSTATUS_SPIE`、`SSTATUS_SIE`
- 错误码：`E_NO_MEM`、`E_NO_FREE_PROC`

不得自行创建与现有接口重复的函数或宏。

## [GUARANTEE]

必须实现以下六个函数，保持函数签名不变：

```c
static struct proc_struct *alloc_proc(void);

void proc_init(void);

int kernel_thread(int (*fn)(void *),
                  void *arg,
                  uint32_t clone_flags);

int do_fork(uint32_t clone_flags,
            uintptr_t stack,
            struct trapframe *tf);

static void copy_thread(struct proc_struct *proc,
                        uintptr_t esp,
                        struct trapframe *tf);

static int copy_mm(uint32_t clone_flags,
                   struct proc_struct *proc);
```

所有函数必须实现真实功能，不允许保留原有 `Lab4 TODO` panic 或用空函数绕过测试。

## [SPECIFICATION]

### 1. alloc_proc()：分配并初始化 PCB

**Pre-Condition：**

内核内存分配器已初始化。

**Post-Condition：**

成功时返回初始化完成的 PCB，失败时返回 `NULL`。

**Cases：**

- 使用 `kmalloc()` 分配 `proc_struct`。
- 将状态初始化为 `PROC_UNINIT`，PID 初始化为 `-1`。
- 初始化运行次数、内核栈、调度标记、父进程、内存管理指针、Trap frame、flags 等字段。
- 将 `context` 清零，按当前内核页表约定设置 `pgdir`。
- 初始化两个链表节点及名称字段。
- 内存分配失败时安全返回，不访问空指针。

### 2. proc_init()：初始化进程管理系统

**Pre-Condition：**

内核物理内存、页表和动态内存分配器已经初始化。

**Post-Condition：**

完成全局进程管理初始化，成功建立 `idleproc` 和 `initproc`。

**Cases：**

- 初始化进程链表及 PID 哈希表。
- 使用 `alloc_proc()` 分配 `idleproc` 的 PCB。
- 将 `idleproc` 的 PID 设置为 0，状态设置为 `PROC_RUNNABLE`。
- `idleproc` 复用启动阶段已有的 `bootstack`，不额外分配内核栈。
- 设置 `current = idleproc`，并将其 `need_resched` 置为 1。
- 通过 `kernel_thread()` 创建执行 `init_main()` 的内核线程，传入 `"Hello world!!"` 作为参数。
- 将新线程登记为 `initproc`，设置名称 `"init"`，PID 应为 1。
- 初始化结束后，系统进程总数应为 2。
- 对关键初始化失败进行合理检查。

### 3. kernel_thread()：创建内核线程

**Pre-Condition：**

目标函数和参数有效，进程管理基础设施已初始化。

**Post-Condition：**

成功时返回新线程 PID，失败时返回错误码。

**Cases：**

- 创建并清零临时 `trapframe`。
- 按现有汇编入口约定，将函数指针和参数保存到 `s0`、`s1` 对应字段。
- 设置 `epc` 指向 `kernel_thread_entry`。
- 正确配置 `sstatus` 的 SPP、SPIE 和 SIE 位，使新线程能够在 S 模式下正确开始执行。
- 按内核线程共享地址空间的约定，调用 `do_fork()` 创建新线程。

### 4. do_fork()：完成线程创建

**Pre-Condition：**

进程管理系统已经初始化，传入有效的 Trap frame。

**Post-Condition：**

成功时返回新线程 PID，线程进入 `PROC_RUNNABLE` 状态；失败时返回错误码并清理已分配资源。

**Cases：**

- 若进程数量达到 `MAX_PROCESS`，返回 `-E_NO_FREE_PROC`。
- 调用 `alloc_proc()` 分配 PCB。
- 调用 `setup_kstack()` 分配内核栈。
- 调用 `copy_mm()` 处理内存管理信息。
- 调用 `copy_thread()` 初始化新线程执行现场。
- 设置父进程关系，并分配唯一 PID。
- 将新进程加入 `proc_list` 和 PID 哈希表。
- 更新 `nr_process`，调用 `wakeup_proc()` 将其设置为可运行状态。
- PID 分配、进程链表登记及进程数量更新应在必要的中断保护下完成。
- 如果 PCB 分配、内核栈分配或内存初始化失败，必须正确回收已分配资源，不能产生内存泄漏或残留无效进程记录。

### 5. copy_thread()：初始化新线程的执行现场

**Pre-Condition：**

新线程 PCB 与内核栈已经分配完成，输入 Trap frame 有效。

**Post-Condition：**

新线程具备能够完成首次上下文切换和 Trap 返回的执行现场。

**Cases：**

- 在新线程内核栈中为 Trap frame 预留空间。
- 将输入 Trap frame 复制到新线程的内核栈中。
- 按子线程返回约定设置 `a0 = 0`。
- 正确设置线程栈指针，保证首次执行时使用有效的内核栈；特别注意 `esp == 0` 的内核线程创建情况。
- 将 `context.ra` 设置为 `forkret`。
- 将 `context.sp` 设置为新线程 Trap frame 的地址。
- 保证首次执行能够正确经过以下流程：

`switch_to → forkret → forkrets → __trapret → kernel_thread_entry → 目标函数`

不得修改现有寄存器保存布局或汇编恢复接口。

### 6. copy_mm()：初始化线程内存管理信息

**Pre-Condition：**

新线程 PCB 已分配完成。

**Post-Condition：**

完成当前 Lab4 阶段的内存管理接口处理。

**Cases：**

- 当前实验仅创建内核线程，不要求复制用户态虚拟地址空间。
- 对 `current->mm == NULL` 的情况，正确设置 `proc->mm = NULL`，返回成功。
- 不实现用户进程地址空间复制、写时复制或页面置换等后续功能。
- 不得错误共享或释放不属于当前线程的内存资源。

## 验证与交付要求

实现完成后，依次进行以下检查：

1. 确认六个函数全部完成，且没有遗留本模块的 TODO panic。
2. 检查 PCB 各字段、PID、父进程关系和内核栈是否初始化正确。
3. 检查 `do_fork()` 各种资源分配失败路径是否正确回滚。
4. 检查新线程的 Trap frame、context 及首次执行入口是否正确。
5. 运行 `make` 完成编译和链接检查。
6. 如果环境具备条件，运行 `make grade`，重点验证 `[Test Process Create] PASS`。
7. 不修改评分脚本和测试文件来规避失败。
8. 如果其他尚未实现的功能阻塞了整体运行，应明确指出阻塞原因，不能将未通过的整体测试描述为通过。

最后简要汇报实际修改文件、六个函数的实现情况、测试结果和遗留问题。

**本轮只完成上述进程创建与初始化功能，不扩展任务范围，也不进行 Git 提交或推送。**

# Lab4：进程调度与上下文切换实现

## [PROMPT]

**任务：**

在本项目 Lab4 工程中，完成 Lab4 的进程调度与上下文切换功能。

主要修改文件：

- `code/kern/schedule/sched.c`
- `code/kern/process/proc.c`（仅功能模块三区域）
- `code/kern/process/switch.S`

需要实现四个函数：

- `cpu_idle()`
- `schedule()`
- `proc_run()`
- `switch_to()`

最终目标是让 `idleproc` 在 `kern_init()` 末尾主动让出 CPU，调度器选中 `initproc` 后完成页表与寄存器现场切换，使新线程能够首次运行并进入 `init_main`；后续测试线程也能够在 `schedule()` 与 `switch_to()` 之间反复切换。

**操作要求：**

1. 首先检查工程中 `sched.c`、`proc.c`、`switch.S`、`proc.h`、`trap.h`、`trapentry.S`、`process/entry.S` 及测试代码的真实实现，确认接口与依赖。
2. 只修改 `//LAB4 function3 YOUR CODE` 对应的三处区域；不得改动 `//LAB4 function1`（虚拟内存页操作）和 `//LAB4 function2`（进程创建）的代码。
3. 不修改 `struct proc_struct`、`struct context`、`struct trapframe` 的现有布局，不改变 `forkret → forkrets → __trapret` 的既有恢复链路。
4. 不修改 `pmm.c`、`entry.S`、`trapentry.S`、`tools/` 下的测试与评分脚本。
5. 保留 `lab4` 注释，将本区域的 `YOUR CODE` 替换为学号 `2413042`。
6. 不引入时间片、优先级、抢占式调度等本实验之外的功能；调度点仍只保留在 `cpu_idle()` 中。
7. 临界区必须使用现有 `local_intr_save()` / `local_intr_restore()`，并恢复进入前的中断状态。
8. 不擅自执行 Git 提交、推送、合并或重置操作，保留工作区已有修改。

**输出要求：**

直接修改实际代码文件。完成后简要说明四个函数的实现要点、关键寄存器与页表操作、编译及运行验证结果，以及尚未解决的问题。

## [RELY]

以下是实现所依赖的现有数据结构和接口，使用时必须与项目中的实际定义保持一致。

**1. 核心数据结构**

`struct proc_struct`：进程控制块，本模块主要使用：

- `state`：`PROC_RUNNABLE` 等状态
- `need_resched`：调度请求标志
- `runs`：运行次数
- `kstack`：内核栈地址
- `pgdir`：页表根节点物理地址
- `context`：线程切换上下文
- `list_link`：进程链表节点

`struct context`：保存 `ra`、`sp`、`s0～s11`，用于线程上下文切换。

`struct trapframe`：保存通用寄存器和 `status`、`epc`，由 `__trapret` 恢复。

**2. 全局变量**

- `proc_list`、`current`、`idleproc`、`initproc`
- `boot_pgdir_pa`、`bootstack`

**3. 可使用的函数和宏**

- 调度与切换：`switch_to()`、`proc_run()`
- 进程状态：`PROC_RUNNABLE`、`PROC_SLEEPING`、`PROC_ZOMBIE`
- 链表管理：`list_next()`、`le2proc()`、`list_entry_t`
- 中断保护：`local_intr_save()`、`local_intr_restore()`、`intr_enable()`、`intr_disable()`
- 寄存器操作：`read_csr()`、`write_csr()`、`SSTATUS_SIE`
- 页表与 TLB：`RISCV_PGSHIFT`、`flush_tlb()`
- 首次运行链路：`forkret()`、`forkrets()`、`__trapret`、`kernel_thread_entry`
- 断言：`assert()`

不得自行创建与现有接口重复的函数或宏，也不得修改 `context` 的字段顺序。

## [GUARANTEE]

必须实现以下四个函数，保持函数签名不变：

```c
void cpu_idle(void) __attribute__((noreturn));

void schedule(void);

void proc_run(struct proc_struct *proc);

void switch_to(struct context *from, struct context *to);
```

`switch_to()` 必须按 `ra`、`sp`、`s0～s11` 的顺序保存旧 context、恢复新 context；`proc_run()` 必须按 Sv39 格式写 `satp`；`schedule()` 必须处理“没有其他就绪线程”的情况；`cpu_idle()` 必须持续运行不返回。

所有函数必须实现真实功能，不允许保留原有 `Lab4 TODO` panic、空循环或占位跳转绕过测试。

## [SPECIFICATION]

### 1. cpu_idle()：空闲线程主循环

**Pre-Condition：**

`current` 指向有效 PCB；`kern_init()` 已完成进程管理初始化；调度点位于该循环内。

**Post-Condition：**

函数不返回；每次发现 `current->need_resched` 非零时调用 `schedule()`。

**Cases：**

- `need_resched == 0`：继续循环，不调用调度器。
- `need_resched != 0`：调用一次 `schedule()`，由其清除标志并可能切换线程。
- 不主动设置 `need_resched`，不引入时钟抢占。

### 2. schedule()：选择下一个可运行线程

**Pre-Condition：**

`current`、`idleproc` 有效；`proc_list` 已初始化；可在任意线程上下文调用。

**Post-Condition：**

`current->need_resched` 被清零；若存在其他 `PROC_RUNNABLE` 线程则切换到该线程，否则保持或回到 `idleproc`；被选中线程的 `runs` 增加；退出时恢复原中断状态。

**Cases：**

- `current == idleproc`：从 `proc_list` 表头开始扫描。
- `current` 为普通线程：从当前线程的 `list_link` 之后开始扫描，实现轮转。
- 扫描时跳过链表头哨兵，遇到第一个 `PROC_RUNNABLE` 即停止。
- 整圈未找到：回退到 `idleproc`。
- `next == current`：不调用 `proc_run()`。
- 全程处于关闭中断的临界区。

### 3. proc_run()：切换线程与页表

**Pre-Condition：**

`proc` 指向已初始化且处于可运行状态的 PCB；`proc->pgdir` 是页表根物理地址；`proc->context` 已按布局初始化。

**Post-Condition：**

`current == proc`；`satp` 的 MODE=8、PPN=`proc->pgdir >> 12`；TLB 已刷新；`switch_to()` 后执行流位于目标线程现场；中断状态恢复到进入临界区前。

**Cases：**

- `proc == current`：直接返回，不做切换。
- 目标与当前共享启动页表：`satp` 数值相同，仍执行刷新以保持一致性。
- 目标为新线程：`context.ra = forkret`，经 `forkrets → __trapret → kernel_thread_entry` 进入目标函数。
- 切换过程中不得被中断打断。

### 4. switch_to()：保存与恢复上下文

**Pre-Condition：**

`from`、`to` 指向有效的 `struct context`；调用发生在内核栈上。

**Post-Condition：**

旧线程的 `ra/sp/s0～s11` 已写入 `from`，CPU 寄存器已从 `to` 恢复；`ret` 返回到 `to->ra` 指向的位置。

**Cases：**

- 普通线程切换：返回到对方上次 `switch_to()` 之后，即 `proc_run()` 中的下一条语句。
- 新线程首次切换：`to->ra == forkret`，返回到 `forkret`。
- 寄存器偏移必须与 `struct context` 的字段顺序一致，不得增删字段。

## 验证与交付要求

1. 确认四个函数全部完成，不存在本模块遗留的 TODO panic 或占位实现。
2. 检查 `schedule()` 的扫描起点、链表头哨兵处理、`idleproc` 兜底与中断恢复。
3. 检查 `proc_run()` 的 `satp` 编码、TLB 刷新与 `current` 更新顺序。
4. 检查 `switch_to()` 的寄存器偏移与 `ra/sp/s0～s11` 覆盖是否完整。
5. 运行 `make` 完成编译和链接检查。
6. 运行 `make grade`，确认 `[Test Schedule] PASS` 以及 `initproc` 的三行输出。
7. 不修改评分脚本和测试代码来规避失败；若其他模块阻塞整体运行，应明确指出阻塞原因。
8. 最后简要汇报实际修改文件、四个函数的实现情况、测试结果和遗留问题。

**本轮只完成上述调度与上下文切换功能，不扩展任务范围，也不进行 Git 提交或推送。**

