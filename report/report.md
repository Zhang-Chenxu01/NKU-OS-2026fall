# 操作系统实验报告

## 目录

- [实验基本信息](#实验基本信息)
- [一、实验目的](#一实验目的)
- [二、实验环境](#二实验环境)
- [三、实验整体逻辑分析](#三实验整体逻辑分析)
- [四、实验内容与实现](#四实验内容与实现)
- [4.4 Challenge 拓展练习](#44-challenge-拓展练习)
- [五、测试与验证](#五测试与验证)
- [六、实验总结与收获](#六实验总结与收获)

## 实验基本信息

| 项目 | 内容 |
| --- | --- |
| **实验名称** | Lab4：进程管理 |
| **小组成员** | 2411326-张晨旭、2411566-罗晟皓、2413042-郭桦 |
| **完成日期** | 2026-11-19 |

### 小组分工

| 成员 | 功能模块 | 负责的任务与修改范围 |
| --- | --- | --- |
| 2411326-张晨旭 | 虚拟内存页操作 | 实现 `pmm.c` 中的 `page_insert()`、`page_remove()`，验证映射、权限、引用计数与回收；检查既有 SLOB/VMM 初始化及运行情况，整理模块一实现与专项测试记录 |
| 2411566-罗晟皓 | 进程创建与初始化 | 实现 `proc.c` 中的 `alloc_proc()`、`proc_init()`、`kernel_thread()`、`do_fork()`、`copy_thread()`、`copy_mm()`，负责 PCB、内核栈、PID、线程初始现场及失败清理 |
| 2413042-郭桦 | 进程调度与上下文切换 | 实现 `cpu_idle()`、`schedule()`、`proc_run()`、`switch_to()`，负责运行实体选择、页表与上下文切换，并牵头最终集成、完整回归和运行证据汇总 |

---

## 一、实验目的

1. 理解虚拟内存的基本组织，掌握 Sv39 三级页表中非叶子项与叶子项的区别，在保留启动 1 GiB 大页映射的基础上维护 4 KiB 页映射。
2. 实现映射建立、权限更新、映射替换和撤销，保持页表内容、物理页引用计数、资源回收与 TLB 状态一致，并正确处理不存在映射和页表分配失败的情况。
3. 理解 `mm_struct`、`vma_struct` 与内核动态内存分配器的作用，验证地址空间描述结构的创建、插入、查找和销毁，为线程管理提供可用的内存基础设施。
4. 理解内核线程的创建过程，建立 PCB、内核栈、Trap frame 与 context 之间的联系，掌握 `idleproc`、`initproc` 和线程首次运行现场的初始化方式。
5. 理解线程状态与调度流程，结合临界区、中断状态保护、Sv39 页表切换和 RV64 寄存器保存恢复，实现从空闲线程到可运行线程的执行切换。
6. 按模块进行实现和专项验证，区分局部通过、已有框架限制与后续模块阻塞，再通过集成测试验证完整线程执行链，保留可复现的构建、QEMU 和 GDB 证据。

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

### 3.1 从内存管理到多线程执行

Lab4 在 Lab2 的物理页管理和 Lab3 的 Sv39 启动映射、Trap 现场保存恢复基础上，建立“内存准备—线程创建—调度执行”的完整流程。实验以统一的进程结构管理内核线程；这些线程共享内核地址空间，各自拥有 PCB、内核栈和执行上下文。

下图表示各模块完成后的目标执行链，完整执行链仍待集成验证：

```text
kern_init
    → pmm_init：物理页、页表操作检查、kmalloc_init
    → vmm_init：地址空间描述结构自检
    → proc_init：建立 idleproc，创建 initproc
    → clock_init、intr_enable
    → cpu_idle
    → schedule：选择可运行线程
    → proc_run：更新 current、切换页表
    → switch_to：保存旧 context，恢复新 context
    → 新线程首次运行：forkret → forkrets → __trapret
    → kernel_thread_entry → init_main
```

三个模块的衔接点分别是物理页及动态内存分配接口、已初始化的 `proc_struct` 和线程初始现场，以及 `current`、线程状态与 `context` 的一致性。前一阶段提供对象和资源，后一阶段使这些对象真正进入执行状态。

### 3.2 物理页、页表与动态内存的关系

[pmm_init()](../code/kern/mm/pmm.c) 先初始化物理页分配器和 `Page` 描述符，通过物理页自检后登记启动页表，再执行 `check_pgdir()`、`check_boot_pgdir()`，最后调用 `kmalloc_init()`。

模块一在既有 `get_pte()` 遍历框架上维护 4 KiB 叶子项。`page_insert()` 处理首次映射、同页权限更新和不同页替换；`page_remove()` 在不分配页表的前提下撤销映射。引用计数随映射增减，最后一个引用解除后回收物理页，并通过 `tlb_invalidate()` 使处理器停止使用旧翻译。

[kmalloc.c](../code/kern/mm/kmalloc.c) 的 SLOB 分配器通过 `alloc_pages()` 获取物理页，并通过 `page2kva()` 使用启动阶段建立的内核直接映射。小块分配由 SLOB 管理空闲块，大块分配使用连续物理页。因此，动态内存分配依赖物理页分配器和有效的内核映射；当前分配路径不会为每个对象调用 `page_insert()`。

[vmm.c](../code/kern/mm/vmm.c) 使用 `kmalloc()` 创建 `mm_struct` 和 `vma_struct`，通过有序链表维护 VMA，按 `[vm_start, vm_end)` 查找地址，并在销毁时释放描述结构。这里管理的是地址范围的描述信息，创建 VMA 本身不会建立物理页映射。`mm_destroy()` 当前只回收这些描述结构，后续地址空间资源的生命周期仍需调用方明确管理。

### 3.3 线程创建与首次运行现场

模块二负责将内核函数转换为可调度线程。`alloc_proc()` 建立初始 PCB，`do_fork()` 组织内核栈、PID、父子关系和进程登记，`copy_thread()` 在新内核栈中准备 Trap frame，并设置首次切换所需的 context。创建失败时，需要按已完成的分配步骤清理资源，避免留下不完整的线程对象。

`proc_init()` 将当前执行流登记为 `idleproc`，设置 `current`，再通过 `kernel_thread()` 创建 `initproc`。首次调度不能恢复一个从未执行过的普通调用现场，因此新线程的 context 需要指向 `forkret`，再经 `forkrets` 和 Lab3 的 `__trapret` 恢复预置 Trap frame，进入 `kernel_thread_entry`，最终调用目标线程函数。

`struct trapframe` 保存 Trap 返回所需的完整现场，`struct context` 则保存线程切换所需的 `ra`、`sp` 和 `s0～s11`。两者共同连接线程创建与首次执行，布局和初始化约定必须保持一致。

### 3.4 调度、页表与上下文切换

模块三通过 `cpu_idle()` 检查调度请求，由 `schedule()` 从进程链表中选择 `PROC_RUNNABLE` 线程，再调用 `proc_run()` 完成执行实体切换。没有其他可运行线程时，应返回空闲线程。

`proc_run()` 需要协调 `current`、目标页表和实际寄存器现场：按 Sv39 格式更新 `satp` 并刷新 TLB，再调用 `switch_to()` 保存旧 context、恢复新 context。切换过程需要保存并关闭中断，完成后恢复原中断状态，避免执行现场尚未切换完毕时暴露不一致的线程状态。

---

## 四、实验内容与实现

### 4.1 虚拟内存页操作

**负责人：** 2411326-张晨旭（成员 A）

#### 4.1.1 模块功能描述

本模块依据 Lab4 实验指导书，完成 [pmm.c](../code/kern/mm/pmm.c) 中的 `page_insert()` 和 `page_remove()`，在现有 Sv39 页表框架上建立、更新和撤销 4 KiB 页映射。通过维护页表项、物理页引用计数和 TLB，使软件记录的映射关系与处理器实际使用的地址翻译保持一致，为后续内存和线程管理提供基础。

生产代码修改限于 `LAB4 function1 2411326` 区域，包括两个目标函数及新增的静态辅助函数 `page_remove_pte()`。保留 Lab2 的物理页分配器、Lab3 的启动映射及已有大页保护；对 VMM/SLOB 仅进行接口检查和运行验证。

#### 4.1.2 关键接口与设计

| 接口或数据 | 定义位置 | 作用 |
| --- | --- | --- |
| `pde_t`、`pte_t`、`struct Page` | `memlayout.h` | 页表项类型均为 `uintptr_t`；`Page.ref` 保存物理页引用计数 |
| `get_pte(pgdir, la, create)` | `pmm.c` | 按三级索引定位 4 KiB PTE，必要时创建中间表；遇到大页返回 `NULL` |
| `page2ppn()`、`pte_create()`、`pte2page()` | `pmm.h` | 完成物理页号转换、有效 PTE 构造及物理页描述符解析 |
| `page_ref_inc/dec()`、`page_ref()` | `pmm.h` | 增减、读取引用计数；计数接口本身不释放物理页 |
| `alloc_page()`、`free_page()` | `pmm.h` | 调用已有物理页分配器分配或回收单页 |
| `PGSIZE`、`PTE_ADDR`、`PTE_V/R/W/X/U` | `mmu.h` | 定义 4096 字节页大小、PTE 地址提取和有效/权限位 |
| `tlb_invalidate()`、`flush_tlb()` | `pmm.c`、`pmm.h` | 分别执行带地址及无地址的 `sfence.vma` |
| `E_NO_MEM`、`E_INVAL` | `libs/error.h` | 表示定位/分配失败，以及拒绝覆盖有效非叶子项 |

**（1）页表层级与大页保护。** Sv39 通过 `PDX1/PDX0/PTX` 三个 9 位索引和 12 位页内偏移定位地址，每张页表有 512 项。4 KiB 叶子位于最底层；有效项设置 `R/W/X` 中任意一位时，应按叶子处理。[entry.S](../code/kern/init/entry.S) 的根页表包含物理起点为 `0x80000000` 的内核 1 GiB 叶子，已有 `get_pte()` 会拒绝向下遍历 1 GiB 和 2 MiB 叶子。本模块复用该保护，不拆分启动大页。

**（2）引用计数与回收。** 同一物理页可以映射到多个地址，每新增一个映射增加一次引用，每撤销一个映射减少一次引用，最后一个引用解除后才释放。更新同页权限时保持计数不变。中间页表页另有框架引用约定，删除接口不擅自回收中间表。

**（3）翻译一致性与职责划分。** 修改 PTE 后需要使对应 TLB 翻译失效，权限更新同样需要刷新。撤销映射时先清除 PTE、失效翻译，再减少引用并回收物理页。项目原先没有 `page_remove_pte()`，因此新增静态辅助函数集中实现这一顺序，供插入替换和删除共同使用。

多级页表对应地址翻译，引用计数对应共享资源生命周期，TLB 失效对应软件页表与硬件缓存的一致性。当前实现及测试基于单 hart；未新增并发页表锁或远程 TLB 同步，也未实现按需分页、写时复制、缺页恢复和页面置换。

#### 4.1.3 具体实现

**（1）`page_insert()`：建立与更新映射。** 首先调用 `get_pte(pgdir, la, 1)`，失败返回 `-E_NO_MEM`，此时尚未增加目标页引用。`la` 可带页内偏移，索引宏会定位其所在页，例如 `0x100` 与 `0x0` 对应同一 PTE。

取得 PTE 后处理权限。原有检查会传入 `perm=0` 和仅含写权限的参数：只有 `PTE_V` 会被解释为非叶子，`W=1,R=0` 则是非法组合。因此，当请求写权限或缺少读/执行位时补充 `PTE_R`。`perm=0` 形成只读叶子，仅写请求形成可读写叶子，仅执行请求保留为执行叶子。

目标未映射时增加新页引用；已映射同一页时直接更新权限；已映射不同页时先增加新页引用，再调用 `page_remove_pte()` 撤销旧映射。旧页仍有其他引用时继续保留，归零才回收。最终通过 `pte_create(page2ppn(page), perm)` 写入有效 PTE，并调用 `tlb_invalidate()`。

对于最底层有效但没有叶子位的条目，返回 `-E_INVAL` 并保留原项。对于大页范围，`get_pte()` 返回 `NULL`，插入也返回 `-E_NO_MEM`，在该分支中这一错误码并不代表真实内存耗尽。

**（2）`page_remove()`：撤销映射。** 使用 `get_pte(pgdir, la, 0)` 查询，不创建页表。查找失败直接返回；取得 PTE 后由辅助函数检查有效位和叶子位，再保存原物理页、清零 PTE、失效 TLB、减少引用，归零时调用 `free_page()`。重复删除会看到无效 PTE，不会重复减少引用或释放。

例如，同页映射至两个地址后引用为 2，删除第一个地址后为 1，第二个地址仍可访问；删除第二个地址后为 0，物理页才回收。先撤销翻译再回收，可避免处理器继续通过旧地址访问已重新分配的物理页。

以下为当前模块的完整实现，保留实验注释；已有的页表遍历和物理页分配器未重复列出。

```c
//LAB4 function1 2411326
static void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    if ((*ptep & PTE_V) && (*ptep & (PTE_R | PTE_W | PTE_X))) {
        struct Page *page = pte2page(*ptep);
        *ptep = 0;
        // Retire the translation before returning the frame to the allocator.
        tlb_invalidate(pgdir, la);
        if (page_ref_dec(page) == 0) {
            free_page(page);
        }
    }
}

int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }

    // A writable Sv39 leaf must be readable; perm=0 also needs a leaf bit.
    if ((perm & PTE_W) || !(perm & (PTE_R | PTE_X))) {
        perm |= PTE_R;
    }

    if (*ptep & PTE_V) {
        // A valid non-leaf is not a managed 4KiB mapping to replace.
        if (!(*ptep & (PTE_R | PTE_W | PTE_X))) {
            return -E_INVAL;
        }
        if (pte2page(*ptep) != page) {
            page_ref_inc(page);
            page_remove_pte(pgdir, la, ptep);
        }
    } else {
        page_ref_inc(page);
    }

    *ptep = pte_create(page2ppn(page), perm);
    tlb_invalidate(pgdir, la);
    return 0;
}

void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL) {
        page_remove_pte(pgdir, la, ptep);
    }
}
```

当前 `tlb_invalidate()` 无条件执行地址级 `sfence.vma`，并未检查参数页表是否为活动页表；本模块按其真实行为调用。异页替换会在旧项清除和新项写入后分别失效目标翻译，同页权限更新也会失效旧权限。

#### 4.1.4 Spec 与 Prompt

完整提示词已记录于 [prompt.md：实现虚拟内存页操作](prompt.md#实现虚拟内存页操作)，本节概括规格设计与代码的对应关系。

**Pre-Condition：** `pgdir` 指向有效根页表，`page_insert()` 的 `page` 指向已分配物理页；内存基础设施已初始化，调用方提供符合当前 Sv39 框架的地址和权限，并遵守物理页生命周期约定。

**Post-Condition：** 插入成功返回 `0`，形成合法 4 KiB 叶子映射，引用与 TLB 状态同步更新；失败不得错误增加目标页引用或建立有效叶子。删除存在的映射时正确减引用并按需回收，不存在时安全返回，重复删除具有幂等性。

| Case | 规格关注点与实现约定 |
| --- | --- |
| 首次映射 | 新页引用加一，PTE 包含正确页号和有效权限 |
| 同页重映射 | 更新权限，最终引用不变，不释放目标页 |
| 异页替换 | 新引用建立，旧引用减少，旧页归零才释放 |
| 页表分配失败 | 返回 `-E_NO_MEM`，目标页引用和叶子状态保持正确；底层部分分配回滚问题单独记录 |
| 权限更新 | 保证合法叶子，避免 `W=1,R=0`，并失效旧翻译 |
| 删除、重复删除及共享 | 只撤销当前映射，不分配页表，不重复释放，不提前回收共享页 |
| 已有大页 | 不误当中间表，不破坏内核启动映射 |

#### 4.1.5 实现及验证过程

**（1）接口检查与实现。** 阅读实验指导书、`pmm.c`、`pmm.h`、`mmu.h` 和现有检查，确认权限输入、大页保护及清理约定后完成实现。现有记录未出现两个目标函数实现后断言失败、再修改函数修复的过程；权限合法化和同页保护属于初次实现中的设计。

**（2）构建与测试方法。** 实际环境为 Linux/Bash、SiFive GCC-Metal 10.2.0、GDB 10.1 和 QEMU 4.1.1。QEMU 配置为 `virt`、128 MiB、单 hart、`-bios default`，通过 loader 将内核镜像加载至 `0x80200000`。

验证分为原始自检和独立源码副本中的补充断言检查。原始测试文件和生产代码不包含临时插桩；补充测试使用独立构建目录，构建产物及日志与工程源码分离保存。已执行的基础构建命令如下，在项目根目录运行：

```bash
make -C code -j2
```

补充验证使用实验时的临时辅助脚本，分别执行页表与 VMM/SLOB 的基线检查和补充检查，具体覆盖范围及结果见下文。这些脚本未纳入仓库，因此补充测试尚不具备仅依赖本仓库的完整复现条件。本节保留已取得的实验记录。

GDB 通过 `-gdb stdio` 管道连接 QEMU。原始页表验证在 `kmalloc_init()` 入口停止，两个页表自检已经完成；补充验证在 `module_tests_done()` 和 `infra_tests_done()` 停止后终止虚拟机。构建与链接均成功，没有编译警告或错误。

**（3）页表专项验证。** `check_pgdir()` 覆盖创建、查询、替换、共享引用和逐一删除；`check_boot_pgdir()` 检查启动大页，并通过 `0x100`、`0x100+PGSIZE` 的别名验证实际访问。二者均带有测试页表清理和空闲页数检查。后者直接释放数据页并撤销整个低地址子树，删除幂等性由补充用例验证。

原有 `test_lab4_pmm_basic()` 包含同页权限更新及重复删除，但未回收测试建立的中间页表。临时包装器在叶子清空后回收空页表，再核对资源恢复；正式删除接口仍保留中间页表。

| 测试与日志标记 | 预期检查 | 实际结果 |
| --- | --- | --- |
| `check_alloc_page()` | 既有物理页分配器自检 | 通过，输出 `succeeded!` |
| `check_pgdir()` | 创建、查询、替换、引用变化和清理后页数恢复 | 通过，输出 `succeeded!` |
| `check_boot_pgdir()` | 启动大页完整性与低地址别名访问 | 通过，输出 `succeeded!` |
| 原有 PMM 测试、A01 | 原测试断言通过，补充空页表清理后恢复资源 | 通过，输出 `[Test PMM] PASS` |
| A02 | 根项不存在时删除不创建页表 | 通过，根项及空闲页数不变 |
| A03 | 首次映射非对齐地址、PPN、引用及实际读取正确 | 通过，引用为 1，实际读取值正确 |
| A04 | 同页 RWU→X→RW，权限准确，引用与页数不变 | 通过；随后实际写入可从物理页别名读回。X 阶段检查 PTE，未执行测试指令 |
| A05 | 异页替换、旧引用归零并回收，TLB 使用新翻译 | 通过，空闲页增加 1，实际读取新页内容 |
| A06、A07 | 共享引用 2→1→0，重复及空 PTE 删除不重复释放 | 通过，最后一次删除后才释放物理页 |
| A08.1 | 第一次分配失败，错误码、目标引用与资源均正确 | 通过，返回 `-E_NO_MEM`，无残留中间页表 |
| A08.2 | 第二次分配失败，错误码与叶子状态正确，分配完整回滚 | 错误码、引用与无叶子映射检查通过；完整回滚未满足，残留 1 页，见本节问题分析 |
| A09 | 恢复真实分配器后重试成功并清理 | 通过 |
| A10、A11 | 对 1 GiB、合成 2 MiB 叶子的操作不破坏大页 | 通过，条目与计数保持不变 |
| A12 | 最底层有效非叶子项拒绝覆盖或回收 | 通过，插入返回 `-E_INVAL`，原项不变 |
| A13 | 补充测试最终资源恢复 | 通过，所有根项恢复；空闲页 `31731→31731` |

分配失败通过临时包装分配器回调，使第 1 或第 2 次请求精确失败，不耗尽内存。补充测试末尾输出如下：

```text
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
[Test PMM] PASS
[A08.2] allocation failure: error/ref/mapping correct; retained tables=1; cleaned PASS
[A13] all root entries restored; free pages 31731 -> 31731 PASS
MODULE_A_ALL_PASS
```

`MODULE_A_ALL_PASS` 表示补充断言均满足，其中明确记录了 A08.2 的残留；不能据此认定生产失败路径已经完整回滚。最终页数恢复包含临时测试对残留页的显式清理。

**（4）VMM/SLOB 验证。** 实际初始化顺序为 `kern_init → pmm_init → kmalloc_init → 返回 kern_init → idt_init → vmm_init → proc_init`。`kmalloc_init()` 调用 `slob_init()` 并输出信息，本身没有独立分配自检；分配行为由原有 VMA 自检和补充用例验证。

| 验证项 | 实际结果与证据 |
| --- | --- |
| 原始工程与临时副本构建 | 成功，无编译警告或错误 |
| `kmalloc_init()` | 输出 `use SLOB allocator`、`kmalloc_init() succeeded!` |
| `vmm_init()` | 输出 `check_vma_struct() succeeded!`、`check_vmm() succeeded.` |
| I01、I02：11 种小块尺寸、12 轮 | 1、15、16、17、63、64、127、511、1000、2048、4063 字节均分配成功；内容保持、互不重叠、交错释放及复用检查通过；空闲页 `31730→31730` |
| I03：大块边界、8 轮 | 4064、4095、4096、4097、8192、12289 字节通过；地址页对齐，按 2 的幂页数分配，释放后物理页数精确恢复 |
| I04：边界调用 | `kmalloc(0)`、超过最大支持大小的请求返回 `NULL`，`kfree(NULL)` 不改变页数 |
| I05：原有 100-VMA 自检追加 20 轮 | 排序插入、查找、区间边界和销毁检查均通过，每轮空闲页数稳定 |
| I06：描述结构补充检查 | `mm_struct` 初值、`map_count`、VMA 归属、查找缓存、半开区间及销毁通过 |
| I07：映射保护 | 全部根页表项与测试前一致，未留下额外测试映射 |

基础设施日志输出 `INFRA_ALL_PASS`。SLOB 小块释放后合并空闲块供后续复用，大块释放归还连续物理页，因而分别检查多轮页数稳定和精确物理页回收。

---

### 4.2 进程创建与初始化

**负责人：** 2411566-罗晟皓

#### 4.2.1 模块功能描述

本模块实现 [proc.c](../code/kern/process/proc.c) 中的 `alloc_proc()`、`proc_init()`、`kernel_thread()`、`do_fork()`、`copy_thread()` 和 `copy_mm()`，把内核中的目标函数及参数组织为具有独立 PCB、内核栈和初始执行现场的内核线程。初始化后，当前启动执行流对应 `idleproc`，新建线程 `initproc` 处于 `PROC_RUNNABLE` 状态，供后续调度使用。

代码修改限于 `//LAB4 function2 2411566` 区域，保持现有函数签名以及 PCB、context、Trap frame 的布局。当前 Lab4 内核线程共用启动页表，不创建或复制用户地址空间。

#### 4.2.2 关键接口与设计

| 数据结构或接口 | 定义位置 | 本模块中的作用 |
| --- | --- | --- |
| `struct proc_struct` | `proc.h` | 记录状态、PID、运行次数、内核栈、调度标志、父进程、内存管理指针、context、Trap frame、页表物理地址、名称和链表节点 |
| `struct context` | `proc.h` | 保存 `ra`、`sp` 和 `s0～s11`；新线程首次切换的 `ra` 指向 `forkret` |
| `struct trapframe` | `trap.h` | 保存通用寄存器、`status`、`epc` 等信息，经现有 Trap 返回路径恢复 |
| `proc_list`、`hash_list`、`nr_process` | `proc.c` | 维护线程集合、PID 查询和进程数量；PID 0 计入数量，但不加入普通进程链表和 PID 哈希表 |
| `kmalloc()`、`kfree()` | `kmalloc.c` | 分配和回收 PCB |
| `setup_kstack()`、`put_kstack()` | `proc.c` | 通过物理页接口分配和释放 `KSTACKPAGE` 页内核栈 |
| `get_pid()`、`hash_proc()`、`find_proc()` | `proc.c` | 分配唯一 PID、登记哈希节点以及按 PID 查找线程 |
| `boot_pgdir_pa`、`bootstack` | `pmm.h` 声明 | 分别用于设置内核页表根的物理地址和复用启动栈 |
| `wakeup_proc()` | `sched.c` | 将初始化完毕的线程从 `PROC_UNINIT` 转为 `PROC_RUNNABLE` |
| `local_intr_save/restore()` | `sync.h` | 保护 PID 分配、链表登记、计数更新和线程唤醒，并恢复进入临界区前的中断状态 |

这里需要区分两类现场：context 用于线程之间的上下文切换，Trap frame 用于恢复线程开始执行时的寄存器及状态。新线程没有已有调用现场可恢复，因此需要人为设置两者，使 context 先进入 `forkret`，再通过 Trap frame 到达 `kernel_thread_entry`。

#### 4.2.3 具体实现

**（1）`alloc_proc()`：建立 PCB 初值。** 使用 `kmalloc()` 分配 PCB，失败返回 `NULL`。成功后将整个结构清零，因此运行次数、内核栈、调度标志、父进程、`mm`、Trap frame、flags、名称及 context 均取得明确初值；再设置 `state = PROC_UNINIT`、`pid = -1`、`pgdir = boot_pgdir_pa`，并分别调用 `list_init()` 初始化进程链表和哈希链表节点。链表节点需要自指，不能仅依靠清零完成初始化。

**（2）`copy_mm()`：处理本阶段的地址空间约定。** 当 `current->mm == NULL` 时，设置 `proc->mm = NULL` 并返回 `0`。线程的 `pgdir` 已在 PCB 初始化时设置为启动页表物理地址。当父进程具有非空 `mm` 时返回 `-E_INVAL`，交给调用方清理已分配资源；本模块不接管或释放父进程的地址空间，也不实现用户地址空间复制。

**（3）`copy_thread()`：布置首次执行现场。** 在新内核栈顶部预留 `sizeof(struct trapframe)` 字节，将输入 Trap frame 复制到该位置，令 `proc->tf` 指向它，并将子线程返回值寄存器 `a0` 置为 `0`。如果传入的 `esp` 为 `0`，将恢复后的 `sp` 设置为该 Trap frame 的地址，使函数调用向低地址使用新线程自己的内核栈；否则使用传入的栈地址。最后设置 `context.ra = forkret`、`context.sp = proc->tf`，其他 context 寄存器保留 PCB 分配时的零值。

按现有汇编接口，初始现场应衔接如下路径：

```text
switch_to
    → forkret
    → forkrets(current->tf)
    → __trapret：恢复 Trap frame，执行 sret
    → kernel_thread_entry：a0 ← s1，调用 s0 指向的函数
    → 目标线程函数
```

**（4）`do_fork()`：组织创建过程与失败回收。** 首先检查 `nr_process`，达到 `MAX_PROCESS` 时返回 `-E_NO_FREE_PROC`。随后依次分配 PCB、设置 `parent = current`、分配内核栈、调用 `copy_mm()` 和 `copy_thread()`。只有对象及现场准备完毕后，才进入中断保护区，分配 PID、加入 PID 哈希表和 `proc_list`、增加 `nr_process`，并通过 `wakeup_proc()` 将线程设为可运行，最后恢复原中断状态并返回 PID。

| 失败位置 | 返回及资源处理 |
| --- | --- |
| 进程数量达到上限 | 返回 `-E_NO_FREE_PROC`，不分配资源 |
| PCB 分配失败 | 返回 `-E_NO_MEM`，不访问空指针 |
| 内核栈分配失败 | 跳转 `bad_proc`，仅释放已经分配的 PCB |
| `copy_mm()` 失败 | 先经 `bad_kstack` 释放新内核栈，再释放 PCB，返回原错误码 |

失败分支均发生在 PID 和链表登记之前，因此不需要撤销进程记录，也不会错误增加进程数量。当前 `copy_mm()` 不分配任何 `mm` 资源，失败清理不涉及共享启动页表或父进程地址空间。

**（5）`kernel_thread()`：将目标函数转换为线程入口现场。** 清零栈上的临时 Trap frame，按 `kernel_thread_entry` 的现有约定将函数指针存入 `s0`、参数存入 `s1`，并将 `epc` 指向该汇编入口。状态值以当前 `sstatus` 为基础，设置 `SSTATUS_SPP` 和 `SSTATUS_SPIE`、清除 `SSTATUS_SIE`，使 Trap 恢复期间保持关闭中断，并在 `sret` 后以 S 模式和预置的中断状态开始执行。随后调用 `do_fork(clone_flags | CLONE_VM, 0, &tf)`，由创建路径复制临时现场并分配实际线程栈。

**（6）`proc_init()`：建立两个初始进程。** 初始化进程链表和全部 PID 哈希桶后，分配 `idleproc`，设置 PID 为 `0`、状态为 `PROC_RUNNABLE`、内核栈为既有 `bootstack`、`need_resched = 1`、名称为 `idle`，并设置 `current = idleproc`、`nr_process = 1`。此处不为启动执行流重复申请内核栈。

然后通过 `kernel_thread(init_main, "Hello world!!", 0)` 创建第二个线程；检查创建返回值，按 PID 查得 `initproc`，确认其 PID 为 `1`，设置名称为 `init`，并断言总进程数为 `2`。PCB 或初始线程创建失败时输出明确的初始化错误信息。

#### 4.2.4 Spec 与 Prompt

完整提示词记录于 [prompt.md：Lab4 进程创建与初始化功能实现](prompt.md#lab4-process-create-2411566)。提示词采用课程要求的四段式结构，先界定六个函数和代码修改边界，再列出实际依赖、必须保持的接口以及可检查的前置条件、后置条件和分支情形。

| 提示词部分 | 设计内容及实现对应 |
| --- | --- |
| `[PROMPT]` | 明确创建 `idleproc`、`initproc` 的目标，限定修改 `proc.c` 的 function2 区域，保留他人实现和既有结构布局 |
| `[RELY]` | 列出 PCB、context、Trap frame、全局变量、内存分配、PID、链表、中断及状态位接口，要求先核对真实源码 |
| `[GUARANTEE]` | 固定六个目标函数的签名，要求实现真实功能并移除对应的占位 panic |
| `[SPECIFICATION]` | 按函数给出 Pre-Condition、Post-Condition 和 Cases，覆盖初始化、入口寄存器、栈地址、状态位、线程登记及失败回收 |

**Pre-Condition：** 物理内存、启动页表和动态内存分配器已初始化；创建线程时 `current` 有效，输入目标函数、参数和 Trap frame 满足对应接口约定。

**Post-Condition：** PCB 字段初值明确，新内核线程拥有有效的内核栈和初始现场；成功创建后具有唯一 PID、正确父进程和可运行状态；失败时清理本次已取得的资源，不留下无效登记。进程管理初始化结束后，`current == idleproc`，`initproc->parent == idleproc`，两个初始进程的 PID 分别为 `0` 和 `1`。

规格中的关键 Cases 是：PCB 分配失败、进程数量达到上限、内核栈分配失败、内存管理处理失败、`esp == 0` 的内核线程现场，以及 PID 和链表登记的中断保护。这些约束分别对应返回错误、按逆序清理资源、设置有效栈指针和只公开完整线程的实现逻辑。

#### 4.2.5 实现及验证过程

**（1）源码核对与实现。** 实现前检查了 `proc.c`、`proc.h`、`trap.h`、`entry.S`、`trapentry.S`、`sync.h`、`pmm.h`、`kmalloc.c` 以及原有进程创建测试，确认寄存器字段、内核栈分配接口、PID 规则及 Trap 返回约定。六个目标函数原先均通过 `Lab4 TODO` panic 占位，本次在指定区域完成实现，并将该区域的 `YOUR CODE` 替换为学号 `2411566`。

**（2）构建验证。** 在 WSL Ubuntu 22.04 中使用 `riscv64-unknown-elf-gcc` 10.2.0，进入工程的 `code` 目录运行：

```bash
make -j2
```

编译、链接及 `bin/ucore.img` 生成完成，构建日志中没有编译警告或错误。本节测试结果依据实现阶段保存的构建和运行记录。

**（3）进程创建专项运行。** 使用[仓库原有的测试注入工具](../code/tools/inject.py)，在独立测试源码副本中插入[原有进程创建用例](../code/tools/test_lab4.c) `test_lab4_process_create()` 的调用，并在副本中统一文本换行格式；原仓库的测试文件及评分脚本保持原样。构建副本内核后，在该副本的 `code` 目录使用 QEMU 4.1.1 的 `virt` 机器直接运行：

```bash
timeout 6s qemu-system-riscv64 \
    -machine virt -nographic -bios default \
    -device loader,file=bin/ucore.img,addr=0x80200000 \
    -monitor none
```

`test_lab4_process_create()` 在 `proc_init()` 结束位置执行。实际串口输出包含：

```text
[Test Process Create] PASS
```

| 检查项 | 检查方式 | 实际结果 |
| --- | --- | --- |
| 构建和链接 | `make -j2` | 通过，生成内核及镜像，无编译警告或错误 |
| 当前进程和空闲进程 | 原有断言检查 `current == idleproc`、PID 0、可运行状态、复用 `bootstack` | 通过 |
| 空闲进程初始字段 | 原有断言检查 `parent`、`mm`、`tf` 均为 `NULL`，`runs`、`flags`、`context.ra` 均为 `0` | 通过 |
| 初始内核线程 | 原有断言检查 `initproc != NULL`、PID 1、可运行状态、父进程为 `idleproc`、`mm == NULL` | 通过 |
| 初始上下文入口 | 原有断言检查 `initproc->context.ra`、`context.sp` 非零 | 通过 |
| 初始化计数 | `proc_init()` 中的 `assert(nr_process == 2)` | 通过 |
| 资源分配失败清理 | 按代码路径检查错误码、栈和 PCB 的释放顺序及登记时机 | 路径已核对；本次未进行资源不足故障注入 |
| 修改范围与占位检查 | `git diff --check`、差异范围核对及 function2 区域检查 | 通过，无本模块遗留 TODO panic，区域外代码保持原样 |

进程创建用例验证的是初始化后的对象状态，以及 context 入口和栈指针已设置；该 PASS 不能单独证明整个首次切换和目标函数执行链已经运行通过。

**（4）实现中处理的关键问题。** 首先，不能直接把创建接口传入的零栈地址写回新线程，否则 Trap 返回会恢复无效 `sp`，因此 `copy_thread()` 将其转换为新内核栈中 Trap frame 的地址。其次，context 与 Trap frame 的入口作用不同，需要同时设置 `context.ra/sp` 和 `tf.epc/s0/s1/status`，并与已有汇编保持一致。最后，PCB 和栈的资源归属不同，失败回收必须依据已经完成的分配步骤执行；启动栈和共享启动页表不属于新线程的独占分配资源，不能纳入该回收路径。

### 4.3 进程调度与上下文切换

**负责人：** 2413042-郭桦

#### 4.3.1 模块功能描述

本模块实现 [sched.c](../code/kern/schedule/sched.c) 中的 `schedule()`、[proc.c](../code/kern/process/proc.c) 中的 `proc_run()` 与 `cpu_idle()`，以及 [switch.S](../code/kern/process/switch.S) 中的 `switch_to()`，把模块二创建好的内核线程真正交给 CPU 执行。

Lab4 的调度点唯一，位于空闲线程 `cpu_idle()` 的循环中：当 `current->need_resched` 被置位时，`schedule()` 从 `proc_list` 中选出一个 `PROC_RUNNABLE` 状态的线程，`proc_run()` 更新 `current`、切换到目标页表，并调用 `switch_to()` 保存旧线程的执行现场、恢复新线程的执行现场。

调度策略为最简单的 FIFO/轮转：以当前线程为起点向后扫描 `proc_list`，跳过链表头哨兵；`idleproc` 不加入 `proc_list`，在没有其他就绪线程时作为兜底。本实验不实现时间片、优先级和抢占式调度，这些内容留给后续实验。

本模块不修改 PCB、context、Trap frame 的布局，也不改变 `forkret → forkrets → __trapret` 的既有恢复链路；代码修改限于 `//LAB4 function3 2413042` 区域。

#### 4.3.2 关键接口与设计

| 数据结构或接口 | 定义位置 | 本模块中的作用 |
| --- | --- | --- |
| `struct proc_struct` | `proc.h` | 通过 `state`、`need_resched`、`runs`、`pgdir`、`context` 判断和切换线程 |
| `struct context` | `proc.h` | `switch_to()` 保存/恢复的 `ra`、`sp`、`s0～s11`；新线程的 `ra` 指向 `forkret` |
| `proc_list`、`current`、`idleproc` | `proc.c` | 就绪队列、当前线程和空闲线程；`idleproc` 不进入 `proc_list`，仅作兜底 |
| `cpu_idle()` | `proc.c` | 唯一调度点：循环检查 `need_resched` 并调用 `schedule()` |
| `schedule()` | `sched.c` | 在临界区内选择下一个可运行线程，必要时调用 `proc_run()` |
| `proc_run()` | `proc.c` | 更新 `current`、按 Sv39 写 `satp`、刷新 TLB、调用 `switch_to()` |
| `switch_to()` | `switch.S` | 保存旧 context、恢复新 context，并返回到新线程的执行现场 |
| `local_intr_save/restore()` | `sync.h` | 使“选择 + 切换”整体原子，退出时恢复进入前的中断状态 |
| `read_csr/write_csr(satp)`、`RISCV_PGSHIFT` | `riscv.h` | 构造 Sv39 的 satp 值：MODE=8，PPN=`pgdir >> 12` |
| `flush_tlb()` | `pmm.c`、`pmm.h` | 切换页表后失效旧地址翻译 |
| `forkret()`、`forkrets()`、`__trapret`、`kernel_thread_entry` | `proc.c`、`trapentry.S`、`process/entry.S` | 新线程首次运行的恢复链路 |
| `boot_pgdir_pa` | `pmm.h` | 内核线程共享的启动页表根物理地址，写入 `satp` 的 PPN |

设计上有三个必须同时保持一致的状态：`current` 指针、`satp` 指向的页表，以及 CPU 中实际保存的寄存器现场。若先改 `current` 再切换现场，或先写 `satp` 再切换栈，任何一次中断都可能让三者不一致，因此 `proc_run()` 在关中断的临界区内依次完成这三步。

`satp` 不能直接复用 `lcr3()`：该函数按 Sv32 语义只写入 PPN，不带 MODE 字段；本实验需要 Sv39 的 `MODE=8`，因此按 `kern_entry` 的写法显式构造 `(8UL << 60) | (pgdir >> 12)`，其中 `pgdir` 是页表根的物理地址。

`switch_to()` 只处理被调用者保存寄存器，原因是它本身是一个函数调用：调用者保存寄存器由编译器在调用点负责，而 `ra` 决定返回地址，`sp` 决定新线程使用哪个内核栈，`s0～s11` 是跨调用需要保持的寄存器。改变 `context` 的字段顺序或数量会破坏这条约定，因此实现中严格沿用 `ra, sp, s0～s11` 的既有布局。

#### 4.3.3 具体实现

**（1）`cpu_idle()`：唯一的调度点。** 空闲线程在 `kern_init()` 末尾被调用，此后不再返回。循环中只做一件事：发现 `need_resched` 非零就调用 `schedule()` 让出 CPU。这里不主动置位 `need_resched`，也不引入时钟抢占；首个调度请求由 `proc_init()` 在初始化 `idleproc` 时置位。

```c
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)
        {
            schedule();
        }
    }
}
```

**（2）`schedule()`：选择下一个可运行线程。** 进入函数后先关中断，避免在选择过程中被时钟中断打断。随后清除当前线程的 `need_resched`，并以当前线程为起点计算扫描位置：`idleproc` 不在 `proc_list` 中，因此从链表头开始；普通线程从自己的 `list_link` 之后开始，实现轮转。扫描时跳过链表头哨兵，遇到第一个 `PROC_RUNNABLE` 线程即停止；整圈都没有找到时回退到 `idleproc`。

```c
current->need_resched = 0;
last = (current == idleproc) ? &proc_list : &(current->list_link);
le = last;
do
{
    if ((le = list_next(le)) != &proc_list)
    {
        next = le2proc(le, list_link);
        if (next->state == PROC_RUNNABLE)
        {
            break;
        }
    }
} while (le != last);

if (next == NULL || next->state != PROC_RUNNABLE)
{
    next = idleproc;
}
```

选中后增加目标线程的 `runs` 计数；只有目标与当前线程不同才调用 `proc_run()`。若当前线程本来就是唯一可运行线程（例如全部线程都在等待），则不发生切换，但仍已清除调度请求。最后恢复中断状态，保证从 `cpu_idle` 或线程函数中调用时不会意外改变中断使能。

**（3）`proc_run()`：切换当前线程与页表。** 首先判断目标是否就是当前线程，相同则直接返回，避免无意义切换。否则保存旧线程指针，关中断进入临界区，更新 `current`，按 Sv39 格式写入 `satp` 并执行 `flush_tlb()`，然后调用 `switch_to(&prev->context, &proc->context)` 完成寄存器现场切换。`local_intr_restore()` 在切换后执行，恢复的是进入临界区前的中断状态。

```c
current = proc;
uintptr_t satp = (8UL << 60) |
                 ((proc->pgdir >> RISCV_PGSHIFT) & ((1UL << 44) - 1));
write_csr(satp, satp);
flush_tlb();
switch_to(&(prev->context), &(proc->context));
```

`satp` 的 MODE 字段为 8（Sv39），PPN 取页表根物理地址右移 12 位后保留低 44 位。本实验所有内核线程共享启动页表，因此切换前后 `satp` 的 PPN 通常相同，但代码仍按“可能不同”处理，为后续用户进程留出接口。

**（4）`switch_to()`：保存与恢复上下文。** 函数参数 `a0`、`a1` 分别指向旧、新 `struct context`。实现按字段顺序保存 `ra`、`sp`、`s0～s11`，再按同样顺序从新 context 恢复；每个寄存器占 8 字节，偏移依次为 0、8、…、104。恢复 `ra` 后执行 `ret`，若目标是新建线程，其 `context.ra` 已被 `copy_thread()` 设为 `forkret`，于是返回到 `forkret` 继续首次运行。

```asm
switch_to:
    STORE ra,  0*REGBYTES(a0)
    STORE sp,  1*REGBYTES(a0)
    ...
    STORE s11, 13*REGBYTES(a0)

    LOAD ra,  0*REGBYTES(a1)
    LOAD sp,  1*REGBYTES(a1)
    ...
    LOAD s11, 13*REGBYTES(a1)
    ret
```

**（5）首次运行链路。** 新建线程的 `context.ra` 指向 `forkret`，`context.sp` 指向它自己的 Trap frame。`switch_to()` 恢复这两个寄存器后返回 `forkret`；`forkret()` 调用 `forkrets(current->tf)`，后者把 `sp` 设为 Trap frame 地址并跳转 `__trapret`；`__trapret` 从 Trap frame 恢复通用寄存器并执行 `sret`，`epc` 指向 `kernel_thread_entry`；该入口把 `a0` 设为 `s1`（线程参数）、调用 `s0`（线程函数）。整条链路为：

```text
cpu_idle → schedule → proc_run → switch_to
    → forkret → forkrets → __trapret
    → kernel_thread_entry → 线程函数（init_main / 测试线程）
```

普通线程之间的切换不需要经过完整 Trap 恢复：`switch_to()` 直接恢复对方的 `ra/sp/s0～s11` 后返回到对方上次调用 `switch_to()` 之后的指令，即 `proc_run()` 里 `switch_to()` 的下一条语句。

#### 4.3.4 Spec 与 Prompt

完整提示词记录于 [prompt.md：Lab4 进程调度与上下文切换实现](prompt.md#lab4进程调度与上下文切换实现)。提示词沿用四段式结构，先限定四个函数和修改边界，再列出真实依赖、必须保持的接口以及可检查的前置/后置条件。

| 提示词部分 | 设计内容及实现对应 |
| --- | --- |
| `[PROMPT]` | 明确 `cpu_idle/schedule/proc_run/switch_to` 的目标，限定 `//LAB4 function3` 区域，保留 `proc_list`、context、Trap frame 与 `forkret` 链路 |
| `[RELY]` | 列出 `proc_struct`、`context`、`proc_list`、`current`、`idleproc`、`need_resched`、`runs`、`local_intr_save/restore`、`satp`/`flush_tlb`、`boot_pgdir_pa` 等实际依赖 |
| `[GUARANTEE]` | 固定四个函数签名，要求移除占位 panic，不修改其他模块和评分脚本 |
| `[SPECIFICATION]` | 按函数给出 Pre-Condition、Post-Condition 与 Cases，覆盖无就绪线程、目标即当前线程、首次切换和中断保护 |

**Pre-Condition：** 物理内存、启动页表、进程管理已初始化；`current` 指向有效 PCB，`proc_list` 已建立；目标线程的 `pgdir` 是页表根物理地址，`context` 已按 `ra/sp/s0～s11` 布局初始化；调用 `schedule()` 时可在任意线程上下文。

**Post-Condition：** `schedule()` 返回时，`current` 指向一个 `PROC_RUNNABLE` 线程（通常是下一线程或 `idleproc`），其 `runs` 已增加，中断状态与进入前一致；若发生切换，`switch_to()` 之后执行流位于新线程的现场，`satp` 指向新线程页表。

| Case | 规格关注点与实现约定 |
| --- | --- |
| 首次调度（idleproc → initproc） | 从链表头扫描，选中 initproc，`need_resched` 清零 |
| 无其他就绪线程 | 回退 `idleproc`，`next == current` 时不调用 `proc_run` |
| 普通线程轮转 | 从当前线程之后扫描，跳过链表头哨兵，整圈后回到起点 |
| 目标即当前线程 | `proc_run` 直接返回，避免无意义切换 |
| 切换页表 | `satp` 的 MODE=8、PPN=`pgdir>>12`，随后 `flush_tlb` |
| 中断保护 | `schedule`/`proc_run` 全程关中断，退出时恢复原状态 |
| 新线程首次运行 | `context.ra=forkret`，经 `forkrets → __trapret → kernel_thread_entry` 进入目标函数 |
| 汇编 ABI | 只保存/恢复 callee-saved 寄存器，不改 `context` 布局 |

#### 4.3.5 实现及验证过程

**（1）源码核对与实现。** 实现前检查了 `sched.c`、`proc.c`、`switch.S`、`proc.h`、`trap.h`、`trapentry.S`、`process/entry.S`、`sync.h`、`pmm.h` 以及原有测试 `test_lab4.c`，确认 `context` 的字段布局、`local_intr_save/restore()` 的语义、`satp` 的 Sv39 编码方式以及 `forkret → forkrets → __trapret` 的既有链路。四个目标函数原先均为 `Lab4 TODO` 占位，本次在 `//LAB4 function3` 区域完成实现，并将 `YOUR CODE` 替换为学号 `2413042`；模块一、模块二的代码与测试脚本未改动。

实现完成后，用 `objdump` 核对 `switch_to` 的机器码，确认 14 个寄存器的保存/恢复偏移依次为 0、8、…、104，与 `struct context` 的字段顺序一致。

**（2）构建验证。** 在 WSL 的 Ubuntu 环境中使用 `riscv64-unknown-elf-gcc` 构建：

```bash
cd code
make -j4
```

编译、链接与 `bin/ucore.img` 生成均成功，构建日志中没有编译警告或错误。

**（3）集成评分。** 使用仓库自带的测试注入工具与 `make grade` 完成整体验证，实际输出为：

```text
  -check PMM:                                OK
  -check Process Create:                     OK
  -check Schedule:                           OK
Total Score: 30/30
```

串口日志中，三项测试与 `init_main` 的输出依次出现：

```text
[Test PMM] PASS
check_vma_struct() succeeded!
check_vmm() succeeded.
[Test Process Create] PASS
++ setup timer interrupts
[Test Schedule] PASS
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

| 检查项 | 检查方式 | 实际结果 |
| --- | --- | --- |
| 构建和链接 | `make -j4` | 通过，生成内核及镜像，无编译警告或错误 |
| 虚拟内存页操作 | `[Test PMM]` | PASS |
| 进程创建 | `[Test Process Create]` | PASS |
| 调度与切换 | `[Test Schedule]`，两个测试线程交替执行 | PASS |
| 首次运行链路 | `initproc` 打印 PID、名称与两行 `To U` | 通过，输出与预期一致 |
| 进程退出路径 | `init_main` 返回后进入 `do_exit()` 的 panic | 预期行为：本实验未实现进程退出，评分脚本以 `readline` 断点收尾 |
| 修改范围 | `git diff` 核对 `//LAB4 function3` 区域 | 通过，模块一/二与测试脚本未改动 |

`[Test Schedule]` 验证的是：`test_lab4_schedule()` 创建两个测试线程，二者各自循环三次并调用 `schedule()`，最终断言两个线程都真正运行过。这说明调度器能够从 `initproc` 切到测试线程、在多个线程之间往返，并保持各自的执行现场。

**（4）GDB 首次运行链路验证。** 以内核符号文件连接 QEMU 的 gdbstub，在关键函数处设置断点，实际命中顺序与现场如下：

```text
[HIT] cpu_idle     current pid=0 state=2 need_resched=1
[HIT] schedule     current pid=0 need_resched=1
[HIT] proc_run     from pid=0 to pid=1 pgdir=0x80208000
[HIT] switch_to    from=0xffffffffc040e040 to=0xffffffffc040e140
[HIT] forkret      current pid=1 tf=0xffffffffc0411ee0
[HIT] forkrets     tf=0xffffffffc0411ee0
[HIT] __trapret
[HIT] kernel_thread_entry
[HIT] init_main    pid=1 name=init
```

该记录验证了：调度前 `current` 是 PID 为 0 的 `idleproc`（`state=2` 即 `PROC_RUNNABLE`），`need_resched=1`；`proc_run` 把 `current` 切到 PID 1 的 `initproc`，其页表根物理地址为 `0x80208000`；`switch_to` 在两个不同的 context 地址之间切换；新线程经 `forkret`、`forkrets`、`__trapret` 和 `kernel_thread_entry` 到达 `init_main`，此时 `current->pid == 1`、名称为 `init`。链路与设计一致。

**（5）实现中处理的关键问题。**

- **`satp` 的构造。** `lcr3()` 按 Sv32 语义只把地址右移 12 位写入 `satp`，不带 MODE 字段；在 Sv39 下这样写入会得到 MODE=0 的无效值。因此 `proc_run()` 按 `kern_entry` 的写法显式构造 `(8UL << 60) | (pgdir >> 12)`，并对 PPN 取低 44 位。
- **`idleproc` 的位置。** 模块二有意不把 `idleproc` 加入 `proc_list`，因此 `schedule()` 对 `idleproc` 从链表头开始扫描，对其他线程从自身之后开始；整圈找不到可运行线程时回退到 `idleproc`。若按普通线程处理 `idleproc`，会因为它的 `list_link` 未挂入链表而破坏轮转。
- **不切换的情况。** 当 `next == current`（例如所有其他线程都在睡眠）时不调用 `proc_run()`，避免无意义地保存和恢复同一现场。
- **原子性。** `schedule()` 与 `proc_run()` 都在关中断的临界区内完成，退出时通过 `local_intr_restore()` 恢复进入前的中断状态；否则在选择线程或写 `satp` 的间隙被时钟中断打断，会看到 `current`、页表与寄存器现场三者不一致。
- **首次运行依赖模块二。** `switch_to()` 只负责恢复寄存器，新线程之所以能进入 `init_main`，是因为 `copy_thread()` 已把 `context.ra` 设为 `forkret`、`context.sp` 设为该线程的 Trap frame；两个模块的约定必须同时成立。
- **`do_exit()` 的 panic 是预期。** `init_main` 返回后由 `kernel_thread_entry` 调用 `do_exit()`，后者在本实验中仍是 panic 占位，因此会进入内核调试监视器；这是评分脚本采集日志所依赖的收尾路径，不是调度失败。



---

### 4.4 Challenge 拓展练习

#### 4.4.1 中断保存与恢复机制

##### 4.4.1.1 相关代码与调用关系

[sync.h](../code/kern/sync/sync.h) 的实现如下：

```c
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);
```

[intr.h](../code/kern/driver/intr.h) 声明底层接口，[intr.c](../code/kern/driver/intr.c) 实际修改 CSR：

```c
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
```

调用关系为 `local_intr_save → __intr_save → read_csr / intr_disable → clear_csr`，恢复路径为 `local_intr_restore → __intr_restore → intr_enable → set_csr`。[riscv.h](../code/libs/riscv.h) 中，`read_csr` 通过 `csrr` 读取；`set_csr`、`clear_csr` 通过 CSR 置位、清位操作修改掩码指定的位，并返回旧值。常量掩码采用立即数约束，其余采用寄存器约束。

`do { ... } while (0)` 使保存宏展开为一条复合语句，调用处的分号结束整个循环，可安全放在 `if (...) local_intr_save(flag); else ...` 中。循环条件恒假，宏体只执行一次。当前恢复宏直接展开函数调用且自带分号，工程中将它作为独立语句使用。

##### 4.4.1.2 中断状态的保存与恢复

SIE 是当前 hart 在 S 态运行时的中断全局使能位。S 态响应某个中断还要求相应来源已使能、处于待处理状态，并满足平台的委托关系。清除 SIE 可以屏蔽当前 S 态的这类可屏蔽中断；同步异常仍可发生，其他 hart 的执行也不受影响。

保存函数读取 SIE：原为1时关闭中断并返回1，原为0时返回0。局部变量 `intr_flag` 保存布尔状态，通常位于调用者的执行现场中。恢复函数仅在标志为1时重新开启；为0时保持现状。因此使用约定是临界区期间维持关闭状态，各层使用各自的保存变量，按进入顺序的逆序退出。

| 阶段 | 进入前开启 | 进入前关闭 |
| --- | --- | --- |
| 调用 save 前 | SIE=1 | SIE=0 |
| save 后 | flag=1，SIE=0 | flag=0，SIE=0 |
| 临界区内 | SIE=0 | SIE=0 |
| restore 后 | SIE=1 | SIE=0 |

如果退出时无条件开启中断，原本由外层关闭的中断会被提前打开。保存原状态可以使同一个函数兼容不同调用环境；`__intr_restore(false)` 本身没有再次清位，因此临界区内部也应遵守上述关闭约定。

软件保存使用普通变量；硬件 Trap 进入使用 SPIE 状态栈：进入 S 态 Trap 时，将原 SIE 写入 SPIE，再清 SIE；执行 `sret` 时，将 SPIE 恢复到 SIE，再将 SPIE置1。SPP另外记录返回特权级。软件临界区的 save/restore 不自动执行这套 Trap 操作。

##### 4.4.1.3 嵌套临界区分析

以进入外层前 SIE=1 为例，分别使用 `outer_flag` 和 `inner_flag`：

| 操作 | outer_flag | inner_flag | SIE |
| --- | --- | --- | --- |
| 外层 save | 1 | 尚未保存 | 0 |
| 内层 save | 1 | 0 | 0 |
| 内层 restore | 1 | 0 | 0 |
| 外层 restore | 1 | 0 | 1 |

内层看到的是已关闭状态，退出时不会破坏外层保护。若最初 SIE=0，两层保存值均为0，退出后也保持0。复用同一个变量会覆盖外层保存值，破坏这一配对关系；每层独立变量和逆序恢复共同保证正确性。

单核中断屏蔽使临界区内的相关操作不被本核中断处理抢入，形成相对于该类异步执行的原子区域。

##### 4.4.1.4 在进程管理中的应用

`do_fork()` 先完成 PCB、栈和初始现场准备，再在中断保护内执行 `get_pid → hash_proc → list_add → nr_process++ → wakeup_proc`。这一发布顺序使全局结构只暴露完整线程，PID、链表、哈希表、计数和可运行状态同步更新。

`schedule()` 的保护范围包含清除 `need_resched`、选择下一个可运行线程、维护 `runs` 和调用 `proc_run()`；后者在内部再次保存中断状态，保护 `current` 更新、`satp` 写入、TLB刷新和 `switch_to()`。内层保存到0，不会在外层尚未完成时开启中断。

上下文切换后，旧线程的栈和保存标志留在其执行现场，待它再次被调度回来，才继续执行自己的恢复语句。已有线程按各自进入临界区前的状态恢复；新线程首次运行通过预置 Trap frame 的 `SPIE=1`，经 `__trapret/sret` 开启中断。`kernel_thread()` 同时设置 SPP，并清除帧中的 SIE，保证首次返回状态符合内核线程约定。

#### 4.4.2 不同分页模式的工作原理

##### 4.4.2.1 分页机制与地址结构

虚拟地址由虚拟页号 VPN 和页内偏移组成，物理页号 PPN 指定物理页框。`satp` 提供根页表的物理页号，CPU以VPN的高位索引根表。有效非叶子 PTE 提供下一级页表的PPN；合法叶子提供最终映射与权限。普通4KiB转换将叶子PPN与原12位偏移组合，大页转换则使用相应的低位VPN组成更大的页内偏移，并要求物理基址对齐。

下列图示中的高位扩展不参与页表索引：

```text
Sv32：32位虚拟地址
31           22 21           12 11             0
+--------------+---------------+----------------+
| VPN[1] 10位  | VPN[0] 10位   | offset 12位    |
+--------------+---------------+----------------+

Sv39：64位寄存器承载规范虚拟地址
63          39 38      30 29      21 20      12 11          0
+-------------+----------+----------+----------+-------------+
| bit38的扩展 | VPN[2] 9 | VPN[1] 9 | VPN[0] 9 | offset 12   |
+-------------+----------+----------+----------+-------------+

Sv48：64位寄存器承载规范虚拟地址
63          48 47      39 38      30 29      21 20      12 11          0
+-------------+----------+----------+----------+----------+-------------+
| bit47的扩展 | VPN[3] 9 | VPN[2] 9 | VPN[1] 9 | VPN[0] 9 | offset 12   |
+-------------+----------+----------+----------+----------+-------------+
```

Sv39要求地址的63～39位等于bit38；Sv48要求63～48位等于bit47。寄存器宽度都是64位，有效地址位数分别为39和48。规范地址分别分布在64位编码空间的低端和高端，中间有非规范地址区间。

##### 4.4.2.2 Sv32、Sv39、Sv48 的对比

| 比较项目 | Sv32 | Sv39 | Sv48 |
| --- | --- | --- | --- |
| 适用架构／SXLEN | RV32／32 | RV64／64 | RV64／64 |
| 有效虚拟地址位数 | 32 | 39 | 48 |
| 页表层数 | 2 | 3 | 4 |
| 各级索引位数（高→低） | 10+10 | 9+9+9 | 9+9+9+9 |
| 页内偏移 | 12位 | 12位 | 12位 |
| 单项大小 | 4字节 | 8字节 | 8字节 |
| 每表项数 | 1024 | 512 | 512 |
| 基本页／页表页大小 | 4KiB／4KiB | 4KiB／4KiB | 4KiB／4KiB |
| 上层叶子支持的大页 | 4MiB | 2MiB、1GiB | 2MiB、1GiB、512GiB |
| 虚拟地址空间总容量 | 2³²字节=4GiB | 2³⁹字节=512GiB | 2⁴⁸字节=256TiB |
| satp.MODE编码 | 1（bit31） | 8（bits63:60） | 9（bits63:60） |

每张页表占一页，因而项数为 `4096 / sizeof(PTE)`：Sv32得到1024项，需要10位索引；Sv39/Sv48得到512项，需要9位。Sv48增加一级，相同4KiB基础上扩展9位VPN，容量增为Sv39的512倍。大页尺寸可由 `4096 × 项数^i` 推导，其中i为叶子下方省略的层数。

多级结构只为被使用的地址区域建立下级表，稀疏空间无需分配覆盖全部VPN的连续单级表。代价是TLB未命中时可能增加页表访存；对普通页，三级、四级分别最多读取三项、四项，实际还受页表遍历缓存影响。TLB缓存已有翻译和权限，命中后可跳过逐级查找；修改页表后需按映射变化失效相应缓存。

##### 4.4.2.3 当前实验的 Sv39 实现

[mmu.h](../code/kern/mm/mmu.h) 的普通页路径使用以下宏：

```c
#define PDX1(la) ((((uintptr_t)(la)) >> PDX1SHIFT) & 0x1FF)
#define PDX0(la) ((((uintptr_t)(la)) >> PDX0SHIFT) & 0x1FF)
#define PTX(la)  ((((uintptr_t)(la)) >> PTXSHIFT) & 0x1FF)
#define PGOFF(la) (((uintptr_t)(la)) & 0xFFF)
#define PTE_ADDR(pte) (((uintptr_t)(pte) & ~0x3FF) << (PTXSHIFT - PTE_PPN_SHIFT))
#define PDE_ADDR(pde) PTE_ADDR(pde)
```

三个移位常量依次为30、21、12，掩码提取9位索引。PTE的PPN从bit10开始，物理页基址从bit12开始，`pde_t/pte_t` 都是 `uintptr_t`。V表示有效；叶子R/W/X控制读写执行，W置位要求R置位；叶子U标识用户页。R=W=X=0的有效项指向下一级表。

RV64的`satp`字段为MODE[63:60]、ASID[59:44]、PPN[43:0]；RV32则为MODE[31]、ASID[30:22]、PPN[21:0]。ASID可区分地址空间，实际支持位数由实现决定。当前 [proc_run()](../code/kern/process/proc.c) 使用ASID=0：

```c
uintptr_t satp = (8UL << 60) |
                 ((proc->pgdir >> RISCV_PGSHIFT) & ((1UL << 44) - 1));
write_csr(satp, satp);
flush_tlb();
```

`proc->pgdir`和`boot_pgdir_pa`存物理地址，右移12位后写PPN；`boot_pgdir_va`是内核可访问的根表指针。`entry.S`也以MODE=8初始化satp。

[entry.S](../code/kern/init/entry.S) 在根项511建立从 `0xffffffffc0000000` 到物理 `0x80000000` 的1GiB映射，内核起点`0xffffffffc0200000`对应`0x80200000`。Lab4的4KiB映射使用三级表，普通页与该启动大页并存。写satp本身不自动替代TLB同步；`flush_tlb()`执行针对当前hart全部地址的`sfence.vma`，`tlb_invalidate()`携带目标地址，本地翻译失效后重新查询映射。

#### 4.4.3 get_pte() 中重复逻辑的分析

##### 4.4.3.1 三级页表查找过程

当前 [get_pte()](../code/kern/mm/pmm.c) 的签名为：

```c
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
```

`pgdir`是根表的内核虚拟地址；`la`是目标虚拟地址；`create`决定缺少中间表时是否分配。返回值是最底层目标PTE的内核地址，PTE本身可以尚未有效；`NULL`可表示路径不存在、分配失败或遇到上层大页。

第一段取`&pgdir[PDX1(la)]`。若V=0，create为0即返回；否则分配物理页、设置ref=1，通过`page2pa/KADDR`清零整页，再用`pte_create(page2ppn(page), PTE_U | PTE_V)`挂接到根表。检查上层叶子位后，通过`KADDR(PDE_ADDR(*pdep1))`访问下一级表。

第二段在该表中取PDX0项，重复检查、分配、设引用、清零和挂接步骤，建立最底层表。最后用PTX索引返回PTE指针。这两段分配的是页表页；数据页映射仍由`page_insert()`负责写入。

两段均在继续向下前检查`R/W/X`。硬件可在合法上层叶子处结束遍历，形成1GiB或2MiB映射；当前软件接口只定位4KiB PTE，遇到上层叶子便返回NULL，避免把大页的数据内容解释成下一张页表。

##### 4.4.3.2 两段代码相似的原因

多级页表具有重复的结构：每一层都是索引到条目的数组，非叶子项给出另一张同形页表的地址。Sv39前两次下降仅索引字段和当前表不同，资源初始化与挂接规则一致，因此代码自然相似。

定位普通4KiB项时，Sv32需下降一次，Sv39两次，Sv48三次；次数等于总层数减一，最终再在底层取条目。合法大页的硬件遍历提前结束，不能把上述次数推广到所有映射。若支持多种模式，可以围绕“层数、索引位数、PTE大小”设计遍历器，或复用取得下级表的辅助函数；Sv32的PTE宽度不同，抽象时还需区别条目读写类型。具体层级重复的原因来自分页树结构，而非需要分别设计两套分配算法。

##### 4.4.3.3 页表查询示例

取合法低半区地址 `la=0x0000000486856789`，这是自行构造的原理示例，未声称该地址在实际内核中已有映射。按宏计算：

| 字段 | 运算 | 结果 | 对应表内字节偏移 |
| --- | --- | --- | --- |
| PDX1 | `(la >> 30) & 0x1FF` | 0x12=18 | 18×8=144 |
| PDX0 | `(la >> 21) & 0x1FF` | 0x34=52 | 52×8=416 |
| PTX | `(la >> 12) & 0x1FF` | 0x56=86 | 86×8=688 |
| PGOFF | `la & 0xFFF` | 0x789=1929 | 数据页内偏移 |

地址可重新组合为 `(0x12<<30)|(0x34<<21)|(0x56<<12)|0x789`。bit38为0，高位也为0，满足Sv39规范地址规则。若两级均为有效非叶子，则路径为`pgdir[18] → 第二级表[52] → 第三级表[86]`，每次下级表地址按`KADDR(PDE_ADDR(当前项))`转换。若最终为普通叶子，物理地址为`PTE_ADDR(叶子)+0x789`。create=1可补齐缺失中间表，但返回PTE指针仍不代表数据映射已经建立。

#### 4.4.4 页表查找与分配接口的设计分析

##### 4.4.4.1 现有接口设计

`get_pte(pgdir, la, 0)`只查询已有结构，路径缺失即返回；`get_pte(pgdir, la, 1)`允许创建中间表。`get_page()`和`page_remove()`使用0，因为查询、删除都不应为不存在的地址额外分配资源；`page_insert()`使用1，为新映射准备PTE。原有自检也通过create=0检查定位结果，确认查询不改变页表。

##### 4.4.4.2 合并设计的优点与局限

统一函数复用索引、地址转换和大页判定，调用者只需选择create，适合当前小规模模块。代价是同一函数因参数不同而产生分配副作用，NULL又合并多种失败原因；随着层级增加，还需协调哪些表是此次新建、失败时可否撤销。

##### 4.4.4.3 接口拆分方案与比较

可以设计下列对外封装：

```c
pte_t *lookup_pte(pde_t *pgdir, uintptr_t la);  // 纯查找
pte_t *ensure_pte(pde_t *pgdir, uintptr_t la);  // 必要时创建
```

二者共享内部遍历器；分配接口记录该次新建的页表及其父项，失败时逆序撤销，只回收本次创建且仍为空的表，保留已有表和数据映射。概念流程如下：

```text
walk(root, address, policy):
    遍历目标底层之前的各级索引
    若遇到叶子，报告当前4KiB接口不支持继续下降
    若缺表且policy为lookup，返回未找到
    若允许创建，分配清零并登记新增表，然后挂接
    分配失败时撤销本次新增的空表，返回错误
    返回底层PTE地址
```

| 设计角度 | 单接口加create | 分离对外接口、共享内部遍历 |
| --- | --- | --- |
| 易用性与职责 | 调用短，需理解布尔参数 | 名称直接表明查询或分配 |
| 复用 | 一份遍历实现 | 仍复用一份遍历，增加薄封装 |
| 副作用 | 由参数隐含决定 | lookup明确无分配，ensure明确可分配 |
| 错误处理 | 需在同一函数协调两种策略 | 可明确归由ensure处理资源回滚 |
| 测试与扩展 | 每条分支同时检查模式 | 分别测试纯查询和创建事务，便于扩层 |

##### 4.4.4.4 设计结论

当前Lab4保留create参数的统一接口是合理的：调用点少，页表层级固定，能够直接服务查询、插入和删除。应先完善已确认的局部分配失败处理，再考虑重构。若后续扩展多种分页层级、独立地址空间或更复杂错误处理，采用lookup/ensure对外封装并共享内部遍历器更清晰，也更便于分别验证副作用和回滚。

---

## 五、测试与验证

本章汇总 Lab4 的集成测试结果。三项功能测试通过仓库自带的 `tools/inject.py` 注入到 `pmm_init()`、`proc_init()` 与 `init_main()` 的对应位置，由 `make grade` 统一构建、运行并检查串口输出；测试代码与评分脚本保持原样。

**（1）构建与评分命令。**

```bash
cd code
make -j4
make grade
```

**（2）测试结果。**

| 测试 | 检查内容 | 实际结果 |
| --- | --- | --- |
| `check PMM` | `page_insert()`/`page_remove()`、`check_pgdir()`、`check_boot_pgdir()` | OK |
| `check Process Create` | `idleproc`/`initproc` 的 PID、状态、内核栈、父进程、context 入口 | OK |
| `check Schedule` | 两个测试线程交替运行、上下文往返 | OK |
| 首次运行输出 | `initproc` 的 PID、名称与两行 `To U` | 符合预期 |
| 总分 | 三项测试合计 | 30/30 |

**（3）串口关键输出。**

```text
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
kmalloc_init() succeeded!
[Test PMM] PASS
check_vma_struct() succeeded!
check_vmm() succeeded.
[Test Process Create] PASS
++ setup timer interrupts
[Test Schedule] PASS
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

**（4）GDB 验证。** 首次运行链路的断点记录见 4.3.5（4）：`current` 从 PID 0 的 `idleproc` 切换到 PID 1 的 `initproc`，`satp` 指向 `0x80208000`，并依次经过 `switch_to → forkret → forkrets → __trapret → kernel_thread_entry → init_main`。

**（5）回归与未覆盖项。**

- 本模块未修改 Trap 与时钟相关代码；启动日志显示时钟中断已正常初始化（`++ setup timer interrupts`），Lab3 的 Trap 入口与中断处理路径未受影响。
- `init_main` 返回后进入 `do_exit()` 的 panic，本实验未实现进程退出，这是预期的收尾路径。
- 未覆盖内容：用户进程、缺页异常、页面置换与抢占式调度，均属于后续实验。
- 已知遗留：模块一记录的 `get_pte()` 第二级页表分配失败残留 1 个页表页（A08.2），不影响本次评分。

---

## 六、实验总结与收获

**（1）执行链闭环。** 从 `kern_init()` 出发，Lab4 的完整路径为：`pmm_init`（物理页、页表自检、kmalloc）→ `vmm_init` → `proc_init`（idleproc/initproc）→ `clock_init`/`intr_enable` → `cpu_idle` → `schedule` → `proc_run` → `switch_to` → `forkret` → `forkrets` → `__trapret` → `kernel_thread_entry` → `init_main`。三个模块的接口在同一次运行中闭合，评分测试也在此基础上额外创建并切换了测试线程。

**（2）设计取舍。** 调度点唯一放在 `cpu_idle()`，符合本实验“空闲线程主动让出 CPU”的模型；选择策略采用最简单的 FIFO/轮转，不引入时间片和优先级；`context` 只保存 callee-saved 寄存器，把调用者保存寄存器留给编译器；`satp` 按 Sv39 显式构造而不复用 `lcr3()`；调度与切换整体关中断，保证 `current`、页表、寄存器现场三者一致。

**（3）验证方法。** 采用分层验证：模块一、二、三各有专项断言与 PASS 标记，整体用 `make grade` 检查 30/30，再用 GDB 断点确认首次运行链路的每个衔接点。报告中的测试结论均来自实际运行记录，未执行的检查不记为通过。

**（4）遗留问题。** 模块一在 `get_pte()` 第二级分配失败时的完整回滚尚未实现（A08.2，残留 1 个页表页）；`do_exit()` 仍是 panic 占位；没有抢占式调度与优先级；用户进程、缺页异常和页面置换留给后续实验。

**（5）收获。** 通过本实验理解了进程切换并非简单的“跳转”，而是 `current`、页表与寄存器现场三件事的原子更新；理解了 `context` 与 Trap frame 的分工，以及新线程首次运行必须借助 `forkret → forkrets → __trapret` 链路的原因；理解了 `satp` 的 MODE/PPN 编码与切换后刷新 TLB 的必要性；也体会到“先约定接口、再分层验证”的工程方法对多人协作的价值。
