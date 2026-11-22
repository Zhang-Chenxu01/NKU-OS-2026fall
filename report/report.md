# 操作系统实验报告

## 目录

- [实验基本信息](#实验基本信息)
- [一、实验目的](#一实验目的)
- [二、实验环境](#二实验环境)
- [三、实验整体逻辑分析](#三实验整体逻辑分析)
- [四、实验内容与实现](#四实验内容与实现)
- [五、测试与验证](#五测试与验证)
- [六、实验总结与收获](#六实验总结与收获)

## 实验基本信息

| 项目 | 内容 |
| --- | --- |
| **实验名称** |  |
| **小组成员** | 2411326-张晨旭、2411566-罗晟皓、2413042-郭桦 |
| **完成日期** |  |

### 小组分工

| 成员 | 功能模块 | 负责的任务与修改范围 |
| --- | --- | --- |
| 2411326-张晨旭 |  |  |
| 2411566-罗晟皓 |  |  |
| 2413042-郭桦 |  |  |

---

## 一、实验目的

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 |
| --- | --- | --- |
| 2411326-张晨旭 | OpenAI Codex | `gpt6-luna` |
| 2411566-罗晟皓 | OpenAI Codex | `gpt6-luna` |
| 2413042-郭桦 | DeepSeek Harness | `deepseek-flash` |

### 运行环境

- 开发平台：Windows，使用 PowerShell 与 Git。
- 目标架构与验证系统：RISC-V RV64；独立编译和模拟接口测试在 WSL 的 Ubuntu 22.04 环境中执行。
- 编译工具：`riscv64-unknown-elf-gcc` 用于 RISC-V 交叉编译，`gcc` 用于宿主模拟接口测试。
- 模拟器：QEMU 的 RISC-V `virt` 机器，课程脚本使用 `-bios default`。完整 QEMU 运行与评分状态见第五章。

---

## 三、实验整体逻辑分析

### 本章节的逻辑主线

### 功能的逐步实现

---

## 四、实验内容与实现

### 4.1 

**负责人：** 

#### 4.1.1 模块功能描述

#### 4.1.2 关键接口与设计

#### 4.1.3 具体实现

#### 4.1.4 Spec 与 Prompt

#### 4.1.5 实现及验证过程

### 4.2 用户进程创建与 ELF 程序装载

**负责人：** 2411566-罗晟皓

#### 4.2.1 模块功能描述

第一个用户进程从一个可调度的内核线程开始。内核先为它分配 PCB、PID 和内核栈，再把内嵌 ELF 的程序段装入独立的用户地址空间。装载成功后，线程沿用原 PID 和内核栈，CPU 从 ELF 指定的入口以 U 态执行，用户栈指针指向新建立的栈顶。

```text
proc_init → init_main → user_main → kernel_execve → do_execve → load_icode
load_icode 返回 → do_execve 安装新映像并返回 → kernel_execve → forkrets → __trapret → sret → ELF 入口
```

其中，`init_main` 和 `user_main` 分别是两次创建的内核线程入口，由调度器先后选中执行。从 `kernel_execve` 开始，装载和首次用户态返回都发生在同一个进程中。[进程管理实现](../code/kern/process/proc.c)

#### 4.2.2 关键接口与设计

| 数据结构或接口 | 定义位置 | 本模块中的作用 |
| --- | --- | --- |
| `proc_struct`、`trapframe` | [proc.h](../code/kern/process/proc.h)、[trap.h](../code/kern/trap/trap.h) | PCB 记录 PID、内核栈和活动 Trap 帧指针。帧中的 `epc`、`gpr.sp`、`status` 分别保存返回地址、栈指针及特权状态 |
| `mm_struct`、`vma_struct` | [vmm.h](../code/kern/mm/vmm.h) | 一个 `mm` 管理根页表、VMA 链表和引用计数。每个 VMA 描述一个虚拟地址区间及其读、写、执行或栈属性 |
| `mm_create()`、`setup_pgdir()`、`mm_map()` | [vmm.c](../code/kern/mm/vmm.c)、[proc.c](../code/kern/process/proc.c) | 创建引用计数为零的地址空间，分配根页表并复制启动页表的根项，按页对齐后登记 VMA |
| `pgdir_alloc_page()` | [pmm.c](../code/kern/mm/pmm.c) | 为指定虚拟页分配物理页并建立叶子映射。装载器通过物理页的内核映射写入文件内容 |
| `elfhdr`、`proghdr` | [elf.h](../code/libs/elf.h) | ELF 头给出入口和程序头表位置。程序头给出段的文件偏移、虚拟地址、文件长度、内存长度及权限 |
| `satp`、`sstatus`、`sepc`、`sscratch` | [proc.c](../code/kern/process/proc.c)、[trapentry.S](../code/kern/trap/trapentry.S) | `satp` 选择 Sv39 页表，`sstatus.SPP` 和 `sepc` 决定返回特权级与指令地址，`sscratch` 保存用户程序下次陷入时使用的内核栈顶 |

用户地址范围为 `[0x00200000, 0x80000000)`。内核通过 `mm->pgdir` 的内核虚拟地址填充页表，切换地址空间时使用 `proc->pgdir` 记录的根页表物理地址。用户栈从 `USTACKTOP` 向低地址增长，容纳用户程序的调用现场；进程的内核栈容纳 Trap 帧和内核函数的调用现场。陷入和返回时，由汇编入口切换栈指针。[地址空间布局](../code/kern/mm/memlayout.h)

#### 4.2.3 具体实现

**（1）从启动线程建立进程关系。** `alloc_proc()` 将 PCB 清零，再设置 `state = PROC_UNINIT`、`pid = -1` 和启动页表根的物理地址。进程链表与 PID 哈希表的节点分别调用 `list_init()`，使空节点首尾自指。`proc_init()` 让 PID 0 的 `idleproc` 复用启动栈并成为 `current`，随后以 `kernel_thread(init_main, NULL, 0)` 创建 PID 1 的 `initproc`。`kernel_thread()` 把函数和参数分别放入临时 Trap 帧的 `s0`、`s1`，令 `epc` 指向 `kernel_thread_entry`，并设置返回 S 态所需的状态位。线程首次运行时，汇编入口将参数送入 `a0` 并调用目标函数。[内核线程入口](../code/kern/process/entry.S)

**（2）选择内嵌镜像。** `init_main()` 创建执行 `user_main` 的子线程，调用 `schedule()` 交出 CPU，恢复执行后进入 `do_wait(0, NULL)` 等待循环。构建时，每个用户程序先链接为独立 ELF，再作为二进制数据嵌入内核。`user_main()` 根据 `TEST`、`TESTSTART` 和 `TESTSIZE` 取得所选程序的名称、起始地址和字节数，默认程序为 `exit`。首次启动时这些参数位于内核地址空间，`kernel_execve()` 直接调用 `do_execve()`。[用户程序构建规则](../code/Makefile)

**（3）按程序头装载 ELF。** `load_icode()` 接受 ELF64、小端、RISC-V 的可执行文件。它检查 Magic、版本和头部长度，拒绝需要解释器的 `PT_INTERP`。检查程序头表时，先确认 `e_phoff` 在文件内，再用剩余字节数除以单个表项大小，与 `e_phnum` 比较。这样，即使输入中的偏移或数量异常，边界判断本身也不会因加法或乘法溢出而失效。

对每个 `PT_LOAD`，装载器要求 `p_filesz <= p_memsz`，文件区间有效，虚拟地址与文件偏移满足对齐约束，段的字节区间有序、互不重叠且止于预留栈区之前。程序入口还须按两字节对齐并落在可执行段内。随后按段标志建立 VMA，为用户可访问的页设置 `PTE_U`；可执行页设置 `PTE_X`，可写页按 Sv39 的叶子项编码同时设置 `PTE_R` 和 `PTE_W`。

每张新分配的物理页先整体清零。复制时只写入该页与文件数据区间 `[p_va, p_va + p_filesz)` 的交集。因此，即使文件数据结束在页中间，该页剩余的 BSS 字节和后续纯 BSS 页也都保持为零。两个段的字节范围可以相邻而共用一张边界页。此时装载器复用已有物理页，只合并该页所需的权限并写入后一段的数据，前一段已装入的字节得以保留。

**（4）建立用户栈并替换映像。** 在 `USTACKTOP` 下方登记 256 页可读写栈区，最低一页只登记虚拟范围，保留为空映射，以便在栈越界时触发页故障。其余 255 页立即分配并清零。`do_execve()` 先在旧地址空间中校验参数、保存名称。读取用户提供的 ELF 时，除检查 VMA 范围，还逐页检查有效位、用户位和读权限，再通过内核映射读取数据。落在栈区保护页内的源指针也会在读取前被识别。

完成全部映射后，`load_icode()` 设置首次返回的寄存器现场。清除 `SPP` 使 `sret` 返回 U 态，清除 `SIE` 使恢复期间保持关中断，设置 `SPIE` 则使返回后的中断状态符合约定。

```c
tf->epc = elf.e_entry;
tf->gpr.sp = USTACKTOP;
tf->status = (read_csr(sstatus) | SSTATUS_SPIE | SSTATUS_SUM) &
             ~(SSTATUS_SPP | SSTATUS_SIE | SSTATUS_FS);
```

`do_execve()` 在关中断区增加新 `mm` 的引用，更新进程名称、PCB 和活动 Trap 帧，将 Sv39 模式与根页表物理页号写入 `satp`，刷新 TLB 并执行 `fence.i`。此时进程已经可以从新页表取指，便可减少旧 `mm` 的引用，计数归零时释放其用户映射、页表和描述符。若装载中途失败，只需回收尚未安装的新地址空间，原程序仍可取得错误码并继续执行。

**（5）从内核栈返回 U 态。** 首次装载成功后，`kernel_execve()` 关中断并调用 `forkrets(current->tf)`。`forkrets` 将内核 `sp` 指向 Trap 帧，跳至 `__trapret` 恢复 `sstatus` 和 `sepc`。返回 U 态时，汇编将帧末端的内核栈顶写入 `sscratch`，最后恢复帧中的用户 `sp` 并执行 `sret`。用户程序从 ELF 入口开始运行，下一次执行 `ecall` 时即可切回自己的内核栈。[Trap 返回汇编](../code/kern/trap/trapentry.S)

#### 4.2.4 Spec 与 Prompt

模块二以装载成功时的用户现场和装载失败时的原进程状态作为验收依据。[完整开发提示词](prompt.md#lab5-user-elf)

| 提示词部分 | 设计要求与实现对应 |
| --- | --- |
| `[PROMPT]` | 复用 Lab4 的内核线程和调度机制，完成第一个用户进程的创建、装载及 U 态切换 |
| `[RELY]` | 按工程的 ELF 结构、用户地址布局、Sv39 页表和 Trap 返回汇编确定段映射方式及寄存器约定 |
| `[GUARANTEE]` | 保留测试镜像选择，按段设置权限、清零 BSS、建立栈页；先完成新映像，再安装页表并释放旧引用 |
| `[SPECIFICATION]` | 分别检查合法 ELF、非法 ELF、分配失败、特权级切换和已有用户进程替换程序五种情形 |

**Pre-Condition：** 内核线程、调度、物理页分配、VMA 管理和 Trap 返回路径已建立，当前线程具有独立内核栈和有效 Trap 帧。

**Post-Condition：** 成功时新页表映射程序段和用户栈，进程保留原 PID，从 ELF 入口以 U 态执行。失败时调用者取得错误码，新映像已分配的资源得到回收。

**Cases：** 对合法镜像检查入口、段权限、非页对齐的文件末尾和纯 BSS 页。对异常镜像检查 Magic、程序头截断、偏移溢出和用户地址越界。对分配失败和程序替换，分别检查空闲页数能否恢复，以及原程序能否在失败后继续执行并再次调用 exec。

#### 4.2.5 实现及验证过程

**（1）构建与用户态执行。** 在 WSL 的 `code` 目录中交叉编译、链接内核与用户程序，生成 `bin/kernel` 和 `bin/ucore.img`。分别选择 `hello` 和 `testbss` 启动 QEMU，读取串口输出与异常帧。

```bash
make -s -j4
make -s -j4 build-hello
timeout 5s make -s qemu
make -s -j4 build-testbss
timeout 5s make -s qemu
```

| 测试程序 | 关键输出 | 实验结果 |
| --- | --- | --- |
| [hello](../code/user/hello.c) | `I am process 2.`、`hello pass.` | 用户程序成功装载并在 U 态执行，通过 `getpid` 取得自身 PID，通过字符输出系统调用打印上述两行 |
| [testbss](../code/user/testbss.c) | `testbss may pass.`，随后记录 `badvaddr 0x00c02000`、`cause 0x0000000f` | 程序逐项确认 4 MiB BSS 数组的初值为零，再将各元素的下标写入并读回核对。最后按测试设计执行越界写入，触发存储页故障（`scause = 0xf`） |

**（2）exec 参数、回滚与映像替换。** 专项用户程序从已有地址空间调用 exec，依次构造异常输入、触发内存压力，再以合法 ELF 替换自身。这样可以在同一次运行中观察错误码、失败后的执行位置和成功后的进程身份。

| 检查项 | 观察结果 |
| --- | --- |
| 地址和 ELF 边界 | 指向栈保护页的参数返回 `-E_INVAL`（`-3`）。坏 Magic、程序头截断、非法文件偏移、文件段大于内存段、段侵入栈区及地址溢出，均返回 `-E_INVAL_ELF`（`-8`） |
| 内存不足 | 连续两次装载均返回 `-E_NO_MEM`。内核在每次装载前和失败返回时读取空闲页数，两组读数均为 `31273 → 31273`，原程序随后继续执行 |
| 程序替换 | 同一进程再次提交合法的 `hello` 镜像，其中两个程序段共用一张边界页。新程序打印 `I am process 2.` 和 `hello pass.`，表明边界页上的代码可执行，替换前后的 PID 一致 |

### 4.3 用户进程 fork 主流程

**负责人：** 2411566-罗晟皓（fork 主流程）

#### 4.3.1 模块功能描述

用户程序执行一次 `fork()`，成功时父子进程都从这次系统调用之后继续运行。父进程取得子 PID，子进程取得 0，因而可以在同一段用户代码中选择不同分支。内核为子进程分配独立的 PCB、内核栈和 Trap 帧，以便父子分别保存寄存器、陷入内核和接受调度。设置 `CLONE_VM` 时，父子进程共享同一地址空间。普通 fork 分配独立根页表，再调用 `dup_mmap` 请求复制用户映射。[fork 主流程](../code/kern/process/proc.c)

#### 4.3.2 关键接口与设计

| 数据结构或接口 | 定义位置 | 本模块中的作用 |
| --- | --- | --- |
| `sys_fork()`、`do_fork()` | [syscall.c](../code/kern/syscall/syscall.c)、[proc.c](../code/kern/process/proc.c) | `sys_fork` 传入标志 0、父用户栈指针和活动 Trap 帧。`do_fork` 准备子进程资源并在成功后登记 PID |
| `copy_mm()`、`CLONE_VM`、`mm_count` | [proc.c](../code/kern/process/proc.c)、[unistd.h](../code/libs/unistd.h)、[vmm.h](../code/kern/mm/vmm.h) | 根据父 `mm` 和标志选择启动页表、共享页表或私有页表。每个使用用户地址空间的进程持有一个 `mm` 引用 |
| `dup_mmap(to, from)`、`copy_range()` | [vmm.c](../code/kern/mm/vmm.c)、[pmm.c](../code/kern/mm/pmm.c) | 普通 fork 将父 VMA 和映射的复制交给 `dup_mmap`；`copy_range` 是页表层的区间复制接口。私有副本要求父子的可写用户页能够独立修改 |
| `copy_thread()`、`forkret()`、`forkrets()` | [proc.c](../code/kern/process/proc.c)、[trapentry.S](../code/kern/trap/trapentry.S) | `copy_thread` 设置子 Trap 帧和首次调度现场。`forkret` 取当前子帧，`forkrets` 将其交给 Trap 返回汇编 |

#### 4.3.3 具体实现

**（1）分配 PCB、内核栈与地址空间。** `do_fork()` 先检查 `MAX_PROCESS`，再分配 PCB、设置 `parent = current` 并申请两页独立内核栈。子进程此时处于 `PROC_UNINIT`，调度器还无法从进程链表找到它。`copy_mm()` 接着按三种条件处理页表和引用。

| 条件 | 地址空间与引用处理 |
| --- | --- |
| 父进程的 `mm` 为空 | 子进程沿用 `alloc_proc` 设置的启动页表物理地址，`mm` 为空。启动页表由内核管理 |
| 父进程有 `mm`，设置 `CLONE_VM` | 在关中断区增加父 `mm` 的引用计数，把同一个 `mm` 和根页表物理地址写入子 PCB。父子访问同一用户页时可看到对方的写入 |
| 父进程有 `mm`，普通 fork | `mm_create` 创建私有描述符，`setup_pgdir` 建立独立根页表。在父 `mm` 锁保护下调用 `dup_mmap(new_mm, parent_mm)`。成功后把新引用交给子 PCB，返回错误时销毁本次分配的私有资源 |

**（2）从同一个用户 PC 返回两次。** 用户态 `ecall` 的异常处理先将父帧的 `epc` 增加 4，再分发系统调用。因此复制父帧即可保留下一条用户指令的位置。子帧位于子内核栈顶，其中的系统调用返回值单独置零。[异常入口](../code/kern/trap/trap.c)

```c
struct trapframe *child_tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
*child_tf = *tf; // The syscall trap has already advanced epc past ecall.
child_tf->gpr.a0 = 0;
```

传入用户栈指针时，`copy_thread()` 将它写入子帧；用户帧未指定新栈时沿用原 `sp`。对于以零栈参数启动的 S 态内核线程，则从子帧地址向低地址使用自己的内核栈。随后设置 `context.ra = forkret`、`context.sp = child_tf`。子进程首次切换经 `forkret → forkrets → __trapret → sret` 恢复自己的帧，父进程则由系统调用分发器把 `do_fork` 的结果写回父帧的 `a0`。[系统调用返回值](../code/kern/syscall/syscall.c)

**（3）登记进程与撤销失败分配。** 获取父 `mm` 锁期间可能发生调度，`do_fork()` 因此在关中断区再次检查进程上限，再分配唯一 PID。`hash_proc()` 将子 PCB 加入 PID 哈希表，`set_links()` 将它加入进程链表、接入父进程的子女与兄弟链并更新进程计数。最后由 `wakeup_proc()` 将子进程置为可运行。此后父进程按返回的 PID 查找，能得到已具备完整现场的子进程。[进程登记](../code/kern/process/proc.c) · [唤醒与调度](../code/kern/schedule/sched.c)

| 失败位置 | 处理方式 |
| --- | --- |
| 初次检查已达进程上限，或 PCB 分配失败 | 分别返回 `-E_NO_FREE_PROC` 或 `-E_NO_MEM`，进程表维持原状 |
| 子内核栈分配失败 | `do_fork` 释放已分配的 PCB |
| 创建私有 `mm`、根页表或复制映射失败 | `copy_mm` 回收自己取得的资源并传回原错误码。已有根页表时，`exit_mmap` 先解除已登记 VMA 的映射并释放中间页表，`put_pgdir` 释放根页，`mm_destroy` 释放 VMA 和描述符。外层 `do_fork` 回收子内核栈和 PCB |
| 最终登记前达到进程上限 | 撤销子进程取得的 `mm` 引用，计数归零时销毁地址空间，再回收子内核栈和 PCB |

#### 4.3.4 Spec 与 Prompt

模块三把子进程何时可见、由谁持有地址空间引用，以及父子分别恢复哪一份 Trap 帧，列为 fork 的设计约束。[完整开发提示词](prompt.md#lab5-fork-mm)

| 提示词部分 | 设计要求与实现对应 |
| --- | --- |
| `[PROMPT]` | 以 `do_fork`、`copy_mm`、`copy_thread` 组织创建和失败回滚，沿用既有内核线程、exec 和调度入口 |
| `[RELY]` | 依据 PCB 字段、`mm_count`、VMA 复制及清理接口确定资源归属，根据 Trap 返回汇编布置子现场 |
| `[GUARANTEE]` | 新建独立内核栈，按标志共享或复制地址空间，分设父子返回值，完整初始化后在临界区登记并唤醒 |
| `[SPECIFICATION]` | 检查内核线程、`CLONE_VM`、普通 fork、父子执行现场和分配或复制失败五类情形 |

**Pre-Condition：** `current` 指向有效 PCB，已有内核栈分配、用户程序装载和调度机制。用户 fork 的输入帧保存了系统调用后的 PC 和父用户栈指针。

**Post-Condition：** 成功时父进程取得唯一子 PID，子进程恢复到相同用户 PC 并取得 0。共享分支新增一个 `mm` 引用，普通 fork 的成功条件包括独立根页表和独立可写用户页。失败时撤销子进程取得的资源，保留父进程地址空间和执行现场。

**Cases：** 内核线程沿用启动页表；共享分支检查引用增量和写入可见性；普通 fork 检查复制接口返回值与私有资源归属。上下文用例核对 PC、SP 和 `a0`。资源边界覆盖 PCB、内核栈、`mm`、根页表、映射复制和进程数量上限。

#### 4.3.5 实现及验证过程

**（1）启动与用户程序回归测试。** 使用 `make -s -j4` 编译并链接全工程，构建 `build-hello` 目标后，以 `timeout 4s make -s qemu` 启动内核。`init_main` 创建的子线程获得调度后装载 `hello`。串口先记录 `kernel_execve: pid = 2, name = "hello".`，随后输出 `I am process 2.` 和 `hello pass.`，验证了从内核线程启动到用户程序执行的路径。

**（2）共享地址空间与用户现场。** 用户 `fork()` 固定传入标志 0，因此共享用例从内核调用 `do_fork(CLONE_VM, 0, tf)`，用 `proc_run()` 分别恢复父子进程。测试核对两者的 `mm`、页表根和子帧内容，再在 U 态读取返回值并访问同一个变量。引用计数从 1 增至 2，新增的两页物理内存用于子内核栈。串口记录如下。

```text
U child a0=0, saved sp and a4 restored, pid=3
U parent pid=2, fork result=3, shared write=3
```

子进程从 `ecall` 的下一条指令恢复，检查原 `sp` 与寄存器 `a4`，再把自己的 PID 写入共享变量。父进程取得子 PID 3，也从该变量读到 3，分别验证了系统调用返回值和映射共享。

**（3）失败回滚与容量边界。** 分别触发 PCB、`mm_struct`、内核栈和根页表分配失败。四种情况下，父进程的地址空间引用计数均保持为 1，Trap 帧和子进程链保持原值。进程总数与 PID 哈希项数始终分别为 3 和 2，未登记新的子进程。各用例的空闲页数及容量边界结果见下表。

| 测试条件 | 观察结果 |
| --- | --- |
| 分别对 PCB 和 `mm` 的堆分配注入失败 | 两次均返回 `-E_NO_MEM`，空闲页数保持 31292，堆对象净增量为零 |
| 实际占用物理页至只剩 1 页或 2 页 | 分别在分配子内核栈和根页表时返回 `-E_NO_MEM`，失败前后的空闲页数分别为 `1 → 1` 和 `2 → 2` |
| 构造一个含 VMA 和两张用户页的私有 `mm`，直接调用清理函数 | 空闲页数恢复至 31292，验证了已登记映射和页表的释放路径 |
| 创建进程直至总数为 4096 | 再次创建返回 `-E_NO_FREE_PROC`（`-5`），空闲页数保持 22815，PID 哈希项数保持 4095。PID 0 的 idle 计入进程总数，位于哈希表之外 |

---

### 4.4

**负责人：**

#### 4.4.1 模块功能描述

#### 4.4.2 关键接口与设计

#### 4.4.3 具体实现

#### 4.4.4 Spec 与 Prompt

#### 4.4.5 实现及验证过程

## 五、测试与验证

**（1）构建与评分命令。**

```bash
cd code
make -j4
make grade
```

**（2）测试结果。**

**（3）串口关键输出。**

**（4）GDB 验证。** 

**（5）回归与未覆盖项。**

**测试截图：**

---

## 六、实验总结与收获

### 对操作系统的理解

### AI 协作开发的经验

---
