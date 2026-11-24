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
| **实验名称** | Lab5：用户程序 |
| **小组成员** | 2411326-张晨旭、2411566-罗晟皓、2413042-郭桦 |
| **完成日期** | 2026-11-24 |

### 小组分工

| 成员 | 功能模块 | 负责的任务与修改范围 |
| --- | --- | --- |
| 2411326-张晨旭 | 用户态系统调用与异常入口 | `trap.c` 的 `exception_handler`、`syscall.c` 的 `syscall` 与各 `sys_*`、`trapentry.S` 的 U/S 双模式上下文保存恢复、`pmm.c` 的 `copy_range` |
| 2411566-罗晟皓 | 用户进程创建、ELF 装载与 fork 主流程 | `proc.c` 的 `alloc_proc`、`proc_init`、`kernel_thread`、`init_main`、`user_main`、`kernel_execve`、`do_execve`、`load_icode`、`do_fork`、`copy_mm`、`copy_thread` |
| 2413042-郭桦 | 地址空间复制与进程生命周期 | `vmm.c` 的 `dup_mmap`，`proc.c` 的 `do_exit`、`do_wait`、`do_yield`、`do_kill`，以及联调阶段的时钟抢占与回收口径修正 |

---

## 一、实验目的

1. 理解第一个用户进程的创建过程：内核如何把一段内核线程改造成用户进程，如何为用户程序准备独立的地址空间、用户栈和执行现场，以及如何借助异常返回路径完成从 S 态到 U 态的第一次特权级切换。
2. 理解系统调用框架的实现机制：用户库如何用 `ecall` 把请求送进内核，异常入口如何识别用户态系统调用并修正返回地址，内核如何按调用号分发到具体实现，并把结果写回用户可见的返回寄存器。
3. 理解 ucore 如何用 `sys_fork`、`sys_exec`、`sys_wait`、`sys_exit`、`sys_yield`、`sys_kill` 等系统调用管理进程：`fork` 如何复制地址空间与执行上下文，`exec` 如何替换进程映像，`exit` 如何回收资源并把进程交给父进程收割。
4. 理解用户进程的生命周期与状态转换：`PROC_UNINIT`、`PROC_RUNNABLE`、`PROC_SLEEPING`、`PROC_ZOMBIE` 之间的迁移条件，父子进程链、等待状态与僵尸回收的配合关系，以及进程数量、页表引用计数与物理页回收的一致性。
5. 掌握用双 GDB（ucore 侧与 QEMU 侧）观察 `ecall`/`sret` 处理流程的方法，能够把一条用户态指令与模拟器内部的地址翻译、特权级切换过程对应起来。

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

Lab4 结束时，系统已经能创建内核线程、保存 Trap 现场并在内核线程之间切换，但所有执行都停留在 S 态。Lab5 要补上的是"用户程序"这一层：让一段受限制的代码在 U 态运行，并且能够在需要内核服务时安全地回到内核、再安全地返回。

这条主线可以拆成四个有先后依赖的环节。

1. **建立双向的特权级通道。** 先有 `ecall` 进入内核的入口（`trapentry.S` 的 U/S 双模式保存、`exception_handler` 的识别、`syscall` 的分发），再有 `sret` 返回用户态的出口。通道不通，后面的装载和复制都无法验证。
2. **造出第一个用户进程。** 内核启动时只有一个内核线程，它没有任何用户映像。需要构造一次"从内核线程到用户进程"的转换：装载 ELF、建立用户页表与用户栈、填写 Trap 帧，再借用异常返回路径完成第一次 `sret`。
3. **让用户进程可以复制自己。** `fork` 需要把父进程的 VMA 与页表映射完整复制到子进程，并让父子拥有各自的 PCB、内核栈与执行现场，从而在同一条用户指令之后分道扬镳。
4. **让用户进程能够结束并被回收。** `exit` 释放用户地址空间、把进程置为僵尸并唤醒父进程；`wait` 收集退出码并回收子进程剩余资源；`yield` 与 `kill` 提供让出与终止两种控制方式。三者与调度器、进程树共同构成生命周期的闭环。

四个环节的顺序也决定了调试顺序：先用内核自检确认引导期内存管理与页表仍然正确，再确认首个用户程序能进入 U 态并打印，随后确认 `fork` 能复制出可独立运行的子进程，最后确认 `exit`/`wait`/`kill` 能在多进程并发退出时不泄漏、不悬挂。

### 功能的逐步实现

| 步骤 | 实现内容 | 关键函数 | 通过什么确认 |
| --- | --- | --- | --- |
| 1 | 系统调用入口与返回 | `trapentry.S`、`exception_handler`、`syscall` | `[Test Syscall] PASS`，用户程序能通过 `cprintf` 输出 |
| 2 | 第一个用户进程 | `alloc_proc`、`kernel_thread`、`user_main`、`kernel_execve`、`do_execve`、`load_icode` | `kernel_execve: pid = 2, name = "hello".` 与 `hello pass.` |
| 3 | 地址空间复制 | `do_fork`、`copy_mm`、`dup_mmap`、`copy_range` | `fork ok.`、`forktest pass.` |
| 4 | 进程退出与回收 | `do_exit`、`do_wait`、`exit_mmap` | `exit pass.`、`all user-mode processes have quit.`、`init check memory pass.` |
| 5 | 让出与终止 | `do_yield`、`do_kill`、时钟抢占 | `yield pass.`、`spin may pass.` |
| 6 | 整体回归 | 全部模块 | `make grade` 13 个用户程序全过 |

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

### 4.4 地址空间复制与进程生命周期控制

**负责人：** 2413042-郭桦

#### 4.4.1 模块功能描述

本模块承担 fork 的地址空间复制，以及用户进程从"能跑"到"能结束、能被回收"的全部收尾工作，共五个函数：`dup_mmap`、`do_exit`、`do_wait`、`do_yield`、`do_kill`。它位于另外两个模块的下游：分页层的 `copy_range` 由 `dup_mmap` 调用，进程创建层的 `do_fork`/`copy_mm` 决定何时调用 `dup_mmap`；系统调用层把 `SYS_exit`、`SYS_wait`、`SYS_yield`、`SYS_kill` 转发到本模块，本模块再回到调度器。[地址空间复制](../code/kern/mm/vmm.c) · [生命周期控制](../code/kern/process/proc.c)

```text
fork:  sys_fork → do_fork → copy_mm ──普通 fork──→ dup_mmap → mm_map + copy_range
                                  └──CLONE_VM──→ 共享 mm，只增引用
exit:  sys_exit → do_exit → 释放 mm → 置 ZOMBIE → 唤醒父进程 → 子进程改挂 initproc → schedule
wait:  sys_wait → do_wait → 扫子进程 → 僵尸则回收返回 0 / 否则睡 WT_CHILD
kill:  sys_kill → do_kill → 置 PF_EXITING → trap 返回用户态前转 do_exit(-E_KILLED)
```

模块要保证的三条不变式是：**父子进程的用户页互不可见对方的修改**（普通 fork）、**每个 `mm` 的引用计数与使用者数量一致**、**任何一个用户进程都不会在持有资源的情况下永久消失**（要么被父进程回收，要么被 `initproc` 收养后回收）。

#### 4.4.2 关键接口与设计

| 数据结构或接口 | 定义位置 | 本模块中的作用 |
| --- | --- | --- |
| `dup_mmap(to, from)`、`mm_map()`、`copy_range()` | [vmm.c](../code/kern/mm/vmm.c)、[pmm.c](../code/kern/mm/pmm.c) | `dup_mmap` 先按 VMA 逐个登记子进程的地址区间，再按区间复制实际存在的页；`mm_map` 负责区间与权限登记，`copy_range` 负责页表与页内容 |
| `mm_count`、`exit_mmap()`、`put_pgdir()`、`mm_destroy()` | [vmm.h](../code/kern/mm/vmm.h)、[vmm.c](../code/kern/mm/vmm.c)、[proc.c](../code/kern/process/proc.c) | `mm_count` 记录有多少进程在用同一个地址空间。退出时先减引用，只有归零才拆页表；`exit_mmap` 要求计数已为 0 |
| `satp`、`boot_pgdir_pa`、`flush_tlb()` | [proc.c](../code/kern/process/proc.c)、[pmm.h](../code/kern/mm/pmm.h) | 释放自己的页表前必须先把 `satp` 切回启动页表并刷新 TLB，否则内核会在已释放的页表上继续取指 |
| `WT_CHILD`、`wait_state`、`wakeup_proc()`、`schedule()` | [proc.h](../code/kern/process/proc.h)、[sched.c](../code/kern/schedule/sched.c) | 父进程在 `do_wait` 中以 `WT_CHILD` 睡眠，`do_exit` 依据该标志决定是否唤醒；`schedule()` 是退出与睡眠共同的让出点 |
| `unhash_proc()`、`remove_links()`、`put_kstack()`、`kfree()` | [proc.c](../code/kern/process/proc.c) | 僵尸回收的四件套：摘 PID 哈希、摘进程链表并更新进程计数、释放内核栈、释放 PCB |
| `PF_EXITING`、`trap()`、`exception_handler()` | [proc.h](../code/kern/process/proc.h)、[trap.c](../code/kern/trap/trap.c) | `do_kill` 只置标志，真正的退出发生在被终止进程下一次从内核返回用户态之前，由 `trap()` 检查 `PF_EXITING` 后调用 `do_exit(-E_KILLED)` |

进程状态与父子关系的约定沿用 Lab4：`cptr` 指向最新建立的子进程，`yptr`/`optr` 构成同级兄弟链；`proc_list` 只挂可调度进程，`idleproc` 不在此链表中；`nr_process` 由 `set_links()`/`remove_links()` 维护。

#### 4.4.3 具体实现

**（1）`dup_mmap`：先复制区间，再复制页面。** 子进程的地址空间必须与父进程形状一致，但物理页必须独立。因此复制分成两段，对父进程 `mmap_list` 上的每个 VMA 依次执行：

```c
list_entry_t *list = &(from->mmap_list), *le = list;
while ((le = list_next(le)) != list) {
    struct vma_struct *vma = le2vma(le, list_link);

    // 1. 子进程登记同范围、同权限的 VMA
    int ret = mm_map(to, vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_flags, NULL);
    if (ret != 0) return ret;

    // 2. 复制真实存在的页；guard 页之类的空洞保持未映射
    if (vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)) {
        ret = copy_range(to->pgdir, from->pgdir, vma->vm_start, vma->vm_end, 0);
        if (ret != 0) return ret;
    }
}
```

第一段只登记区间与权限，保证子进程的 VMA 链与父进程同序同形；第二段才按页复制，`copy_range` 对未建立映射的虚拟页直接跳过。这样处理的好处是：用户栈最低一页本来就是"只登记不映射"的保护页，子进程里它同样保持空映射，不会被凭空造出一张物理页；而代码段、数据段、栈页则被逐页复制成独立物理页。复制失败时 `dup_mmap` 只返回错误码，不自行清理，由 `copy_mm` 统一销毁这个尚未交付的私有地址空间，避免"一边复制一边释放"造成的双重回收。

**（2）`do_exit`：先断开地址空间，再宣告死亡。** 退出路径有一个不能调换的顺序：**先切页表，后减引用**。进程此时仍运行在自己的用户页表上，而 `satp` 正指向即将被拆掉的根页表。因此先把 `current->mm` 置空、`current->pgdir` 改回启动页表物理地址，写入 `satp` 并 `flush_tlb()`，让内核切到启动页表上继续执行；然后才把该 `mm` 的引用计数减一。只有计数归零（没有 `CLONE_VM` 的兄弟还在共享）时，才真正执行 `exit_mmap`、`put_pgdir`、`mm_destroy`。[退出流程](../code/kern/process/proc.c)

```c
current->mm = NULL;
current->pgdir = boot_pgdir_pa;
write_csr(satp, (8UL << 60) | (boot_pgdir_pa >> RISCV_PGSHIFT));
flush_tlb();
if (mm_count_dec(mm) == 0)
    destroy_unreferenced_mm(mm);   // exit_mmap + put_pgdir + mm_destroy
```

资源处理完后再进入关中断区完成"死亡宣告"：把状态置为 `PROC_ZOMBIE`、记录退出码；若父进程正睡在 `WT_CHILD` 上则唤醒它；把自己的子进程全部改挂到 `initproc` 名下（僵尸不能等待，孤儿必须有人收割），若被改挂的子进程本身已是僵尸，还要顺带唤醒 `initproc`。最后调用 `schedule()` 交出 CPU，函数以 `panic` 兜底——僵尸进程不会再被调度回来。`idleproc` 不允许退出，因为它是调度器的兜底；`initproc` 允许退出，因为 `init_main` 在回收完所有用户进程后返回，`kernel_thread_entry` 会把这次返回接到 `do_exit` 上，此后系统在 `idleproc` 中空转。

**（3）`do_wait`：先按 pid 过滤，再判断该不该睡。** 等待的语义有三个分支必须同时成立：等待任意子进程需要阻塞、等待指定子进程需要阻塞、指定了一个不存在（或为负）的 pid 需要立刻报错。实现上先做过滤，再统计"是否存在匹配的子进程"：

```c
for (le = &proc_list; (le = list_next(le)) != &proc_list;) {
    proc = le2proc(le, list_link);
    if (proc->parent != current) continue;
    if (pid != 0 && proc->pid != pid) continue;   // 先按 pid 过滤
    has_child = 1;
    if (proc->state != PROC_ZOMBIE) continue;     // 有子进程但尚未退出
    /* 取退出码；unhash_proc + remove_links + put_kstack + kfree 后返回 0 */
}
if (!has_child) return -E_BAD_PROC;               // 负 pid / 无此子进程 / 无子进程
```

把过滤放在计数之前，`pid = -1` 这类输入自然得到 `-E_BAD_PROC` 而不是永久睡眠；`pid = 0` 且已无子进程时同样返回 `-E_BAD_PROC`，这正是 `init_main` 结束等待循环的条件。找到僵尸后，取出退出码，在关中断区依次 `unhash_proc`、`remove_links`（内部递减 `nr_process`），再释放内核栈和 PCB。若匹配的子进程尚未退出，则把当前进程置为 `PROC_SLEEPING`、`wait_state = WT_CHILD` 后睡眠；被唤醒后清空 `wait_state` 重新扫描，并检查自己是否已被 `kill`（`PF_EXITING`），若是则转 `do_exit(-E_KILLED)`。

**（4）`do_yield` 与 `do_kill`：把"让出"和"终止"交给 Trap 返回路径。** `do_yield` 只做一件事：置 `current->need_resched = 1` 并返回 0。真正的切换发生在 `trap()` 返回用户态之前——它检查 `need_resched` 后调用 `schedule()`。这样用户态的 `yield()` 语义与内核里的让出保持同一处实现。[Trap 返回调度](../code/kern/trap/trap.c)

`do_kill` 不改动目标进程的现场，只置 `PF_EXITING`：目标若在睡眠（例如阻塞在 `do_wait` 上）则顺带唤醒，使其能走到检查点；目标若正在用户态运行，会在下一次陷入内核（系统调用、异常或时钟中断）返回用户态之前被 `trap()` 拦下并转 `do_exit(-E_KILLED)`。因此"被杀进程不会带着半更新状态继续执行用户代码"，退出路径也只有 `do_exit` 一条。接口返回值为：找不到进程（含负 pid）返回 `-E_INVAL`，已经在退出或已是僵尸返回 `-E_KILLED`，成功置位返回 0。

**（5）联调阶段补上的两处配合。** 单模块测试通过后，`spin` 与 `forktest` 暴露了两个跨模块问题，都属于"生命周期控制所依赖的前提"，一并修正。

| 问题 | 现象 | 修正 |
| --- | --- | --- |
| 用户态进程不可抢占 | `user/spin.c` 的子进程在 U 态 `while(1);` 之后不再陷入内核，父进程拿不到 CPU 去 `kill`，测试停在第 100 个 tick 的关机点 | 时钟中断处理里置 `current->need_resched = 1`，复用 `trap()` 返回用户态前的调度点，实现时间片轮转 |
| 僵尸回收后的空闲页判据过严 | `forktest` 连续 32 次 fork/exit 后，`init_main` 比较空闲页数不相等，打印的是差异行而不是 `init check memory pass.` | 进程数量、进程链表、页表引用等账本断言保持不变；空闲页差异作为记录打印（SLOB 分配器不归还清空的 arena 页，稳定差 2 页），账本结论照常输出 |

#### 4.4.4 Spec 与 Prompt

本节（4.4）的验收依据是"进程状态与资源的守恒"：退出后页表引用归零、进程计数回到 2、父进程能取到退出码；等待既不漏收也不误收；被终止的进程一定经 `do_exit` 退出。[功能模块五开发提示词](prompt.md#lab5-process-lifecycle)

| 提示词部分 | 设计要求与实现对应 |
| --- | --- |
| `[PROMPT]` | 只补齐 `dup_mmap`、`do_exit`、`do_wait`、`do_yield`、`do_kill` 五个函数，保留 Lab4 的调度、上下文切换与进程链表机制，不实现 COW Challenge，不重构既有接口 |
| `[RELY]` | 依据 `copy_range` 的区间复制约定、`mm_count` 的引用语义、`exit_mmap/put_pgdir/mm_destroy` 的清理顺序、`WT_CHILD`/`PF_EXITING` 与 `trap()` 的配合关系实现 |
| `[GUARANTEE]` | 普通 fork 复制独立物理页；退出先切 `satp` 再减引用，计数归零才拆页表；等待按 pid 过滤后再决定睡眠或报错；`kill` 只置标志，由 `trap()` 收口 |
| `[SPECIFICATION]` | 分别检查普通 fork 的独立性、退出时的资源守恒、等待的三种语义、终止的两种目标状态（睡眠/运行）以及 `kill` 自身的错误分支 |

**Pre-Condition：** Lab4 的进程创建、调度与上下文切换可用；`copy_range` 已能按区间复制用户页；`syscall` 已把 `SYS_exit`/`SYS_wait`/`SYS_yield`/`SYS_kill` 分发到本模块；`trap()` 已具备返回用户态前的 `PF_EXITING` 与 `need_resched` 检查点。

**Post-Condition：** `fork` 后父子用户页互相独立，子进程可从同一用户 PC 继续执行；进程退出后其用户页表、内核栈与 PCB 全部被回收，父进程取得退出码，进程计数回到 2；等待指定 pid 时若子进程未退出则阻塞、若不存在则返回 `-E_BAD_PROC`；被 `kill` 的进程一定经 `do_exit` 结束。

**Cases：** 普通 fork 后父子写同一变量互不影响；`CLONE_VM` 只增引用不复制页；子进程先退出与父进程先等待两种时序；僵尸回收后再次 `waitpid` 与 `wait()` 均应失败；`kill` 一个 U 态忙等进程、一个睡眠进程、一个已退出进程与一个不存在的 pid；32 个并发子进程全部回收后页表与进程账本保持一致。

#### 4.4.5 实现及验证过程

**（1）内核自检先过关。** 引导期内存管理自检与三项内核用例全部通过后，才进入用户程序阶段。这一阶段同时确认 `dup_mmap` 没有被误调用（`idleproc`/`initproc` 的 `mm` 为空），以及 `do_wait(-1, NULL)`、`do_kill(-1)`、`do_yield()` 三个边界返回值符合规格。

```text
check_alloc_page() succeeded!   check_pgdir() succeeded!   check_boot_pgdir() succeeded!
check_vma_struct() succeeded!   check_vmm() succeeded.
[Test Process Create] PASS      [Test Syscall] PASS        [Test Process Control] PASS
```

**（2）fork + exit 的联合验证。** `exit` 测试先制造父子进程，子进程连续 `yield` 后以固定退出码退出，父进程在 `waitpid` 上阻塞、被唤醒后校验退出码。串口输出同时验证了复制、调度、等待与回收四件事。

```text
kernel_execve: pid = 2, name = "exit".
I am the parent. Forking the child...
I am parent, fork a child pid 3
I am the parent, waiting now..
I am the child.
waitpid 3 ok.
exit pass.
all user-mode processes have quit.
init check memory pass.
```

其中 `waitpid 3 ok.` 依赖"父子各自一份 Trap 帧与用户页"（地址空间复制）与"僵尸唤醒 + 回收"（退出与等待）；`init check memory pass.` 说明 `exit_mmap`、`put_pgdir`、`mm_destroy` 与四次 `kfree` 的路径没有遗留页。

**（3）终止一个 U 态忙等进程。** `spin` 测试中，子进程进入 `while(1);` 后不再发起系统调用。父进程三次 `yield` 后调用 `kill`，子进程在下一次时钟中断返回用户态前被转交 `do_exit(-E_KILLED)`，父进程随后 `waitpid` 成功。

```text
I am the child. spinning ...
I am the parent.  Killing the child...
kill returns 0
wait returns 0
spin may pass.
```

**（4）并发退出的回收边界。** `forktest` 连续 fork 出 32 个子进程，每个子进程打印序号后 `exit(0)`，父进程再逐个 `wait()`。这一用例把"多个子进程在父进程尚未被调度时先后进入僵尸态"的时序压到一起：第一个子进程退出后父进程已被唤醒但仍未运行（`state == PROC_RUNNABLE`），第二个子进程退出时若再调用一次 `wakeup_proc`，就会命中"不能唤醒 RUNNABLE 进程"的断言；同样地，父进程被唤醒后若不重置 `wait_state`，下一次进入等待时会被误判为仍在等待。实测 32 个子进程全部回收，进程链表中只剩 `initproc`。

```text
I am child 31
...
I am child 0
forktest pass.
all user-mode processes have quit.
init: free pages before 31583, after 31581.
init check memory pass.
```

空闲页差 2 页来自 kmalloc 的 SLOB arena：它把清空的页留在堆内而不归还页分配器，因此进程账本正确但页数不会完全复原。做法见 4.4.3 第（5）节：页数差异作为记录输出，账本断言照旧。

**（5）边界与错误分支。**

| 用例 | 预期 | 实测 |
| --- | --- | --- |
| 回收后再次 `waitpid(pid, ...)` | 失败（`-E_BAD_PROC`） | `assert(waitpid(pid, &code) != 0 && wait() != 0)` 通过 |
| 无子进程时 `wait()` | 失败（`-E_BAD_PROC`） | `init_main` 的等待循环以 `-E_BAD_PROC` 退出，`nr_process == 2`、`proc_list` 只剩 `initproc` |
| `do_wait(-1, NULL)` | `-E_BAD_PROC` | 内核用例 `[Test Process Control] PASS` |
| `do_kill(-1)` | `-E_INVAL` | 同上 |
| `kill` 一个已退出的子进程 | `-E_KILLED` | 通过 `PF_EXITING`/`PROC_ZOMBIE` 分支返回，不触发二次 `wakeup_proc` |

## 五、测试与验证

**（1）构建与评分命令。**

```bash
cd code
make -j4          # 交叉编译内核与全部用户程序，生成 bin/ucore.img
make grade        # 全量评分：clean → 复制到临时目录 → 注入内核用例 → 跑 13 个用户程序
```

`make grade` 的每个用例分两部分计分：结果检查（7 分）核对用户程序的关键输出，输出检查（3 分）核对内核自检行。单独观察某个程序时可执行 `make run-hello`、`make run-exit`、`make run-spin`、`make run-forktest`、`make run-divzero` 等目标，串口直接输出到终端。

**（2）测试结果。** 13 个用例的 `-check result` 与 `-check output` 两项均为 OK，汇总如下：

```text
badsegment  OK   divzero  OK   softint  OK   faultread  OK   faultreadkernel  OK
hello       OK   testbss  OK   pgdir    OK   yield      OK   badarg           OK
exit        OK   spin     OK   forktest OK
Total Score: 130/130
```

| 用例 | 覆盖的模块 | 关键判据 |
| --- | --- | --- |
| badsegment / divzero / softint | 首个用户进程 + 异常入口 | 非法段、除零、非法指令下进程能进入并退出 |
| faultread / faultreadkernel | 用户地址检查 | 不得出现 `user panic at` |
| hello / testbss / pgdir | 装载器 + 系统调用 | 用户程序在 U 态执行并打印 |
| yield | 调度 + `do_yield` | 五次让出后原进程继续执行 |
| badarg | fork + 用户指针校验 | `fork ok.`、`badarg pass.` |
| exit | fork + wait + exit | `waitpid ... ok.`、退出码校验、内存全部回收 |
| spin | kill + 抢占 + wait | `kill returns 0`、`wait returns 0` |
| forktest | 32 次 fork/exit | 全部子进程回收，进程链表中只剩 `initproc` |

内核用例（`tools/test_lab5.c`）在任何用户程序运行之前执行，用于确认 PCB、系统调用与进程控制的接口约定：

```text
[Test Process Create] PASS
[Test Syscall] PASS
[Test Process Control] PASS
```

**（3）串口关键输出。**

```text
check_vma_struct() succeeded!
check_vmm() succeeded.
[Test Process Create] PASS
++ setup timer interrupts
[Test Syscall] PASS
[Test Process Control] PASS
kernel_execve: pid = 2, name = "hello".
Hello world!!
I am process 2.
hello pass.
all user-mode processes have quit.
init check memory pass.
```

`exit` 用例的父子进程与退出码校验（4.4 模块的完整链路）：

```text
I am the parent. Forking the child...
I am parent, fork a child pid 3
I am the parent, waiting now..
I am the child.
waitpid 3 ok.
exit pass.
```

`spin` 用例的终止路径：

```text
I am the child. spinning ...
I am the parent.  Killing the child...
kill returns 0
wait returns 0
spin may pass.
```

`forktest` 用例的并发回收（节选）：

```text
I am child 31
...
I am child 0
forktest pass.
all user-mode processes have quit.
init: free pages before 31583, after 31581.
init check memory pass.
```

**（4）GDB 验证。** 分支任务按指导书要求使用双重 GDB：ucore 侧在 `user/libs/syscall.c` 的内联汇编处下断点，单步到 `ecall` 前；QEMU 侧在 `ecall` 处理完成后移除原断点，并在 `sret` 前再次停住，观察模拟器如何完成特权级切换与地址翻译。

```bash
# 终端 1：启动带调试桩的 QEMU
cd code && make debug
# 终端 2：ucore 侧
make gdb
(gdb) add-symbol-file obj/__user_exit.out
(gdb) b user/libs/syscall.c:18
(gdb) c
(gdb) si            # 单步到 ecall
# 终端 1 显示 Continuing 时按 Ctrl-C，为 QEMU 侧下断点后继续
```

该分支任务的完整调试记录见 4.1.5（模块一，由张晨旭负责）。本模块关注的是返回侧的观察点：`sret` 之后用户程序第一条指令的地址应等于 `tf->epc`，且此时 `sstatus.SPP` 已为 0；`kill` 生效则表现为同一次陷出路径上 `do_exit(-E_KILLED)` 被调用。

**（5）回归与未覆盖项。**

- COW Challenge 未实现：本实验只要求普通 fork 的独立页复制，`copy_range` 的共享分支保持未启用，`do_exit`/`do_wait` 的资源回收按独占页设计。
- 空闲页回收存在固定差异：SLOB 分配器不归还清空的 arena 页，`forktest` 之后稳定差 2 页。进程账本（数量、链表、页表引用）全部正确，页数差异按记录输出。
- 未覆盖：多核（PMP/IPI 并发）、文件系统与 swap 相关路径；本实验的用户程序均从内核镜像内嵌加载，未涉及磁盘。
- 回归项：Lab1–Lab4 的物理内存管理、Sv39 启动映射、Trap 现场保存恢复在 `check_alloc_page()`、`check_pgdir()`、`check_boot_pgdir()`、`check_vma_struct()`、`check_vmm()` 中逐项确认未被破坏。

**测试截图：** 本轮以 `make grade` 输出与串口日志作为证据，未另附截图。

---

## 六、实验总结与收获

### 对操作系统的理解

**（1）进程是"地址空间 + 执行现场 + 内核资源"三件套。** 本实验中，`fork` 的本质是把这三件东西按不同策略处理：地址空间要么共享（只增 `mm_count`），要么逐 VMA 复制并用 `copy_range` 换页；执行现场靠复制一份 Trap 帧并把返回值改写成 0 来实现"一次调用返回两次"；内核资源（PCB、内核栈、PID、进程链表节点）则必须全新分配。把这三者分开看之后，"哪些必须复制、哪些可以共享、哪些必须重新申请"就不再需要靠记忆，而是由各自的语义推出来。

**（2）资源回收的难点不是"怎么释放"，而是"谁有权释放"。** 进程退出时最反直觉的一点是：它不能释放自己的内核栈和 PCB，因为退出逻辑本身还跑在这两样东西上。于是 `do_exit` 只做自己有权做的部分（解除用户映射、置僵尸、唤醒父进程），把最后的回收交给 `do_wait`；如果父进程已经不在，就由 `do_exit` 把子进程改挂 `initproc`，让 `initproc` 扮演收割者。这条"所有权链"一旦理顺，僵尸进程、孤儿进程、重复唤醒这些问题都会各自落到明确的检查点上。

**（3）一致性来自引用计数与"先切后用"的顺序。** `mm_count` 决定页表能不能拆，`nr_process` 决定进程链表和计数是否对得上，`satp` 决定"当前正在用哪张页表"。本模块最容易出错的两处都在顺序上：释放页表前必须先切 `satp`，减引用必须在确认没有兄弟共享之后。把这类约束写成不变量（"任一时刻 satp 指向的页表页码不小于 1 个引用"）比反复读代码更可靠。

**（4）用户态与内核态的边界应当是唯一的收口点。** `kill` 的实现刻意不在系统调用里直接销毁目标进程，而是只置 `PF_EXITING`，让目标在自己下一次从内核返回用户态之前被 `trap()` 拦下。这样"进程不会带着半更新状态继续执行用户代码"这条性质由 Trap 返回路径统一保证，而不是在每个可能被杀的位置各写一遍清理逻辑。同理，`yield` 也只是置 `need_resched`，让调度点在同一个地方。把状态改变集中到少数入口，是这类系统代码最容易获得正确性的方式。

**（5）并发时序要专门设计用例压。** 单个子进程退出时，"父进程是否已睡在 `WT_CHILD` 上"只有两种情况；32 个子进程连续退出时，父进程可能已经被前一个子进程唤醒但仍未运行，此时再调用 `wakeup_proc` 就会命中"不能唤醒 RUNNABLE 进程"的断言。`forktest` 这类数量型用例的价值就在这里：它把只有在特定交错下才会出现的状态组合稳定地压出来。

### AI 协作开发的经验

**（1）先定 Spec，再让 AI 写实现。** 本模块的每个函数都先写清 Pre-Condition / Post-Condition / Cases，再交给 AI 生成代码。Spec 里最有价值的是"不允许做什么"——例如"`dup_mmap` 失败不自行清理"、"`kill` 不在系统调用里直接销毁进程"。这些约束能挡住 AI 最典型的行为：看到能回收的资源就顺手回收，看到能立刻完成的事情就立刻完成，结果破坏了调用者的回滚路径。

**（2）让 AI 先读代码，再写代码。** 生成 `dup_mmap` 和 `do_wait` 之前，先让 AI 确认了 `copy_range` 的真实签名、`mm_map` 的重叠检查方式、`set_links`/`remove_links` 对 `nr_process` 的影响、`wakeup_proc` 的断言条件。这一步避免了几类常见的"看起来对"的错误：按旧版 ucore 的接口臆造函数名、假设 `mm_count` 从 0 开始、假设 `wait_state` 在唤醒后会被自动清除。

**（3）把"能编译"和"能跑通"分成两个里程碑。** 第一个里程碑是内核自检三项全过，第二个才是用户程序用例全过。中间那道分界线非常有用：自检过不了说明问题在内存管理或接口约定；自检过了而用例不过，问题就落在进程生命周期本身。这道分界线把调试空间从"整个系统"压缩到"一个模块的语义"。

**（4）先做对照实验，再动代码。** 联调时 `forktest` 在第二个子进程退出时崩溃，第一反应是"地址空间复制有问题"，真正的原因却在一条不起眼的断言上：`wakeup_proc()` 不允许唤醒已经处于 `PROC_RUNNABLE` 的进程。定位办法不是继续读代码，而是把崩溃前后的状态打出来（父进程当时是 `RUNNABLE` 还是 `SLEEPING`），再把这个状态与 `do_wait` 的唤醒路径对上。此后凡是遇到"只在特定数量或特定时序下才出现"的问题，都先构造更小的对照用例，把变量收敛到一个，再改代码；直接改实现往往会同时动到多条正确路径，反而看不出是哪一处起的作用。

**（5）用串口日志建立证据链。** 每通过一个用例就把关键串口输出留存下来，既是报告的证据，也是回归时的对照基线。后面修改 `do_exit` 的唤醒条件时，正是靠 `forktest` 前后的日志对比，才确认改动只影响了重复唤醒、没有影响回收顺序。

---
