# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
| --- | --- |
| **实验名称** | Lab4：进程管理 |
| **小组成员** | 2411326-张晨旭、2411566-罗晟皓、2413042-郭桦 |
| **完成日期** | 待最终集成完成后填写 |

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

下图表示各模块完成后的目标执行链，进程创建和调度部分仍需实现与集成验证：

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

本模块依据 [Lab4 实验指导书](<../docs/lab4 进程管理.md>)，完成 [pmm.c](../code/kern/mm/pmm.c) 中的 `page_insert()` 和 `page_remove()`，在现有 Sv39 页表框架上建立、更新和撤销 4 KiB 页映射。通过维护页表项、物理页引用计数和 TLB，使软件记录的映射关系与处理器实际使用的地址翻译保持一致，为后续内存和线程管理提供基础。

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

先运行原始自检，再在仓库外源码副本增加补充断言。原始测试文件和生产代码不包含临时插桩；构建产物与日志位于 `/tmp`。以下命令已在此前验证阶段执行，本次整理报告未重新运行测试：

```bash
make -C code -j2
make -C code -j2 OBJDIR=/tmp/lab4-pmm-3hl5rp1c/base-obj BINDIR=/tmp/lab4-pmm-3hl5rp1c/base-bin
python3 /tmp/lab4-pmm-3hl5rp1c/run_qemu41.py baseline41
python3 /tmp/lab4-pmm-3hl5rp1c/prepare_tests.py
python3 /tmp/lab4-pmm-3hl5rp1c/run_qemu41.py tests41
python3 /tmp/lab4-infra-5pkrcu3r/run.py baseline
python3 /tmp/lab4-infra-5pkrcu3r/prepare.py
python3 /tmp/lab4-infra-5pkrcu3r/run.py supplemental
```

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

### 4.2 中断与时钟中断处理

**负责人：** 2411566-罗晟皓

#### 4.2.1 模块功能描述

#### 4.2.2 关键接口与设计

#### 4.2.3 具体实现

#### 4.2.4 Spec 与 Prompt

#### 4.2.5 实现及验证过程

### 4.3 异常处理

**负责人：** 2413042-郭桦

#### 4.3.1 模块功能描述

#### 4.3.2 关键接口与设计

#### 4.3.3 具体实现

#### 4.3.4 Spec 与 Prompt

#### 4.3.5 实现及验证过程

---

## 五、测试与验证

待汇总 Lab4 各模块专项测试及成员 C 的最终集成、回归结果后填写。

---

## 六、实验总结与收获

待 Lab4 最终集成完成后，结合实际实现、测试结果和遗留问题填写。
