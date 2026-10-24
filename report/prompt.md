# Lab2 AI 协作 Prompt 记录

## 2026-10-15：仓库审计与官方初始代码导入

任务类型：环境及仓库管理，不是 Lab2 算法实现。

### 初始化工单摘要

- 审计小组仓库 `https://github.com/Zhang-Chenxu01/NKU-OS-2026fall` 的本地工作区、远程分支及提交历史，保护其他成员已有工作。
- 确认没有既有 Lab2 工作后，优先基于 `origin/lab1` 创建 `lab2`。
- 将 `D:\操作系统\实验\lab2\lab2_new` 的官方初始源码完整导入 `code/`，保持文件内容、换行和原始注释，逐文件核对相对路径及 SHA-256。
- 准备 `report/report.md`、`report/prompt.md`、`report/images/.gitkeep`，不编造实验结果或截图。
- 检查提交范围并再次核对远程状态后，提交 `lab2: import official starter code`，安全推送到 `origin/lab2`。
- 不实现 `page_init()`、Best-Fit、Buddy System 或其他 `YOUR CODE` 部分，不改动 Lab1 分支，不强制推送或重写历史。

### 后续用户指令（原文）

> 我感觉应该是开发环境一个在Windows一个在WSL的感觉，你先检查一下我们的Github仓库，看看lab2有没有已经被开发过的痕迹了，还是完全没有，如果完全没有的话，就把我们D盘那个代码弄进我们的相应区域呗，我准备进WSL环境里面去搞

### 本轮处理说明

- 初次审计发现原 Lab1 工作区有未跟踪的 `code/bin/`、`code/obj/`，依照初始化工单停止修改。
- 收到后续指令后，重新检查 GitHub 分支、提交与 Pull Request；可见记录中没有 Lab2 工作。
- 使用同一 Git 仓库的独立 worktree 准备 Lab2，保留原 Lab1 工作区及全部未跟踪编译产物。
- 本轮仅进行官方初始源码导入与仓库管理，不包含实验功能实现。
- 修复 Windows 创建的 worktree 在 WSL 中无法识别绝对路径的问题：仅将该工作区的 Git 入口改为相对路径，并锁定其管理记录以保护旧版 WSL Git 无法识别的 Windows 回链；源码未因此修改。

## 后续协作记录

由各成员继续追加实际使用的 Prompt，并注明对应任务和日期。



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



