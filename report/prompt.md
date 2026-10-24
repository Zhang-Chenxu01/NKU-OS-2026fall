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
