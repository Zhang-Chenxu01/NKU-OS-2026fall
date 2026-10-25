# Lab2 功能模块一实现提示词

```
# OS Lab2 — 物理内存初始化

## [PROMPT]

**任务**：在当前 Lab2 源码中，实现 `kern/mm/pmm.c` 中 `//lab2 function1 YOUR CODE` 所对应的 `static void page_init(void)`，完成物理内存信息读取、Page 描述符初始化、已占用物理内存保留和连续空闲页注册。

**操作要求**：

1. 必须先阅读仓库真实代码，再修改实际文件，不能只输出代码示例。
2. 修改范围限定为 `kern/mm/pmm.c` 中的 `page_init()` 实现位置。
3. 保留现有初始化调用流程及全部公开接口，不修改 First-Fit、Best-Fit、Buddy 或 SLUB 的代码。
4. 不得修改课程提供的测试函数和评分脚本。
5. 如果发现其他源码缺陷，单独报告，不自行扩大修改范围。
6. 实现后执行可用的构建检查，提供真实运行结果，未执行的测试明确标注。

## [RELY]

请检查并使用项目中真实存在的下列资源：

**物理内存信息**
- `get_memory_base()`
- `get_memory_size()`

**地址及页面管理**
- `KERNBASE`
- `PHYSICAL_MEMORY_OFFSET`
- `PHYSICAL_MEMORY_END`
- `DRAM_BASE`
- `PGSIZE`、`PGSHIFT`、`PPN`
- `ROUNDUP()`、`ROUNDDOWN()`
- `PADDR()`、`pa2page()`、`page2pa()`

**数据结构与状态**
- `struct Page`
- `pages`、`npage`、`nbase`
- `va_pa_offset`
- `SetPageReserved()`、`PageReserved()`

**已有函数**
- `init_memmap(struct Page *base, size_t n)`
- `panic()`、`cprintf()`

**链接符号**
- `end`

检查 `default_init_memmap()` 的状态约定，以及 `tools/test_lab2.c::test_page_init()` 中的真实测试条件。

注意：本仓库中的 `KADDR()` 已被注释，不允许直接调用。

## [GUARANTEE]

必须实现：

`static void page_init(void);`

成功返回时，必须设置：

- `va_pa_offset`
- `npage`
- `pages`

必须使用已有 `init_memmap()` 向当前物理内存管理器注册连续空闲区域。

允许根据需要增加仅在本模块内部使用的 `static` 辅助函数，但不允许新增或更改公开 API。

## [SPECIFICATION]

### page_init()

**Pre-Condition**

- `dtb_init()` 已执行，物理内存信息可以通过 getter 获得。
- `init_pmm_manager()` 已执行，当前分配器已完成基本初始化。
- Sv39 启动页表已经启用。
- 内核 `end` 符号表示内核镜像末端的虚拟地址。

**Post-Condition**

- 建立覆盖所有可管理物理页的 Page 描述符数组。
- OpenSBI、内核镜像、Page 描述符数组及对齐空隙所占用的页面均不可被分配。
- `pages[0]` 对应物理页号 `nbase`。
- 可用完整物理页被正确交给当前 `pmm_manager`。
- 对合法 Page 描述符，`pa2page(page2pa(page)) == page`。
- 物理内存范围和初始化结果可以通过内核日志检查。

**Case 1：DTB 信息合法**

读取物理内存起点及大小，检查地址范围和整数运算安全性。

设置虚实地址偏移和管理上界。

根据 `end`、描述符数量以及 `sizeof(struct Page)` 确定元数据布局。初始化全部 Page 描述符为保留状态。

从描述符数组末端之后寻找第一个完整空闲物理页，并将剩余的连续完整页面交给 `init_memmap()`。

**Case 2：DTB 信息无效**

当物理内存起点、大小、终点或地址布局不符合当前内核的管理约束时，应通过 `panic()` 终止初始化，不允许继续进行越界访问。

**Case 3：内存空间不足**

当可管理内存无法容纳内核及描述符数组，或者已经不存在完整空闲页时，不得调用 `init_memmap()`。

考虑到后续分配测试要求存在可分配页面，应输出明确原因并安全终止。

**Requirements**

1. 严格区分虚拟地址、物理地址、物理页号和 Page 描述符地址。
2. `PADDR()` 调用之前必须正确设置 `va_pa_offset`。
3. `npage` 为物理页号排他上界，描述符数量为 `npage - nbase`。
4. 检查地址加法、数组大小乘法及描述符结束地址是否溢出。
5. 描述符数组必须放置在 `end` 之后并满足页面对齐要求。
6. 只释放完整的物理页，不将任何内核或元数据页面交给分配器。
7. 不得以 `n == 0` 调用 `init_memmap()`。
8. 以当前框架的内存上界和实际代码定义为准，不得硬编码运行时 `end`、`pages` 或空闲页数量。
9. 不修改 `init_pmm_manager()` 的分配算法选择，这由集成阶段统一处理。

**最终输出**

- 实际修改的文件和代码差异。
- `page_init()` 的执行流程解释。
- 物理内存布局及地址计算方式。
- 与分配器的接口说明。
- 编译、运行及测试结果。
- 尚未验证的情况和潜在风险。
```

# Best-Fit 物理内存分配器实现提示词

````markdown
[PROMPT]

任务：修改 code/kern/mm/best_fit_pmm.c，参考 default_pmm.c 中已有的 First-Fit 算法，实现完整的 Best-Fit 连续物理内存分配器，并在 code/kern/mm/pmm.c 中将当前管理器切换为 Best-Fit。

操作要求：必须修改项目实际文件，不只是输出代码。根据现有 pmm_manager 框架完成内部状态、初始化、页面分配、释放、计数及管理器注册。

保留课程组提供的 basic_check() 和 best_fit_check()，不得修改其内容。不修改已有 page_init() 或其他内存分配算法。

输出要求：根据下列接口与规格实现并完成基本编译、QEMU 验证，说明实际修改和测试结果。

[RELY]

相关数据结构：

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

typedef struct {
    list_entry_t free_list;
    unsigned int nr_free;
} free_area_t;

物理内存管理器接口：

struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

可依赖：

list_init()、list_add()、list_add_before()、list_del()、list_next()、list_prev()、list_empty()。

le2page()。

SetPageProperty()、ClearPageProperty()、PageReserved()、PageProperty()。

set_page_ref()。

default_pmm.c 中已有 First-Fit 实现。

best_fit_pmm.c 中已有 basic_check()、best_fit_check()。

best_fit_pmm.h 中已有的 best_fit_pmm_manager 声明。

[GUARANTEE]

必须实现：

static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);

const struct pmm_manager best_fit_pmm_manager;

允许增加模块内部的 static 状态和辅助函数。

best_fit_pmm_manager.check 必须指向课程组原有的 best_fit_check()。

修改 init_pmm_manager() 中的管理器选择，不改变 page_init() 的实现。

[SPECIFICATION]

best_fit_init()

Pre-Condition：管理器尚未登记空闲物理页。

Post-Condition：

初始化双向循环空闲链表。

将空闲物理页计数置零。

best_fit_init_memmap(base, n)

Pre-Condition：

n > 0。

base 指向连续的 n 个 Page 描述符。

页面原先处于 Reserved 状态。

Post-Condition：

清除可用页的保留状态并初始化管理信息。

设置空闲块首部的 property=n 和 PG_property。

按物理地址顺序将空闲块加入链表。

nr_free 增加 n。

best_fit_alloc_pages(n)

Pre-Condition：n > 0。

Post-Condition：

成功时返回满足 n 个连续物理页请求的起始 Page。

失败时返回 NULL，且不改变分配器状态。

正确维护空闲块链表、property、PG_property 和 nr_free。

Case 1：总空闲页数不足

当 n > nr_free 时返回 NULL。

Case 2：不存在足够大的连续空闲块

即使总空闲页数足够，遍历后仍找不到满足要求的连续块，也应返回 NULL。

Case 3：存在多个候选空闲块

必须选择所有 property >= n 的空闲块中大小最小的一个。大小相同时选择地址靠前的空闲块。

Case 4：恰好匹配

移除整个空闲块，不生成剩余块。

Case 5：需要拆分

从选中块的起始位置分配 n 页，将剩余页面组织成新的空闲块，并保持链表地址顺序。

Requirements：

不得找到第一个合适块就直接停止，否则将退化为 First-Fit。

允许使用线性扫描，不要求额外优化时间复杂度。

best_fit_free_pages(base, n)

Pre-Condition：

n > 0。

释放区域为合法的已分配连续物理页。

Post-Condition：

清除释放区域的分配状态。

按物理地址顺序插入空闲链表。

正确维护空闲页计数。

Case 1：左右均不相邻，保留独立空闲块。

Case 2：仅左侧相邻，向左合并。

Case 3：仅右侧相邻，向右合并。

Case 4：左右均相邻，合并成一个连续空闲块。

Requirements：

根据物理地址连续关系判断是否能够合并。

合并本身不得重复增加 nr_free。

best_fit_nr_free_pages()

Post-Condition：返回当前空闲物理页总数，而不是空闲块数量。
````

# Best-Fit 代码复查与测试提示词

````markdown
[PROMPT]

任务：复查已经实现的 best_fit_pmm.c 和 pmm.c，确认算法正确、接口完整、课程组原有测试通过。

操作要求：检查真实源码、Git 差异以及实际运行结果。如果发现实现错误，限于本任务范围修复并重新测试。

输出要求：完成编译、QEMU 运行与评分模式验证。全部通过后，将成员二代码提交并安全推送到共享仓库 lab2 分支。不编写报告或制作截图。

[RELY]

best_fit_pmm.c：本次实现的管理器。

default_pmm.c：First-Fit 参考实现。

pmm.c：管理器切换与初始化。

pmm.h、memlayout.h、list.h：接口与数据结构。

basic_check()、best_fit_check()：课程组提供的原始测试。

Makefile：提供 make、make qemu、make test。

QEMU 4.1.1：/usr/local/bin/qemu-system-riscv64。

[GUARANTEE]

必须复查：

best_fit_init();
best_fit_init_memmap();
best_fit_alloc_pages();
best_fit_free_pages();
best_fit_nr_free_pages();

必须确认 best_fit_pmm_manager 正确注册并被内核选用。

禁止修改课程组测试函数、成员一的 page_init() 和其他组员模块。

Git 操作不得强制推送、重写已有历史或覆盖他人工作。

[SPECIFICATION]

分配逻辑复查

Pre-Condition：Best-Fit 已实现并接入内核。

Post-Condition：

选择最小合适块。

相同大小候选块的选择规则一致。

恰好匹配、拆分和失败返回均正确。

分配前后空闲页计数保持一致。

释放逻辑复查

Post-Condition：

能够处理左邻、右邻、双邻及无相邻块四种情况。

空闲块长度、链表节点和标志正确。

回收后空闲页数量正确。

测试验收

Case 1：正常构建

执行 make clean、make，确认编译与链接成功。

Case 2：QEMU 运行

执行：

make qemu QEMU=/usr/local/bin/qemu-system-riscv64

确认运行的是 best_fit_pmm_manager，并实际通过 check_alloc_page()。

Case 3：评分模式

清理构建产物后执行：

make test QEMU=/usr/local/bin/qemu-system-riscv64

检查 best_fit_check() 是否达到 grading: 6 / 6 points。

Case 4：边界行为

补充验证最小匹配、空闲块拆分、释放合并、分配失败及空闲页数量守恒。

Requirements：

不修改或绕过课程组断言。

真实记录测试结果。

提交前确认远程同步状态。

仅提交成员二实际修改的代码。
````

# SLUB 分配模块实现提示词

````markdown
[PROMPT]

**任务**：在 `code/kern/mm/` 下新增 `slub.c`、`slub.h`，实现简化的 SLUB 内存对象分配模块。底层通过物理页分配器获取页面，上层将页面划分为不同大小的对象槽位，实现按字节分配、释放、复用及整页回收。

**操作要求**：必须修改项目实际文件，保留已有物理内存管理代码。不修改 First-Fit、Best-Fit、Buddy System 和课程测试。

**输出要求**：直接完成模块实现并检查编译。依据以下接口和规格，不引入无关功能。本轮不编写测试、报告，不提交 Git。

[RELY]

- `kern/mm/pmm.h`：`alloc_pages(size_t n)`、`free_pages(struct Page *base, size_t n)`、`nr_free_pages()`、`page2pa()`、`pa2page()`、`va_pa_offset`。
- `kern/mm/memlayout.h`：`struct Page`、`PHYSICAL_MEMORY_OFFSET`。
- `kern/mm/mmu.h`：`PGSIZE`、`PGSHIFT` 及页面对齐宏。
- `libs/list.h`：已有双向链表接口。
- 当前内核使用高地址直接映射；`KADDR()` 已被注释，不能调用。页对应的内核虚拟地址可由物理地址与已建立的虚实偏移换算。

以实际源码定义为准。

[GUARANTEE]

必须实现以下接口：

```c
void slub_init(void);
void *slub_alloc(size_t size);
void slub_free(void *ptr);
```

允许添加模块内部的 `static` 辅助函数、静态变量和元数据结构，不修改已有公开内存管理接口。

[SPECIFICATION]

## slub_init()

**Pre-Condition**：
- `pmm_init()` 已完成，底层物理页分配器可用。

**Post-Condition**：
- 初始化对象大小类别及对应空闲对象管理结构。
- 不预先占用不必要的物理页。

## slub_alloc(size_t size)

**Pre-Condition**：
- SLUB 已初始化。

**Post-Condition**：
- 成功返回满足大小和对齐要求的可写内核虚拟地址。
- 不同有效分配对象的内存区域不重叠。

**Case 1：小对象分配**
- 根据请求选择最小的合适大小类别，例如 16、32、64、128、256、512、1024、2048 字节。
- 优先复用已有空闲槽位；没有可用槽位时，向底层申请页面并划分对象。
- 页面元数据不得与对象区域重叠。

**Case 2：大对象分配**
- 对超过最大小对象类别的请求，申请足够的连续物理页。
- 正确记录实际占用页数，供后续释放。

**Case 3：无法分配**
- 大小为零、申请大小计算溢出或底层内存不足时，返回 `NULL`，不破坏已有分配状态。

**Requirements**：
- 返回地址至少满足 16 字节对齐。
- 所有元数据由静态存储或底层物理页承载，不依赖 `malloc()`。
- 正确处理页内元数据占用及大小计算溢出。

## slub_free(void *ptr)

**Pre-Condition**：
- `ptr` 为此前成功分配且尚未释放的有效对象地址，或者 `NULL`。

**Post-Condition**：
- 对象被正确回收，可供后续申请复用。
- 当一个 SLUB 页面内全部对象释放后，将该物理页归还底层分配器。
- 大对象按照实际分配页数完整释放。

**Case 1**：
- `ptr == NULL` 时不执行操作。

**Case 2**：
- 小对象释放后恢复对应空闲槽位，更新页面使用计数。

**Case 3**：
- 页面已无占用对象时回收整个页面。

**Requirements**：
- 不修改其他仍在使用的对象。
- 不发生重复页回收或空闲链表状态破坏。
````

# SLUB 测试函数提示词

````markdown
[PROMPT]

**任务**：基于已经实现的 `kern/mm/slub.c`、`slub.h`，新增 `kern/mm/slub_test.c`，设计 SLUB 分配与释放的测试函数，并在 `kern/init/init.c` 中增加受 `SLUB_SELFTEST` 编译宏控制的测试入口。

**操作要求**：必须修改实际文件。测试应覆盖不同大小对象、槽位复用、跨页分配、页面回收和内存不足等情况。

**输出要求**：编译运行 QEMU，报告真实测试结果。不修改分配器算法、课程测试及实验报告，不提交 Git。

[RELY]

- `slub_init()`、`slub_alloc()`、`slub_free()`：第一份 Prompt 实际实现的接口。
- `nr_free_pages()`：检查底层空闲页数量。
- `assert()`、`cprintf()`：现有断言与输出接口。
- `kern/init/init.c`：`kern_init()` 调用 `pmm_init()` 完成物理内存初始化。

[GUARANTEE]

必须实现：

```c
void slub_selftest(void);
```

仅在定义 `SLUB_SELFTEST` 时，于 `pmm_init()` 完成后调用 `slub_init()` 和 `slub_selftest()`。

允许添加测试辅助函数，但不得修改现有内存分配算法或原有测试函数。

[SPECIFICATION]

## slub_selftest()

**Pre-Condition**：
- 物理页管理器及 SLUB 已初始化。

**Post-Condition**：
- 所有测试对象均被释放。
- 底层空闲物理页数量恢复到测试前的值。
- 每项测试使用断言检查真实行为，只有全部通过才输出成功信息。

**Case 1：大小类别**
- 测试 1、16、24、100、500、1024、2048 字节及超过一页的请求。
- 验证返回地址对齐、对象可读写。

**Case 2：对象独立性与复用**
- 连续分配多个同类及不同类对象。
- 写入不同内容并验证没有互相覆盖。
- 释放部分对象后重新申请，验证空闲槽位能够被复用。

**Case 3：页面回收**
- 连续分配足够多的对象，使同一类别使用多个页面。
- 释放全部对象，确认底层空闲页数量恢复。

**Case 4：边界情况**
- 验证零长度申请、空指针释放、过大申请及内存不足时的规定行为。
- 失败后不破坏其他有效对象。

**Requirements**：
- 测试成功后输出 `[SLUB] ALL TESTS PASS`。
- 失败时必须通过真实断言或明确错误输出暴露问题。
- 使用 QEMU 4.1.1 验证，并在清理构建产物后复测默认 Best-Fit，确保普通运行不受影响。
- 仅记录实际执行的命令和结果。
````

---

# Buddy System 物理内存分配器实现提示词

> 依据 lab0.5 四段式框架（[PROMPT] / [RELY] / [GUARANTEE] / [SPECIFICATION]）编写；实现文件为 `code/kern/mm/buddy_pmm.c`、`buddy_pmm.h`，并在 `code/kern/mm/pmm.c` 中通过 `USE_BUDDY_PMM` 接入。

````markdown
## [PROMPT]

**任务**：在现有 riscv64-ucore lab2 框架中实现 Buddy System 物理内存分配器模块，涉及三个文件：

1. 新增 `kern/mm/buddy_pmm.h`：头文件保护，包含 `<pmm.h>`，声明 `extern const struct pmm_manager buddy_pmm_manager;`。
2. 新增 `kern/mm/buddy_pmm.c`：实现 `struct pmm_manager` 的六个接口，注册 `buddy_pmm_manager`。
3. 修改 `kern/mm/pmm.c`：只做两处最小接入——顶部 `#include <buddy_pmm.h>`；在 `init_pmm_manager()` 中用编译开关 `USE_BUDDY_PMM` 选择 `buddy_pmm_manager`，未定义该宏时仍选 `best_fit_pmm_manager`。

**操作要求**：

- 必须在项目中修改真实文件，而不是只输出代码片段；保留现有代码与注释，`pmm.c` 不重写。
- 不得实现或修改 `page_init()`；不得修改 `best_fit_pmm.c`、`pmm.h`、`memlayout.h`、`mmu.h`、`libs/*`、`Makefile`、`tools/*`；不得新增 `kern/test` 目录。
- 本提示词不生成任何测试代码、测试用例、测试脚本、QEMU/GDB 命令或评分步骤；`buddy_check()` 本阶段保持空实现（测试由第二份提示词负责）。
- 允许用 `assert` 做参数与不变量防御，但不得把它写成测试函数或测试场景。
- 参考 Linux buddy 的核心语义，但不得复制 Linux 源码；不引入 zone、迁移类型、per-CPU pageset、watermark、compaction、OOM、pageblock、memory hotplug、多核锁。
- 编译选项含 `-Werror`，代码必须没有警告；只使用内核已有头文件和 C 标准类型，不使用 malloc、printf、浮点或 libc。
- 如果 `page_init()` 或 `best_fit_pmm.c` 尚未完成，不要补实现；本阶段只保证 `make obj/kern/mm/buddy_pmm.o` 能编译通过。不得运行 QEMU/GDB，也不得生成任何测试或评分命令。
- 最终默认 manager 必须是 `best_fit_pmm_manager`；Buddy 只能通过 `USE_BUDDY_PMM` 临时选中。

**输出要求**：直接修改文件；只使用 [RELY]、[GUARANTEE]、[SPECIFICATION] 中的事实与规格；以仓库真实代码为准，不要引入无关功能；每完成一个阶段按附录 A 的格式报告门禁结果。

---

## [RELY]

### 工程事实

| 项 | 值 / 说明 |
| --- | --- |
| 页大小 | `PGSIZE = 4096`，`PGSHIFT = 12` |
| 绝对页号 | `pfn = page2ppn(page) = page2pa(page) >> PGSHIFT` |
| 合法页号范围 | `[nbase, npage)`，本机 `nbase = 0x80000`、`npage = 0x88000`（以实际启动值为准） |
| 页描述符基址 | 全局 `struct Page *pages` |
| 页号换算 | `pa2page(pfn << PGSHIFT)` |
| 页大小相关的宏 | `PGSIZE`、`PGSHIFT`、`PPN(pa)`、`ROUNDUP`、`ROUNDDOWN` |
| 计数语义 | `buddy_free_area[o].nr_free` 是 **块数**；`buddy_nr_free` 是 **页数** |
| 构建 | `kern/mm/*.c` 由 Makefile 自动收集，新增 `buddy_pmm.c` 不需要改 `Makefile` |
| 编译标志 | 以仓库 Makefile 为准：`-mcmodel=medany -std=gnu99 -Werror -Wall -O2 -nostdinc -fno-builtin ...` |

### 数据结构（与 `kern/mm/memlayout.h` 一致）

```c
struct Page {
    int ref;                 // 页引用计数
    uint64_t flags;          // 状态位，含 PG_reserved / PG_property
    unsigned int property;   // Buddy 中存空闲块的 order
    list_entry_t page_link;  // 挂入 free_area_t.free_list
};

typedef struct {
    list_entry_t free_list;  // 同 order 空闲块链表头
    unsigned int nr_free;    // 该 order 的空闲块数（不是页数）
} free_area_t;
```

### 可使用的宏与函数

- 页状态：`PageReserved`、`SetPageReserved`、`ClearPageReserved`、`PageProperty`、`SetPageProperty`、`ClearPageProperty`
- 引用计数：`set_page_ref(page, val)`、`page_ref(page)`
- 地址换算：`page2ppn(page)`、`page2pa(page)`、`pa2page(pa)`
- 链表：`list_init`、`list_add`、`list_add_before`、`list_del`、`list_del_init`、`list_empty`、`list_next`、`list_prev`、`le2page`
- 断言与输出：`assert`、`cprintf`

`list_add(listelm, elm)` 的语义是“把 `elm` 插到 `listelm` 之后”；传入链表头时就是插入到链表头之后（头插）。

### 地址与页号约定

- 所有块定位一律使用绝对页号 `pfn`，禁止用 `page - pages` 代替。
- 一个空闲块只把 **块头页** 挂链：块头 `PageProperty = 1`，`property = order`；块内其余页不挂链。
- 从链表摘除一个块时，必须清 `PageProperty` 并把 `property` 置 0。
- 分配返回的块头 `PageProperty` 必须为 0。

### Buddy 行为参照（Linux 精简版）

- 只有一份全局状态：`buddy_free_area[0..BUDDY_MAX_ORDER]`，order 为 `o` 表示块大小是 `2^o` 页；`BUDDY_MAX_ORDER = 10`（4 MiB）。
- 初始化：把一段连续可用物理页从起始 pfn 开始，按“当前 pfn 对齐、且不越过区间末尾”的最大 2 的幂逐块切分，块头挂入对应 order 链表。
- 分配：把请求页数向上取整为 order；从该 order 起找第一条非空链表；取出块后从原 order 逐级拆到目标 order，每次把后半块挂回低一级链表，前半块作为结果返回。
- 释放：把释放页数向上取整为 order；伙伴 pfn 为 `pfn ^ (1 << order)`；只有伙伴在 `[nbase, npage)` 内、是空闲块头且 order 相同才合并；合并后块头取两者较小的 pfn，order 加一，继续向上尝试。
- 计数：分配只减少 `2^目标order` 页；释放只在入口按 **原始 order** 增加 `2^order` 页，合并过程不重复增加。
- 不变量：`Σ(buddy_free_area[o].nr_free * 2^o) == buddy_nr_free`；每个块的起始 pfn 按 `2^order` 对齐；块之间不重叠、不漏页。

---

## [GUARANTEE]

### 必须实现的 `pmm_manager` 接口

函数名、参数、返回值必须严格如下：

```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);
```

### 必须提供的 manager 表

```c
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
```

### 必须包含的内部辅助接口

```c
static unsigned int buddy_order_for(size_t n);
static void buddy_free_block(struct Page *base, unsigned int order);
```

- `buddy_order_for(n)`：返回最小的 order，使 `2^order >= n`。
- `buddy_free_block(base, order)`：把一个已按 `2^order` 对齐的块插入空闲链表（G2 只插入；G4 扩展为“插入 + 合并”）。

### 允许新增的内容

- `static` 辅助函数（只在本模块内部使用，不改变公开接口）；
- 模块内部宏/常量与静态变量：`BUDDY_MAX_ORDER`（10）、`BUDDY_NR_ORDERS`（11）、`buddy_free_area[BUDDY_NR_ORDERS]`、`buddy_nr_free`；
- 所有模块内部符号统一加 `buddy_` 前缀，避免与 Best-Fit 的 `free_list` / `nr_free` 冲突。

### 接入约束

`buddy_pmm.h` 必须包含 `<pmm.h>` 并声明 `extern const struct pmm_manager buddy_pmm_manager;`；`buddy_pmm.c` 包含 `buddy_pmm.h`、`list.h`、`stdio.h`。

`pmm.c` 只允许两处改动：

```c
#include <buddy_pmm.h>
...
static void init_pmm_manager(void) {
#ifdef USE_BUDDY_PMM
    pmm_manager = &buddy_pmm_manager;
#else
    pmm_manager = &best_fit_pmm_manager;
#endif
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}
```

`buddy_check()` 在本提示词中只保留空实现（可打印一行未实现提示），不得写测试。

---

## [SPECIFICATION]

### buddy_init

**Pre-Condition**：
- 由 `init_pmm_manager()` 在 `page_init()` 之前调用一次。

**Post-Condition**：
- 所有 order 的链表为空，`buddy_free_area[o].nr_free == 0`，`buddy_nr_free == 0`。

**Case 1**：
- 任意初始状态：初始化全部链表头与两个计数器，不依赖先前状态。

**Requirements**：
- 不分配、不释放页，不访问 `[nbase, npage)` 之外的物理页。

### buddy_order_for

**Pre-Condition**：
- `n >= 1`。

**Post-Condition**：
- 返回最小 order `o ∈ [0, BUDDY_MAX_ORDER]`，使 `2^o >= n`。

**Case 1**：
- `n = 1` 返回 0；`n = 3` 返回 2；`n = 5` 返回 3；`n = 1024` 返回 10。

**Case 2**：
- `n > 2^BUDDY_MAX_ORDER` 时返回 `BUDDY_MAX_ORDER`（由调用方负责拒绝该请求）。

**Requirements**：
- 纯函数，不修改任何状态；分配和释放共用。

### buddy_free_block

**Pre-Condition**：
- `base` 是某块的块头，起始 pfn 按 `2^order` 对齐且块整体在 `[nbase, npage)` 内；
- `order <= BUDDY_MAX_ORDER`；`base` 当前不是空闲块头。

**Post-Condition**：
- 该块（可能经过若干次向上合并）成为某个 order 的空闲块头，挂在对应链表上；
- 块头 `PageProperty = 1`、`property = order`；
- 合并后的块大小、块数、`buddy_nr_free` 与最终状态一致。

**Case 1**：
- 伙伴 pfn 越界（不在 `[nbase, npage)`）、伙伴不是空闲块头、或伙伴 order 不同：停止合并，直接把当前块插入对应链表。

**Case 2**：
- 伙伴在范围内且是同 order 空闲块头：把伙伴从链表摘除、清其块头标记与 `property`、对应 order 块数减一，块头更新为两者较小的 pfn，order 加一，继续尝试更高级合并。

**Case 3**：
- 合并到 `BUDDY_MAX_ORDER`：停止合并，把最终块挂入该 order。

**Requirements**：
- 初始化与释放共用该函数；初始化阶段还没有可合并伙伴，因此只做插入；
- `buddy_nr_free` 只按 **原始 order** 增加一次，不能在每次合并时重复增加。

### buddy_init_memmap

**Pre-Condition**：
- `base` 指向 `pages` 中的页；`n > 0`；区间 `[base, base + n)` 在 `[nbase, npage)` 内；
- 区间内每页此前已被标记 `PageReserved`。

**Post-Condition**：
- 区间内所有页的 `flags`、`property` 清零，引用计数置 0；
- 区间被完整切分为若干按 `2^order` 对齐的空闲块并挂入对应 order 链表，块头标记与 order 正确；
- 各 order 块数与 `buddy_nr_free` 正确累加，`Σ(nr_free * 2^o) == buddy_nr_free`；
- 块之间不重叠、不漏页，最远覆盖到区间末尾。

**Case 1**：
- 起始 pfn 未按大块对齐：只能切出与当前对齐程度相符的小块，后续 pfn 对齐变好后切出更大的块。

**Case 2**：
- 页数不是 2 的幂：最后一段按剩余页数能取的最大对齐块切分，不越界。

**Case 3**：
- 区间足够大且对齐良好：优先取不超过 `2^BUDDY_MAX_ORDER` 的最大块。

**Requirements**：
- 使用绝对 pfn 计算边界，禁止 `page - pages`；
- 不得假设起始页号按大块对齐，也不得假设 `n` 是 2 的幂；
- 每页只被一个块覆盖，计数器不得重复累加。

### buddy_alloc_pages

**Pre-Condition**：
- `n >= 1`；分配器已初始化且已接收空闲区间。

**Post-Condition**：
- 成功时返回一个连续块的块头地址，块大小为 `2^ceil(log2 n)` 页；
- 返回块的 `PageProperty == 0`、`property == 0`，块内页不再属于任何空闲块；
- `buddy_nr_free` 减少 `2^目标order`，各 order 块数按拆分结果更新；
- 失败时返回 `NULL`，且空闲区布局与调用前完全一致。

**Case 1**：
- `n == 0` 或 `n > 2^BUDDY_MAX_ORDER`：直接返回 `NULL`，状态不变。

**Case 2**：
- 目标 order 的链表非空：直接取第一个块，不拆分。

**Case 3**：
- 目标 order 为空、但更高 order 有块：取最小的可用高阶块，从高阶逐级拆到目标 order；每次拆分的后半块成为低一级的空闲块头，前半块继续保留。

**Case 4**：
- 所有 order 都没有可用块：返回 `NULL`，状态不变。

**Requirements**：
- 拆分只改变块布局，不改变空闲页总数；只有最终分配减少 `2^目标order`；
- 拆出的后半块必须按 `2^(o-1)` 对齐并挂入 `o-1` 链表；
- 返回的块头必须按目标 order 对齐且整体在 `[nbase, npage)` 内。

### buddy_free_pages

**Pre-Condition**：
- `base` 非空且是此前分配返回的块头；`n >= 1`；`n <= 2^BUDDY_MAX_ORDER`。

**Post-Condition**：
- 释放的块（可能已向上合并）挂入对应 order 链表，块头标记与 order 正确；
- `buddy_nr_free` 增加 `2^原始order`；
- 不越界、不与不同 order 的块合并、不破坏其他空闲块。

**Case 1**：
- 伙伴 pfn 不在 `[nbase, npage)`：不合并，直接插入。

**Case 2**：
- 伙伴在范围内但不是空闲块头：不合并。

**Case 3**：
- 伙伴是空闲块头但 order 不同：不合并。

**Case 4**：
- 伙伴同 order：合并，块头取较小 pfn，order 加一，继续向上尝试。

**Case 5**：
- 合并到 `BUDDY_MAX_ORDER`：停止并插入。

**Requirements**：
- 释放请求按 `2^ceil(log2 n)` 处理；
- `buddy_nr_free` 只在入口按原始 order 增加一次；
- 释放最低/最高 pfn 附近的块时，不得越过 `[nbase, npage)` 误合并。

### buddy_nr_free_pages

**Pre-Condition**：
- 分配器已初始化。

**Post-Condition**：
- 返回 `buddy_nr_free`（当前空闲页总数）。

**Requirements**：
- 只读，不修改状态。

### buddy_check

**Pre-Condition**：
- 本提示词阶段无特殊要求。

**Post-Condition**：
- 保持空实现，可打印一行“测试由第二份提示词负责”的提示。

**Requirements**：
- 不得写测试、断言场景或 QEMU/GDB 命令。

### pmm.c 接入

**Pre-Condition**：
- `kern/mm/pmm.c` 已有 `best_fit_pmm.h` 与 `init_pmm_manager()`。

**Post-Condition**：
- 文件顶部包含 `buddy_pmm.h`；
- `init_pmm_manager()` 在 `USE_BUDDY_PMM` 定义时选择 `buddy_pmm_manager`，否则选择 `best_fit_pmm_manager`；
- 其余代码（包括 `page_init()`）保持不变。

**Requirements**：
- 只允许 include 与 manager 选择两处改动；不修改 `struct pmm_manager` 定义。

### 模块级不变量

- order 为 `o` 的块，起始 pfn 必须按 `2^o` 对齐，且整体在 `[nbase, npage)` 内；
- 一个空闲块只出现在一条链表上，只有块头挂链；
- 只有同 order 且空闲的伙伴才能合并；
- 伙伴 pfn 必须在 `[nbase, npage)` 内；
- `Σ(buddy_free_area[o].nr_free * 2^o) == buddy_nr_free`；
- 从链表摘除块时清 `PageProperty` 并把 `property` 置 0；
- 分配返回的块 `PageProperty == 0`；
- 释放后空闲区应能恢复到可重新分配的状态（测试由第二份提示词负责）。

---

## 附录 A：G1–G5 执行阶段与门禁

### G1：骨架与可编译

- 建立 `buddy_pmm.h` 与 `buddy_pmm.c`：定义状态、六个最小实现、manager 表；`buddy_check()` 打印未实现提示。
- 门禁：`make obj/kern/mm/buddy_pmm.o` 通过；`nm` 可见 `buddy_pmm_manager` 与六个接口；不改 `pmm.c`、`Makefile`、公共框架文件。

### G2：初始化与空闲链表

- 实现 `buddy_init_memmap()` 与初版 `buddy_free_block()`（只插入，不合并）。
- 门禁：单文件编译通过；静态检查确认初始化覆盖整个区间、每个块按 `2^order` 对齐、计数器不重复累加。

### G3：分配与拆分

- 实现 `buddy_order_for()` 与 `buddy_alloc_pages()`。
- 门禁：单文件编译通过；静态检查确认 `n=1/3/5/1024` 分别对应 order 0/2/3/10；`n` 超过 `2^BUDDY_MAX_ORDER` 返回 `NULL`。

### G4：释放与合并

- 实现 `buddy_free_pages()`，把 `buddy_free_block()` 扩展为“插入 + 合并”。
- 门禁：单文件编译通过；静态检查确认伙伴边界、同 order 判断、块头更新、计数增减完整。

### G5：接入现有 ucore 框架

- 按 [GUARANTEE] 的接入约束修改 `pmm.c`。
- 门禁：`make obj/kern/mm/buddy_pmm.o` 仍通过；`pmm.c` 补丁只包含 include 和 manager 选择；默认仍为 `best_fit_pmm_manager`。

### 阶段报告格式

```text
## 阶段 Gx

### 目标
一句话说明本阶段目标。

### 新增/修改文件
- 文件路径
- 新建或补丁片段

### 实现说明
简短说明关键数据结构与流程；不要贴测试。

### 编译/静态检查
- 执行的命令
- 实际结果
- 检查到的关键不变量

### 未决问题
- 缺少 page_init/best_fit 等依赖时只说明，不要补实现。

### 门禁结论
PASS / FAIL；FAIL 时只修当前阶段，不进入下一阶段。
```

---

## 附录 B：提交前检查

- 目标文件与目标模块写清楚了吗？
- 每处改动都在 `buddy_pmm.h/.c` 或 `pmm.c` 的两处最小接入内吗？
- 六个接口签名与 manager 表完全一致吗？
- `[RELY]` 中的宏、结构体、函数名与仓库真实代码一致吗？
- 允许/禁止修改的范围写清楚了吗？
- 每个函数的 Pre/Post/Case 是否覆盖了正常路径与异常路径？
- 关键不变量（对齐、同 order 合并、伙伴边界、计数语义）都覆盖了吗？
````

# Buddy System 测试提示词

> 与上一条配套：测试套件位于 `buddy_pmm.c` 的 `buddy_check()`，覆盖初始化、基本分配、拆分、释放/合并、耗尽与状态恢复。

````markdown
## [PROMPT]

**任务**：在 `kern/mm/buddy_pmm.c` 中实现测试套件，并把 `buddy_check()` 替换为测试入口。测试必须覆盖：

1. 初始化布局（块头、order、对齐、覆盖、计数）；
2. 基本分配（1/2/4/8 页）；
3. 拆分（低 order 出现拆出的后半块）；
4. 释放与合并（任意释放顺序、非 2 的幂、最低/最高 pfn 边界）；
5. 耗尽（分配到 NULL、稳定失败、全部释放后恢复）；
6. 测试前后空闲状态完全恢复。

测试函数放在 `kern/mm/buddy_pmm.c` 中，或放在能被它直接 include 的测试文件里；**不要新增 `kern/test` 目录，不要修改 `Makefile`**。

**操作要求**：

- 必须在项目中修改真实文件，而不是只输出代码片段。
- 只允许修改 `kern/mm/buddy_pmm.c`（必要时新增一个被它直接 include 的测试文件）；不得修改 `pmm.h`、`memlayout.h`、`list.h`、`best_fit_pmm.c`、`tools/*`、`Makefile`。
- 不得修改 Buddy 算法来让测试通过；不得删除、跳过或弱化失败测试；不得只打印 PASS 而不做检查。
- 本提示词不重新实现算法；如果测试发现算法问题，报告失败点与最小复现，不要静默修改算法。
- 测试必须确定、可重复，测试后恢复空闲状态；失败时保留完整日志，并给出最小复现。
- 最终默认 manager 仍是 `best_fit_pmm_manager`；不得把 `buddy_pmm_manager` 留作默认 manager。
- 若 QEMU/GDB 不可用，standalone 模式至少用 host 沙盘运行并说明限制；不能把“没运行”写成 PASS，也不能把 host 沙盘结果写成 QEMU/GDB 结果。

**输出要求**：直接修改文件；先按 [RELY] 判定 ucore/standalone 模式，再按 [SPECIFICATION] 和附录 S1–S6 执行并报告。

---

## [RELY]

### 测试入口与调用链

- `pmm_init() → check_alloc_page() → pmm_manager->check() → buddy_check()`。
- 只有定义 `USE_BUDDY_PMM` 时，真实内核才会调用 `buddy_check()`；默认 Best-Fit 构建不执行 buddy 测试，也不影响 `make grade`。

### 可访问的内部状态

- `buddy_free_area[0..BUDDY_MAX_ORDER]`、`buddy_nr_free`；
- `Page->PageProperty`、`Page->property`；
- `page2ppn(page)`、`nbase`、`npage`、`pages`；
- 测试与实现同文件时可直接访问这些静态符号；如果拆到单独文件，必须通过专用测试接口访问，不能复制一份内部状态。

### 可用的测试能力

- 链表遍历：`list_next`、`le2page`；
- 快照结构：保存 `buddy_nr_free` 与每个 order 的 `nr_free`；
- 小数组、排序、受限位图：用于检查重叠和漏页；不要为测试在正式代码中增加全局大数组；
- `assert`、`cprintf`；
- 测试辅助函数必须 `static`，不改变公开接口。

### 环境判定

**ucore 模式**（同时满足）：

- `kern/mm/pmm.c` 中 `page_init()` 已实现；
- `best_fit_pmm.c` 能编译，`best_fit_pmm_manager` 已定义；
- `make` 能完成完整内核构建；
- `pmm.c` 支持 `USE_BUDDY_PMM` 开关。

**standalone 模式**（任一满足）：

- `page_init()` 尚未实现；
- `best_fit_pmm.c` 还不能编译；
- `make` 不能构建完整内核；
- 无法安全启动到 `pmm_init()`。

### ucore 模式事实

- 构建：`make clean && make DEFS=-DUSE_BUDDY_PMM`
- 运行：`make qemu`
- 期望：`memory management: buddy_pmm_manager`、所有 `[BuddyTest] … PASS`、`check_alloc_page() succeeded!`
- 恢复默认：`make clean && make && make grade`，期望 `Page Init: PASS`、`Best-Fit: PASS`。

### standalone 模式事实

- 不修改 ucore 的其他文件；在仓库外搭 bare-metal 或 host 测试程序；
- 用 fake `pages[]` 模拟物理页描述符，参数参考：`nbase = 0x80000`、`npage = 0x88000`、`INIT_PFN = 0x80200`、`INIT_N = 0x7E00`；
- 直接编译仓库中的 `buddy_pmm.c`；
- 在测试 `main()` 中调用 `buddy_pmm_manager.init()`、`init_memmap()`，再调用 `buddy_pmm_manager.check()`；
- 优先使用 QEMU；GDB 可用时用 `-S -s` 启动，`target remote :1234` 连接；
- 本仓库的参考 harness：`buddy_standalone/`（`build.sh`、`run.sh`、`gdb.sh`；`REPO=` 可指向仓库 `code` 或 `lab2_new`）。

### 参考数值（fake 区间）

| 观察点 | 参考值 |
| --- | --- |
| `npage` / `nbase` | 557056 / 524288 |
| init 页数 / 空闲页 | 32256 / 32256 |
| order 9 / order 10 块数 | 1 / 31 |
| `alloc n=1` 后 free | 32255 |
| 首次 `free n=1` 后 free | 32242 |
| 最终 free | 32256 |
| 最终 order 9 / order 10 | 1 / 31 |
| 耗尽块数 | 32（1×order9 + 31×order10） |

> full ucore 的地址与页数可能不同；比较关系和恢复结果，不要硬编码地址或页数。

---

## [GUARANTEE]

### 必须提供的测试函数

函数名、参数、返回值固定如下：

```c
static void buddy_test_init_layout(void);
static void buddy_test_alloc_basic(void);
static void buddy_test_split(void);
static void buddy_test_free_merge(void);
static void buddy_test_exhaustion(void);
static void buddy_test_all(void);
static void buddy_check(void);
```

- `buddy_check()` 是 `pmm_manager.check` 的入口，只调用 `buddy_test_all()`；
- `buddy_test_all()` 依次调用上面五个测试；
- 每个测试失败时用 `assert` 触发 panic，并打印可定位的测试名；通过时打印 `[BuddyTest] … PASS`；
- `buddy_check()` 必须保持 `void (void)` 签名。

### 允许新增的内容

- `static` 测试辅助函数：快照、恢复断言、布局检查、平铺/覆盖检查、范围计算、order 取整 oracle；
- 固定的测试数组（块指针/order 记录），容量不足时 `assert`；
- 不得为测试增加全局大数组或修改 `Makefile`。

### 测试代码位置

- 测试必须位于 `kern/mm/buddy_pmm.c`，或能被它直接 include 的文件；
- 不得新增 `kern/test`，不得把测试目录写进 `Makefile`。

---

## [SPECIFICATION]

### 测试基础设施

- **快照**：记录当前 `buddy_nr_free` 与每个 order 的 `nr_free`。
- **恢复断言**：逐 order 比较块数、比较 `buddy_nr_free`，再做布局检查。
- **布局检查**：
  - 每个节点都是块头（`PageProperty` 置位），`property == order`；
  - 起始 pfn 按 `2^order` 对齐，块整体在 `[nbase, npage)`；
  - 每个 order 的链表实际长度等于 `nr_free`；
  - `Σ(nr_free * 2^order) == buddy_nr_free == buddy_nr_free_pages()`；
  - 按 pfn 排序后，块之间无重叠、无空洞地覆盖一段连续区间（仅对恢复态要求）。
- **分配块检查**：`PageProperty == 0`、`property == 0`、按目标 order 对齐、块内页均不是空闲块头。
- **范围辅助**：计算空闲区最低 pfn `lo` 与最高 end `hi`，用于边界测试。
- 所有辅助函数必须 `static`，不改变公开接口。

### buddy_test_init_layout

**Pre-Condition**：
- 分配器已 `init` + `init_memmap`，尚未进行任何分配。

**Post-Condition**：
- 测试本身不改变状态；快照与测试前一致。

**Case 1**：
- 正常区间：遍历所有 order 的链表，逐节点检查块头标记、order、对齐、范围、计数；检查页数总和与 `buddy_nr_free` 相等。

**Case 2**：
- 块起点或终点越过 `[nbase, npage)`、起始 pfn 未按 `2^order` 对齐：触发断言失败。

**Case 3**：
- 链表长度与 `nr_free` 不一致，或页数总和与 `buddy_nr_free` 不一致：触发断言失败。

**Requirements**：
- 必须检查“无重叠、无漏页”；可用小数组排序或受限位图，不要增加全局大数组；
- 测试前后快照一致。

### buddy_test_alloc_basic

**Pre-Condition**：
- 空闲页足够（至少 15 页）。

**Post-Condition**：
- 所有分配块释放后，快照完全恢复。

**Case 1**：
- 依次分配 1、2、4、8 页：返回非空、互不相同、按 `2^order` 对齐、`PageProperty == 0`；`buddy_nr_free` 精确减少 1+2+4+8=15。

**Case 2**：
- 按分配顺序或逆序释放：每个 order 的块数与 `buddy_nr_free` 恢复到快照。

**Case 3**：
- `alloc_pages(0)`、`alloc_pages(2^BUDDY_MAX_ORDER + 1)`：返回 `NULL`，且状态不变。

**Case 4**（建议补充）：
- `alloc_pages(5)` 实占 8 页（order 3）；`alloc_pages(1024)` 实占 1024 页（order 10），释放后均恢复。

**Requirements**：
- 只通过公开接口分配/释放；检查块之间不重叠；不得修改算法。

### buddy_test_split

**Pre-Condition**：
- 至少存在一个非空 order 的空闲块。

**Post-Condition**：
- 测试结束后快照恢复。

**Case 1**：
- 找到最小的非空 order `src`，记录该链表头块；分配 1 页后，返回块必须等于该块头。

**Case 2**：
- 分配后 `buddy_nr_free` 减少 1；`src` 的块数减一；每个低于 `src` 的 order 块数加一；每个拆出的后半块位于 `p + 2^k`，是对应 order 的块头。

**Case 3**：
- `src == 0`：不产生拆分，只验证块数减一。

**Case 4**：
- 释放该页后，快照完全恢复。

**Requirements**：
- 拆分观察必须来自真实链表状态，不得硬编码地址。

### buddy_test_free_merge

**Pre-Condition**：
- 快照已记录。

**Post-Condition**：
- 所有释放完成后快照恢复，空闲块平铺覆盖原区间。

**Case 1**：
- 分配 1、2、4、8 页，再按任意不同顺序释放：每个 order 的块数与 `buddy_nr_free` 恢复。

**Case 2**：
- `alloc_pages(3)` 实占 4 页（`buddy_nr_free` 减 4），`free_pages(p, 3)` 后完全恢复。

**Case 3**：
- 最低 pfn 边界：分配最低的两页，先释放高页再释放低页，验证不会与区间外的伙伴误合并。

**Case 4**：
- 最高 pfn 边界：从最大 order 向下取空全部页，确认存在 `pfn == lo` 的块和 `end == hi` 的块；再按分配顺序释放，快照恢复。

**Requirements**：
- 释放顺序可以不同；边界值必须来自实际布局，不得硬编码。

### buddy_test_exhaustion

**Pre-Condition**：
- 快照已记录。

**Post-Condition**：
- 全部释放后快照恢复，所有页重新被空闲块覆盖。

**Case 1**：
- 从最大 order 向下循环分配，直到返回 `NULL`；用固定数组记录每个块的指针与 order，容量不足时断言。

**Case 2**：
- 耗尽后 `buddy_nr_free == 0`，所有 order 链表为空；再次 `alloc_pages(1)` 与 `alloc_pages(1024)` 仍返回 `NULL`。

**Case 3**：
- 分配块数等于初始块数（大块优先时不发生拆分）。

**Case 4**：
- 按记录逆序释放全部块：快照恢复，布局平铺。

**Requirements**：
- 记录数组必须有固定容量并检查；不得使用 malloc。

### buddy_test_all 与 buddy_check

**Pre-Condition**：
- `buddy_check()` 由 `pmm_manager->check()` 调用。

**Post-Condition**：
- 五个测试全部 PASS；入口快照恢复。

**Case 1**：
- 依次调用 `buddy_test_init_layout()`、`buddy_test_alloc_basic()`、`buddy_test_split()`、`buddy_test_free_merge()`、`buddy_test_exhaustion()`。

**Case 2**：
- 最后打印 `[BuddyTest] all: PASS` 与 `[BuddyTest] ALL TESTS PASS, free pages = N`。

**Case 3**：
- `buddy_check()` 只调用 `buddy_test_all()`，保持 `void (void)` 签名。

**Requirements**：
- 不得把测试拆到 `kern/test`，不得修改 `Makefile`。

### 输出要求

每个测试至少输出：

```text
[BuddyTest] init layout: PASS
[BuddyTest] alloc basic: PASS
[BuddyTest] split: PASS
[BuddyTest] free/merge: PASS
[BuddyTest] exhaustion: PASS
[BuddyTest] all: PASS
[BuddyTest] ALL TESTS PASS, free pages = N
```

失败时打印测试名并 `assert`/panic；不得只打印 PASS 而不执行检查。

### ucore 模式执行

```sh
make clean
make DEFS=-DUSE_BUDDY_PMM
make qemu
```

期望串口输出：`memory management: buddy_pmm_manager`、所有 `[BuddyTest] … PASS`、`check_alloc_page() succeeded!`。

回归：

```sh
make clean && make
make grade
```

期望 `Page Init: PASS`、`Best-Fit: PASS`（默认 manager 仍为 `best_fit_pmm_manager`）。

### standalone 模式执行

- 在仓库外搭测试程序：fake `pages[]`、`nbase`、`npage`、最小 `pmm.h`/`stdio.h` 桩；
- 直接编译当前 `buddy_pmm.c`；
- QEMU 直接运行；`-S -s` 启动并 `target remote :1234`；
- 若 QEMU/GDB 不可用，至少用 host 沙盘运行并说明限制。

### GDB 验证

断点建议：

```text
main
buddy_init_memmap
buddy_alloc_pages
buddy_free_pages
qemu_test_done
```

观察点：

- `buddy_init_memmap` 入口：`n` 是传入页数，`base` 对应起始 pfn；
- `buddy_alloc_pages` 入口：`n` 是请求页数；`finish` 后检查返回块与 `buddy_nr_free`；
- `buddy_free_pages` 入口：`n` 是释放请求；`finish` 后检查计数；
- 最终断点：`buddy_nr_free` 与每个 order 块数恢复到初始快照；
- 调用栈应能看出 `main → buddy_check → buddy_alloc_pages / buddy_free_pages`（ucore 中为 `pmm_init → check_alloc_page → buddy_check`）。

---

## 附录 S1–S6：阶段与验收

### S1：环境判定与编译

- 检查 `page_init()`、`best_fit_pmm.c` 和完整 `make` 是否可用；选择 ucore 或 standalone 模式；
- 先只保证测试骨架能编译，不写测试逻辑。
- 验收：ucore 模式 `make DEFS=-DUSE_BUDDY_PMM` 能构建；standalone 模式测试 ELF/host 可执行文件能编译；模块编译无警告。

### S2：初始化测试

- 实现 `buddy_test_init_layout()`。
- 验收：块头/order/对齐/范围/计数/覆盖全部满足；测试前后快照一致。

### S3：分配与拆分测试

- 实现 `buddy_test_alloc_basic()` 与 `buddy_test_split()`。
- 验收：1/2/4/8 页分配成功、互不相同、对齐、`PageProperty == 0`；`nr_free` 按实际块大小减少；释放后恢复；拆分能观察到低 order 出现后半块；`n=0` 与超限返回 `NULL` 且状态不变。

### S4：释放、合并与非 2 的幂

- 实现 `buddy_test_free_merge()`。
- 验收：任意顺序释放后恢复；`alloc_pages(3)` 占 4 页，`free_pages(p, 3)` 后恢复；最低/最高 pfn 边界不误合并、不越界；测试后所有页重新被覆盖。

### S5：耗尽

- 实现 `buddy_test_exhaustion()`。
- 验收：持续分配到 `NULL`；耗尽后再次分配仍 `NULL`；按记录逆序释放全部块；`buddy_nr_free` 与每个 order 块数恢复；参考 fake 区间下耗尽 32 块、恢复 32256 页。

### S6：最终测试与 GDB 验证

- ucore 模式：`make clean`、`make DEFS=-DUSE_BUDDY_PMM`、`make qemu`；期望全部 PASS；再恢复默认 `make clean && make && make grade`。
- standalone 模式：编译测试 ELF/可执行文件，QEMU 运行；GDB 检查初始化、分配、释放和最终状态。
- 验收：`buddy_test_all()` 全部 PASS、无 panic；测试前后状态完全恢复；GDB 关键值与参考关系一致；最终默认 manager 是 `best_fit_pmm_manager`。

### 阶段报告格式

```text
## 阶段 Sx

### 目标
一句话说明本阶段目标。

### 环境/文件
- ucore 模式或 standalone 模式
- 新增/修改的测试文件
- 是否修改了 Buddy 算法（应为否）

### 测试/命令
- 执行的命令
- 测试函数
- 关键代码路径

### 结果
- 实际输出摘要
- GDB 观察值（如果本阶段使用 GDB）
- 失败日志（如果有）

### 验收结论
PASS / FAIL；FAIL 时停在当前阶段，不进入下一阶段。
```

---

## 附录：提交前检查

- 测试是否只改了 `buddy_pmm.c`（或它直接 include 的文件）？
- 是否明确判定并说明了 ucore / standalone 模式？
- 五个测试函数与 `buddy_check()` 的签名、调用顺序是否完全符合 [GUARANTEE]？
- 每个测试是否有快照、恢复断言和失败定位信息？
- 是否检查了块头标记、order、对齐、范围、计数、无重叠、无漏页？
- 是否避免了硬编码地址/页数（只比较关系与恢复结果）？
- 是否避免了为测试修改算法、删除测试、只打印 PASS？
- 最终默认 manager 是否仍是 `best_fit_pmm_manager`？
````

