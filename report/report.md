# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
| --- | --- |
| **实验名称** | Lab3：中断与异常处理 |
| **小组成员** | 2411326-张晨旭、2411566-罗晟皓、2413042-郭桦 |
| **完成日期** | 待填写 |

### 小组分工

| 成员 | 负责的任务 |
| --- | --- |
| 2411326-张晨旭 | Trap 入口与上下文恢复 |
| 2411566-罗晟皓 | 中断与时钟中断处理 |
| 2413042-郭桦 | 异常处理与最终集成 |

---

## 一、实验目的

1. 理解 RISC-V 以 Trap 统一处理中断与异常的机制，掌握 `scause` 中断标志位和原因码的含义，区分异步中断与同步异常的触发方式和返回要求。
2. 理解 Trap 上下文保存与恢复的必要性，建立通用寄存器、Trap CSR、内核栈与 `struct trapframe` 之间的对应关系。
3. 结合 `stvec`、`sstatus`、`sie` 和 `sret`，分析 S 模式内核的入口配置、中断使能、事件分发及执行现场恢复过程。
4. 实现中断分类和 S 模式时钟中断处理，通过重新设置时钟事件、累计计数、周期输出与关机策略，理解操作系统定时服务的基本机制。
5. 使用明确的函数规格和分层验证方法，检查处理函数的状态变化与调用顺序，并为完整内核的 Trap 往返和课程评分测试建立验证目标。

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

### 3.1 Trap 处理的整体流程

Trap 处理需要将事件响应与原程序的执行连续性结合起来。处理程序应能读取事件原因、完成相应服务，并在允许继续执行时恢复被打断的现场。整体处理流程可概括为：

```text
中断或异常发生
    → stvec 指向的 __alltraps
    → SAVE_ALL 保存上下文并建立 Trap frame
    → trap(tf)
    → trap_dispatch(tf)
        ├── interrupt_handler(tf)：异步中断
        └── exception_handler(tf)：同步异常
    → 返回汇编路径，RESTORE_ALL 恢复上下文
    → sret 返回
```

这条链路由硬件入口行为、汇编现场管理和 C 语言事件处理共同组成。初始化函数建立入口与中断条件，汇编负责连接 CPU 状态和 C 结构，处理函数则按原因执行服务。

### 3.2 内核初始化与中断条件

[kern_init()](../code/kern/init/init.c) 先清零 BSS，执行设备树、控制台和物理内存等初始化，再配置时钟并开放中断。与 Trap 相关的调用顺序为：

```text
idt_init()
pmm_init()
idt_init()
clock_init()
intr_enable()
进入内核等待循环
```

`idt_init()` 在物理内存初始化前后各调用一次，将 `sscratch` 置零，并将 `__alltraps` 地址写入 `stvec`，建立统一的 Trap 入口。该函数名保留了 IDT 表述，实际配置的是 RISC-V Trap 向量寄存器。

[clock_init()](../code/kern/driver/clock.c) 在 `sie` 中设置 `MIP_STIP`，安排第一次时钟事件，并将 `ticks` 初始化为零。[intr_enable()](../code/kern/driver/intr.c) 设置 `sstatus.SIE`，开放 S 模式中断的全局使能。时钟来源使能、全局使能和有效的后续时钟事件共同决定内核能否持续响应定时中断；事件进入 S 模式还依赖平台及固件提供的特权级路由。

### 3.3 中断分发与模块衔接

[trap.c](../code/kern/trap/trap.c) 中的 `trap()` 调用 `trap_dispatch()`。分发函数通过 `(intptr_t)tf->cause < 0` 判断原因值的最高位，将中断交给 `interrupt_handler()`，将同步异常交给异常处理模块。

中断处理函数读取入口提供的 Trap frame，完成原因分类及 S 模式时钟服务，并保持现场内容不变。入口、上下文恢复和异常处理的具体接口与实现由各自负责成员设计，在对应模块章节中补充。

---

## 四、实验内容与实现

### 4.1 Trap 入口与上下文恢复

**负责人：** 2411326-张晨旭

#### 4.1.1 模块功能描述

实现 [trapentry.S](../code/kern/trap/trapentry.S) 中的 `.macro RESTORE_ALL` 与 `__alltraps:` 的 C 调用部分，连接统一 Trap 入口、事件处理和特权返回。调用接口为：

```c
void trap(struct trapframe *tf);
```

#### 4.1.2 关键接口与设计

| 接口或数据 | 定义位置 | 设计作用 |
| --- | --- | --- |
| `SAVE_ALL` | `kern/trap/trapentry.S` | 保存 GPR 与 CSR，建立现场；保持已有实现不变 |
| `RESTORE_ALL` | `kern/trap/trapentry.S` | 从现场恢复返回 CSR 和 GPR，最后恢复 sp |
| `__alltraps`、`__trapret` | `kern/trap/trapentry.S` | 连接统一入口、C 调用和已有 sret 返回路径 |
| `struct trapframe` | `kern/trap/trap.h` | 固定汇编与 C 共用的现场布局 |
| `trap(struct trapframe *tf)` | `kern/trap/trap.h`、`trap.c` | 以 a0 传入现场地址，由 C 分发事件 |
| `LOAD`、`STORE`、`REGBYTES` | `libs/riscv.h` | 使用目标架构的访存指令及寄存器宽度 |


[trap.h](../code/kern/trap/trap.h) 的 `struct trapframe` 依次包含 `struct pushregs gpr`、`status`、`epc`、`badvaddr`、`cause`。pushregs 的字段严格按 x0～x31 排列。[riscv.h](../code/libs/riscv.h) 在 RV64 下展开 LOAD 为 ld、STORE 为 sd、REGBYTES 为 8，因而槽位 i 的字节偏移为 8i。

| 槽位 | 保存内容 | 字节偏移 | 恢复说明 |
| --- | --- | --- | --- |
| 0 | x0 / zero | 0 | 只读零寄存器，不需恢复 |
| 1 | x1 / ra | 8 | 被打断代码的函数返回地址 |
| 2 | x2 / sp | 16 | 进入 Trap 前的原栈指针，最后恢复 |
| 3～7 | gp、tp、t0～t2 | 24～56，步长 8 | 各自对应 x3～x7 |
| 8～9 | s0～s1 | 64～72，步长 8 | 写回返回 CSR 后再恢复原值 |
| 10～17 | a0～a7 | 80～136，步长 8 | 包括被用于传参的 a0 |
| 18～27 | s2～s11 | 144～216，步长 8 | 各自对应 x18～x27 |
| 28～31 | t3～t6 | 224～248，步长 8 | 各自对应 x28～x31 |
| 32 | sstatus / status | 256 | 返回控制状态 |
| 33 | sepc / epc | 264 | C 处理结束后的目标 PC |
| 34 | sbadaddr（stval）/ badvaddr | 272 | 事件相关地址或信息，用于诊断 |
| 35 | scause / cause | 280 | 完整原因值，用于分发与诊断 |

SAVE_ALL 先执行 `csrw sscratch, sp` 保存原 sp，再分配 `36 * REGBYTES`。保存其他 GPR 后，`csrrw s0, sscratch, x0` 取出原 sp 并清零 sscratch，将其写入槽位 2。s0～s4 被临时用于读取 CSR 前已经保存原值，因此不会污染现场。

SAVE_ALL 完成后，当前 sp 指向帧起点，槽位 2 则是原栈指针，二者不能混淆。总大小为 288 字节，且 288 % 16 = 0；只要 Trap 前 sp 已按 ABI 对齐，分配后仍满足 C 调用的 16 字节对齐。

#### 4.1.3 具体实现

**（1）RESTORE_ALL：返回状态与通用寄存器恢复。**

恢复首先从帧中取出返回控制状态：

```asm
LOAD s0, 32*REGBYTES(sp)
LOAD s1, 33*REGBYTES(sp)
csrw sstatus, s0
csrw sepc, s1
```

s0、s1 此时只作暂存，其原值仍在槽位 8、9 中，写 CSR 后再恢复，避免破坏已经恢复的寄存器。`sstatus` 决定后续特权返回的状态，`sepc` 决定返回目标；二者都使用 C 结束处理后的帧内容，尤其不能用入口时的旧 EPC 覆盖成员 C 的更新。

随后源码逐项执行 `LOAD x1, 1*REGBYTES(sp)`、`LOAD x3, 3*REGBYTES(sp)`，依次至 `LOAD x31, 31*REGBYTES(sp)`，最终为：

```asm
LOAD x2, 2*REGBYTES(sp)
```

共恢复 31 个可写 GPR。x0 恒为零，不需要装载；其余寄存器均与保存槽位一致，包括 gp、tp、参数、临时及被调用者保存寄存器。最后一条 LOAD 用尚未改变的 sp 计算地址，读取槽位 2 后才写入 x2；此后不再从帧中取数，避免提前恢复 sp 导致所有剩余偏移落到错误地址。

在帧未迁移、槽位未更新的原始场景中，`sp + 288` 与保存的原 sp 相等；仍选择读取槽位 2，是因为返回契约以帧内目标值为准，而不是重新推导地址。这样也保持了 sp 与其他 GPR 一致的现场恢复语义。

`badvaddr` 与 `cause` 是处理和诊断输入，不是 `sret` 的返回控制依据，无需在此写回硬件。sscratch 延续 SAVE_ALL 清零后的约定。原 ra 从槽位 1 恢复，用于原代码未来的普通函数返回；本次 Trap 返回位置由 sepc 和 sret 决定。

**（2）__alltraps：汇编入口与 C 调用衔接。**

```asm
__alltraps:
    SAVE_ALL
    mv a0, sp
    jal trap

__trapret:
    RESTORE_ALL
    sret
```

这是实际控制流的关键节选；宏定义及符号声明见源码。SAVE_ALL 在入口内展开，并非先调用宏再跳到 __alltraps。`mv a0, sp` 把帧地址放入 ABI 的首参数寄存器，C 可据此解释为 `struct trapframe *`。`jal trap` 改写 ra 为下一条指令地址，而原 ra 已在帧中，无需再次压栈。

C ABI 只要求保留部分寄存器，不能要求 trap 自动保留所有参数和临时寄存器。这里完整现场已在调用前保存，返回路径会恢复这些寄存器，因而 C 使用它们不会永久改变被打断的 GPR 状态。C 的栈向低地址增长，遵守 ABI 时恢复调用前 sp，帧基址仍有效；函数返回后自然执行紧邻的 __trapret，无需额外跳转或使用 a0 返回值。最后由 sret 读取恢复的 sepc，并按 SPP 选择返回 S 或 U。

#### 4.1.4 Spec 与 Prompt

实际保存的开发 Prompt 见 [入口与上下文恢复](prompt.md#入口与上下文恢复)，包含 `[PROMPT]`、`[RELY]`、`[GUARANTEE]`、`[SPECIFICATION]` 四段。以下是对该归档和已实现行为的规格摘要，供正文审阅；属于编辑整理。

```text
[PROMPT]
基于真实源码实现 RESTORE_ALL 和 __alltraps 的 trap(tf) 调用部分。
先核对保存布局，再做最小修改，最后验证并如实报告执行结果。
仅修改两处 lab3 function1 区域，保留 SAVE_ALL、__trapret 与公共接口，
不实现 interrupt_handler 或 exception_handler，不改测试，不提交或推送。

[RELY]
RV64：LOAD=ld、STORE=sd、REGBYTES=8。
trapframe 包含 x0～x31 及 status/epc/badvaddr/cause，大小 288 字节。
SAVE_ALL 完成后 sp 为帧基址，槽位 2 为原 sp，sscratch 已清零。
void trap(struct trapframe *tf) 按 RISC-V ABI 调用，可修改允许更新的现场字段。
现有 __trapret 执行 RESTORE_ALL 后执行 sret。

[GUARANTEE]
RESTORE_ALL 恢复返回 CSR 和全部可写 GPR，最后恢复原 sp。
__alltraps 以 a0=sp 传递现场地址并调用 trap，C 返回后保持帧基址，
顺接 __trapret；保留 C 对 tf->epc 的修改。

[SPECIFICATION]
Pre-Condition：Trap 已路由到 S 模式，stvec 指向入口；入口栈有效且对齐；
SAVE_ALL 建立的现场与 trap.h 一致；恢复入口处 sp 指向该现场，C 已处理完成。
Post-Condition：trap 收到同一现场地址；正常返回后，目标 sstatus/sepc
与现场一致，x1～x31 恢复到对应寄存器；sp 最后恢复，随后 sret 返回。
Case 1：异步中断不调整 EPC，返回被打断的执行位置。
Case 2：同步异常已由成员 C 调整 EPC，恢复现场中的新值。
Case 3：C 改写 ra、参数和临时寄存器，仍从现场恢复原执行上下文。
Case 4：恢复期间以 sp 访问现场，其他读取结束前不得改变基址。
Requirements：逐项匹配 SAVE_ALL 偏移，使用 LOAD/REGBYTES；跳过 x0；
临时寄存器用完后再恢复；保证调用栈对齐；不将 ra 或 C 返回值当作 EPC；
不在汇编中自行分类事件或调整 EPC；返回级别由恢复的 SPP 决定。
```

Prompt 的设计先固定“内存布局—调用接口—允许修改范围”，再用前后置条件约束帧基址和返回状态，使 AI 不能通过改结构体或补齐他人模块来绕过接口问题。正常返回、EPC 修改和 C 寄存器变化三个 Case 分别检查事件透明性、跨模块更新和调用者保存寄存器的保护。静态布局、汇编、反汇编与联合测试分层执行，避免把语法正确当作处理器状态恢复正确。

#### 4.1.5 实现及验证过程

实现先核对保存布局和调用接口，再补全两处汇编填空，随后进行静态检查、独立汇编和反汇编。具体命令与输出见 5.4 节，本节概括实际过程：

| 阶段 | 实现或检查内容 | 实际结果及限制 |
| --- | --- | --- |
| 接口梳理 | 对照 SAVE_ALL、trapframe 与 RISC-V ABI，确定槽位、帧基址和参数传递方式 | 明确 36 个槽位、288 字节帧及 sp 最后恢复的约束 |
| 汇编实现 | 先恢复 sstatus/sepc，再恢复 GPR；入口以 a0=sp 调用 trap | 保持既有布局与返回逻辑，不扩展到成员 B/C 的处理函数 |
| 独立验证 | C 字段偏移断言、31 个可写 GPR 恢复顺序检查、独立汇编、objdump 与差异检查 | 均有通过记录；未发现需要重写成员 A 实现的可复现功能错误 |
| 环境修正 | 手动选择 GCC 15 时旧 CSR 别名不兼容；按 .bashrc → env.sh 加载后使用 GCC 10.2.0 | 强制重新汇编、反汇编与静态核对再次通过；属于版本选择问题 |
| 历史构建与后续集成 | 11 月 4 日完整构建因 exception_handler 缺失而阻塞；当前源码已补齐 | 不将旧依赖错误视为当前缺陷；后续小组集成记录见第五章 |

临时静态检查脚本曾因文本切片比较写错触发 AssertionError，修正检查后通过，没有因此修改内核实现。该问题与工具链兼容性、其他模块尚未完成均应区分，不能虚构为多轮汇编 Bug 修复。

成员 A 的独立记录没有执行 make grade、QEMU 动态内核运行或 GDB 寄存器观察。独立验证通过的结论限定于布局、编译及指令序列；小组后续动态结果及其证据范围按第五章分别记录。

### 4.2 中断与时钟中断处理

**负责人：** 2411566-罗晟皓

#### 4.2.1 模块功能描述

中断处理函数位于 `trap.c` 的 `lab3 function2 YOUR CODE` 标记下，接口为：

```c
void interrupt_handler(struct trapframe *tf);
```

函数从保存的现场读取中断原因，对已知中断分类，并承担 S 模式时钟事件的后续设置、计数、统计与关机判断。CPU 现场的保存和恢复由汇编入口及返回路径负责，函数本身不改写传入的 Trap frame。

#### 4.2.2 关键接口与设计

| 接口或数据 | 定义位置 | 用途 |
| --- | --- | --- |
| `struct trapframe`、`tf->cause` | `kern/trap/trap.h` | 读取完整的事件原因 |
| `IRQ_*` | `libs/riscv.h` | 匹配项目定义的中断原因码 |
| `clock_set_next_event()` | `kern/driver/clock.h`、`clock.c` | 安排下一次时钟事件 |
| `volatile size_t ticks` | `clock.c` 定义，`clock.h` 声明 | 累计处理的 S 模式时钟中断次数 |
| `TICK_NUM`、`print_ticks()` | `kern/trap/trap.c` | 设置 100 次一组的统计周期并输出 |
| `print_trapframe(tf)` | `kern/trap/trap.c` | 提供未知原因的现场信息 |
| `sbi_shutdown()` | `libs/sbi.h`、`sbi.c` | 发起关机请求 |
| `SHUTDOWN_AFTER` | `kern/trap/trap.c` | 定义累计三次统计输出后的终止策略 |

处理流程依次完成原因码提取、分支分类和相应服务。S 模式时钟分支按“设置下一事件—递增计数—检查统计周期—判断关机条件”的顺序执行；其他分支完成分类提示或现场诊断。Trap frame 作为只读输入，时钟状态由已有 `ticks` 维护。

#### 4.2.3 具体实现

**（1）中断标志位与原因码提取。**

RV64 的 `scause` 最高位表示事件是否为中断，其余 63 位表示原因码。`trap_dispatch()` 利用完整值判断事件类别，而 `interrupt_handler()` 通过局部变量去除标志位：

```c
uintptr_t cause = (tf->cause << 1) >> 1;
```

`tf->cause` 是无符号类型，左移一位将最高位移出，随后逻辑右移一位，在最高位补零，其余原因位保持原值。因此，带标志的 S 模式时钟中断能与 `IRQ_S_TIMER=5` 匹配，同时保留较高位的未知原因信息。

原 `tf->cause` 没有被改写，现场诊断仍能读取完整事件值。课程测试还会直接用不带中断标志的原因码 15 调用本函数，该提取方式也能正确进入未知中断路径。

**（2）中断分类与未知原因处理。**

`switch` 覆盖 `riscv.h` 中已定义的中断类型。

| 类型 | 常量与原因码 | 处理方式 |
| --- | --- | --- |
| 软件中断 | `IRQ_U_SOFT=0`、`IRQ_S_SOFT=1`、`IRQ_H_SOFT=2`、`IRQ_M_SOFT=3` | 输出对应 User、Supervisor、Hypervisor、Machine 软件中断提示 |
| 时钟中断 | `IRQ_U_TIMER=4`、`IRQ_S_TIMER=5`、`IRQ_H_TIMER=6`、`IRQ_M_TIMER=7` | S 模式执行定时服务，其余模式输出对应提示 |
| 外部中断 | `IRQ_U_EXT=8`、`IRQ_S_EXT=9`、`IRQ_H_EXT=10`、`IRQ_M_EXT=11` | 输出对应模式的外部中断提示 |
| 其他已知中断 | `IRQ_COP=12`、`IRQ_HOST=13` | 输出 `Coprocessor interrupt` 或 `Host interrupt` |
| 未知原因 | 例如原因码 15 | 输出原因码，再打印 Trap frame |

S 模式软件中断的提示严格使用 `Supervisor software interrupt`。非时钟分支不增加 `ticks`。这些分支完成原因分类和诊断，设备中断源的确认、清除及具体设备服务不在当前函数的实现范围内。

未知中断分支使用以下调用：

```c
cprintf("Unknown interrupt: cause %llu\n",
        (unsigned long long)cause);
print_trapframe(tf);
```

根据 [defs.h](../code/libs/defs.h)，`uint64_t` 定义为 `unsigned long long`，`uintptr_t` 又是 `uint64_t` 的别名。因此，`cause` 的原有类型已经与 `%llu` 匹配，显式转换属于强调调用处类型约定的规范性调整。原因输出之后传入原 `tf`，便于将诊断信息与原始现场对应。

**（3）时钟事件重新设置与计数。**

S 模式时钟中断每次先调用 `clock_set_next_event()`，再使 `ticks` 增加一次。`clock_init()` 仅安排首个事件，后续每一次处理都要重新设置下一事件，才能形成持续的定时服务。

`clock.c` 使用 `sbi_set_timer(get_cycles() + timebase)` 安排下一事件。RV64 路径的 `get_cycles()` 通过 `rdtime` 读取时间计数器，`timebase` 当前为 100000；传给 SBI 的是当前时间加间隔得到的下一次绝对时刻。该常量以时间计数器单位计，实际中断周期取决于平台频率，实验没有测量其实际频率。

先重新设置事件，可以在后续计数与输出之前安排下一次触发。`ticks` 由时钟模块定义并在初始化时清零，处理函数每次只递增一次。`volatile` 使等待代码能够重新读取变量，但它不提供多核原子计数或并发同步保证。

**（4）周期输出与关机策略。**

时钟处理的关键代码如下：

```c
case IRQ_S_TIMER:
    clock_set_next_event();
    ticks++;
    if (ticks % TICK_NUM == 0) {
        print_ticks();
        if (ticks / TICK_NUM >= SHUTDOWN_AFTER) {
            sbi_shutdown();
        }
    }
    break;
```

`TICK_NUM=100` 定义统计周期。`print_ticks()` 固定输出 `100 ticks`，每次输出表示完成一组 100 次中断，文本本身不表示累计 ticks 值。

`SHUTDOWN_AFTER=3` 是实验实现采用的关机策略。在 `ticks` 从零开始、按次递增的条件下，输出时的 `ticks / TICK_NUM` 恰好是累计输出次数。因此可以直接利用已有状态判断阈值，无需新增输出计数变量。

| 累计 ticks | 本次输出 | 累计输出次数 | 关机接口调用 |
| --- | --- | --- | --- |
| 1～99 | 无 | 0 | 不调用 |
| 100 | `100 ticks` | 1 | 不调用 |
| 200 | `100 ticks` | 2 | 不调用 |
| 300 | `100 ticks` | 3 | 第三次输出之后调用 |

`print_ticks()` 位于关机判断之前，保证第三条统计信息先输出，再通过 `sbi_shutdown()` 请求固件结束运行。`sbi.c` 已提供对应的 SBI 调用封装。

持续运行到 300 的正常路径要求未定义 `DEBUG_GRADE`：已有 `print_ticks()` 在该宏下会调用 `panic()`。课程评分脚本使用 `GRADE_LAB3_*` 宏，与此调试宏不同。

#### 4.2.4 Spec 与 Prompt

**前置条件：** `tf` 指向有效 Trap frame，`tf->cause` 保存待处理事件原因。真实入口已经保存必要上下文；时钟模块已经初始化 `ticks`，时钟与 SBI 接口可用。统计次数推导以 `ticks` 初值为零且按时钟事件递增为约定。

**后置条件：** S 模式时钟中断每处理一次，设置一次下一事件并使 `ticks` 增加一次；每达到 100 的整数倍调用一次 `print_ticks()`；累计第三次输出后调用 `sbi_shutdown()`。其他已知中断产生对应提示，未知原因产生原因码和现场诊断。函数不修改传入现场，正常返回后继续使用既有 Trap 返回流程。

| Case | 行为要求 |
| --- | --- |
| Case 1：S 模式时钟中断 | 识别 `IRQ_S_TIMER`，持续重新设置事件并准确计数；100、200 时正常输出 |
| Case 2：S 模式软件中断 | 识别 `IRQ_S_SOFT`，输出 `Supervisor software interrupt` |
| Case 3：其他已知中断 | 按 `riscv.h` 定义分类，输出对应提示 |
| Case 4：未知中断 | 输出移除标志后的原因码，并调用 `print_trapframe(tf)` |
| Case 5：关机条件 | 采用 `SHUTDOWN_AFTER=3`，在 300 ticks 的第三次输出后请求关机 |

实现约束包括：正确处理最高位和完整原因码；不随意修改 `tf->epc`、通用寄存器或保存的状态；保留 lab3 标记；不覆盖异常处理区域；保持 `trap_dispatch()`、`trapentry.S`、`clock.c`、`init.c` 和评分脚本不变。这样可以单独验证中断处理的职责，同时保持入口、分发与返回接口一致。

首次实现使用的四段式原始 Prompt 见 [中断与时钟中断功能实现提示词](prompt.md#中断与时钟中断功能实现提示词)。

#### 4.2.5 实现及验证过程

实现包括核心处理路径建立和终止策略补充两个阶段。

| 阶段 | 实现要点 | 验证重点 |
| --- | --- | --- |
| 核心功能实现 | 完成原因码提取、已知中断分类、未知原因诊断、时钟重设、计数及每 100 次输出；项目当时没有定义关机阈值，先保留核心路径 | 中断分类、持续计数、时钟重设与现场保持 |
| 关机策略与调用规范补充 | 加入 `sbi.h`、`SHUTDOWN_AFTER=3` 和打印后的关机判断，并为格式化参数增加显式转换 | 直接由 `ticks` 推导输出次数，检查 300 ticks 时先输出再调用关机接口 |

核心版本和补充策略后的版本均通过独立 RISC-V 交叉编译与模拟接口验证。显式转换不改变当前类型定义下的参数类型或数值；它使格式化调用的类型约定直接可见。具体验证方法、结果及集成状态见第五章。

### 4.3 异常处理

**负责人：** 2413042-郭桦

#### 4.3.1 模块功能描述

异常处理由 [trap.c](../code/kern/trap/trap.c) 中的 `exception_handler()` 完成，接口为：

```c
void exception_handler(struct trapframe *tf);
```

`trap_dispatch()` 通过 `(intptr_t)tf->cause < 0` 把同步异常交给该函数。函数区分实验要求的两类异常——断点（`CAUSE_BREAKPOINT`）与非法指令（`CAUSE_ILLEGAL_INSTRUCTION`）——输出规定提示并把 `tf->epc` 调整为触发指令之后的位置，使 `sret` 返回后不再重复执行同一条异常指令；其余异常只打印 Trap frame 供调试，不改变执行位置。

#### 4.3.2 关键接口与设计

| 接口或数据 | 定义位置 | 用途 |
| --- | --- | --- |
| `struct trapframe`、`tf->cause`、`tf->epc` | [trap.h](../code/kern/trap/trap.h) | 读取异常原因码与 Trap 返回位置 |
| `CAUSE_ILLEGAL_INSTRUCTION = 0x2`、`CAUSE_BREAKPOINT = 0x3` | [riscv.h](../code/libs/riscv.h) | 异常原因码匹配 |
| `print_trapframe(tf)` | [trap.c](../code/kern/trap/trap.c) | 未知异常的现场输出 |
| `cprintf()`（`%lx`） | [printfmt.c](../code/libs/printfmt.c) | 输出 `epc` 的完整 64 位地址 |
| `insn_length()` | trap.c（新增，文件内 `static`） | 读取 `epc` 处指令，判断 16/32 位 |

`exception_handler()` 定义在 `interrupt_handler()` 之后、`trap_dispatch()` 之前。`trap.h` 没有该函数的原型，把定义放在调用点之前即可消除隐式声明，无需修改公共头文件。

#### 4.3.3 具体实现

**（1）异常分类与返回位置更新。**

断点和非法指令都是"执行到该指令即触发"的同步异常，`sepc` 指向触发指令本身。如果不调整 `sepc`，`sret` 会回到同一条指令并再次触发异常，形成死循环。因此两个分支分别在输出提示后执行 `tf->epc += 指令长度`，让返回位置落到下一条指令。

**（2）按指令实际长度前进。**

RISC-V 同时存在 16 位压缩指令（RVC）和 32 位标准指令，不能一律 `+4`。实现用一个读取半字的辅助函数判断：

```c
/* Length of the instruction at epc: 2 bytes for RVC, 4 bytes otherwise. */
static size_t insn_length(uintptr_t epc) {
    uint16_t insn = *(volatile uint16_t *)epc;
    return (insn & 0x3) == 0x3 ? 4 : 2;
}
```

RISC-V 规定 32 位指令的低两位为 `0b11`，16 位压缩指令的低两位不是 `0b11`，因此读取 `epc` 处 2 字节即可确定长度。本实验内核代码位于高地址 `0xffffffffc0...`，已由启动页表映射，S 模式读取该地址是安全的。

**（3）未知异常兜底。**

默认分支调用 `print_trapframe(tf)`，输出通用寄存器、`status`、`epc`、`badvaddr`、`cause`。默认分支**不读取** `epc`：课程测试会用 `memset` 清零的 trapframe（`epc = 0`）直接调用本函数，若在默认分支解引用 `epc` 会触发 load page fault 并再次进入 Trap。

最终实现如下：

```c
/* Length of the instruction at epc: 2 bytes for RVC, 4 bytes otherwise. */
static size_t insn_length(uintptr_t epc) {
    uint16_t insn = *(volatile uint16_t *)epc;
    return (insn & 0x3) == 0x3 ? 4 : 2;
}

//lab3 function2 2413042
void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
        case CAUSE_BREAKPOINT:
            cprintf("Breakpoint at 0x%lx\n", tf->epc);
            tf->epc += insn_length(tf->epc);
            break;
        case CAUSE_ILLEGAL_INSTRUCTION:
            cprintf("Illegal instruction at 0x%lx\n", tf->epc);
            tf->epc += insn_length(tf->epc);
            break;
        default:
            print_trapframe(tf);
            break;
    }
}
```

#### 4.3.4 Spec 与 Prompt

**Pre-Condition：** `tf` 指向有效的 Trap frame，`tf->cause` 保存异常原因码，`tf->epc` 为触发异常的指令地址；`stvec` 已指向 `__alltraps`，`SAVE_ALL` 已完成现场保存；内核代码段已映射，可读。

**Post-Condition：** 断点与非法指令处理后，`tf->epc` 指向触发指令之后，`sret` 返回时继续执行后续指令；其他异常输出完整 Trap frame 且不改变 `tf->epc`。函数不修改通用寄存器、`status`、`badvaddr` 与 `cause`，不触发第二次 Trap。

| Case | 行为要求 |
| --- | --- |
| Case 1：断点异常 | 输出 `Breakpoint at 0x<epc>`，`epc` 按实际指令长度前进后返回原执行流 |
| Case 2：非法指令异常 | 输出 `Illegal instruction at 0x<epc>`，`epc` 按实际指令长度前进 |
| Case 3：16 位压缩指令 | 低两位不是 `0b11`，`epc += 2` |
| Case 4：其他异常 | 调用 `print_trapframe(tf)`，不改变、不读取 `epc` |
| Case 5：清零 trapframe 直接调用 | `cause = 20`、`epc = 0` 时只打印现场，不产生二次异常 |

实现约束：不修改 `trap_dispatch()`、`trap()`、`trapentry.S`、`interrupt_handler()` 与评分脚本；保留 `lab3` 标记；输出前缀必须包含 `Breakpoint at` 与 `Illegal instruction at`，这是评分脚本的检查项。

本次使用的分层 Prompt 见 [异常处理功能实现提示词](prompt.md#异常处理功能实现提示词)。

#### 4.3.5 实现及验证过程

| 阶段 | 内容 | 验证重点 |
| --- | --- | --- |
| 接口梳理 | 阅读 `trap.h`、`trapentry.S`、`trap_dispatch()` 与 `tools/test_lab3*`，确认插入位置与测试期望 | 原因码、`epc` 语义、默认分支的调用方式 |
| 函数实现 | 新增 `insn_length()` 与 `exception_handler()` | 交叉编译通过，无隐式声明 |
| 集成验证 | `make`、`make qemu`、`make grade` | 三个用例得分与串口关键输出 |

实际遇到的问题与处理：

1. **定义位置**：`trap_dispatch()` 先调用后定义，`trap.h` 也没有原型，`-Werror` 下直接编译失败。把 `exception_handler()` 定义在 `trap_dispatch()` 之前解决，未改动公共头文件。
2. **默认分支读取 `epc` 会二次异常**：评分脚本用清零的 trapframe 直接调用 `exception_handler()`（`cause = 20`、`epc = 0`）。因此只在断点/非法指令分支读取 `epc`，默认分支仅打印现场。
3. **本地工具链与 QEMU 版本差异**：本机 binutils 2.45 不再识别旧 CSR 别名 `sbadaddr`，QEMU 10.2.1 也与仓库默认的 `-device loader` 启动方式不兼容。这两点通过只在本机生效的构建/启动适配解决，不进入提交代码，也不影响功能实现；详见 5.1、5.3。

集成结果：`make grade` 三个用例全部通过，合计 **100/100**（Test 1 35/35、Test 2 25/25、Test 3 40/40），具体输出见 5.2。

---

## 五、测试与验证

### 5.1 已执行的验证方法

中断模块采用独立目标编译和宿主模拟接口测试两种验证方式。

独立 RISC-V 交叉编译提取实际 `interrupt_handler()`、`print_ticks()` 和必要常量，使用项目头文件及 `-mcmodel=medany`、`-std=gnu99`、`-Wall`、`-Werror`、`-fno-builtin`、`-nostdinc` 等相关参数生成对象文件。该检查覆盖函数与接口声明的编译兼容性，不包含完整内核链接。

模拟接口测试由宿主 GCC 编译运行，直接调用处理函数。时钟替身记录重新设置的次数和时机，输出替身捕获文字，现场打印替身检查传入指针，关机替身检查触发时点。每次处理前后逐字节比较 Trap frame，以检查 EPC 和其他保存字段是否改变。该方法验证函数行为，不产生真实硬件中断，也不执行 `sret` 或固件关机。

集成验证使用课程评分流程：`make` 完成全量构建；`make qemu` 进行普通启动；`make grade` 通过 Makefile 把源码复制到 `/tmp/lab3_grade`，用 `tools/inject.py` 注入 `kern/test` 与三个测试调用，再由 `tools/grade.sh` 分别以 `GRADE_LAB3_CONTEXT`、`GRADE_LAB3_ILLEGAL`、`GRADE_LAB3_TIMER` 构建并运行 QEMU，按串口输出中的 sentinel 与关键字记分。

本机 QEMU（10.2.1）比课程环境新：`-device loader` 不再设置 OpenSBI 的下一跳地址，且写入 `satp` 后会重新翻译下一条取指；本机 binutils 2.45 也移除了旧 CSR 别名 `sbadaddr`。本地验证时使用仅在本机生效的构建/启动适配（`-kernel` 启动方式与 `sbadaddr` 别名映射），不包含在提交代码中；评分所用的测试代码、Makefile 与评分脚本均未修改。

### 5.2 已执行的测试结果

| 测试项目 | 执行结果 | 验证内容 |
| --- | --- | --- |
| 独立 RISC-V 交叉编译 | 通过 | 核心实现及补充关机逻辑后的实现均编译成功 |
| 已知中断分类 | 通过 | 覆盖头文件定义的各类分支，检查提示及非时钟分支的计数保持 |
| 未知原因及标志位 | 通过 | 检查带或不带中断标志的原因码、原因 15 的诊断及较高位原因信息的保留 |
| 100 ticks | 通过 | 累计一次 `100 ticks` 输出，未调用关机接口 |
| 200 ticks | 通过 | 累计两次统计输出，继续计数，未提前关机 |
| 持续时钟重设 | 通过 | 最终版本连续 300 次模拟调用均重新设置时钟，且重设先于计数增加 |
| 300 ticks 后关机调用 | 通过 | 第三次统计输出完成后调用一次关机替身接口 |
| Trap frame 保持不变 | 通过 | 调用前后现场逐字节一致，包含 EPC 与原始原因值 |
| 未知原因格式化 | 通过 | 原因 15 及较大的 64 位原因值均正确输出 |
| 完整构建 `make` | 通过 | 全部目标编译、链接成功 |
| 普通启动 `make qemu` | 通过 | 输出 `os is loading`、`++ setup timer interrupts`，3 次 `100 ticks` 后关机 |
| Test 1：Trap 入口、寄存器恢复与断点 EPC | 通过（35/35） | `Breakpoint at 0xffffffffc0201c78`，随后 `[Test 1] Trap entry and breakpoint return: PASS` |
| Test 2：非法指令 EPC 前进与兜底 | 通过（25/25） | `Illegal instruction at 0xffffffffc0201c28`、`Supervisor software interrupt`、`Unknown interrupt: cause 15`、`cause    0x00000014`，随后 `[Test 2] Illegal instruction return: PASS` |
| Test 3：时钟重设与持续 Trap 返回 | 通过（40/40） | `100 ticks`、`[Test 3] Sustained supervisor timer: PASS (40/40, ticks=200)` |
| `make grade` 合计 | 通过（100/100） | 三个用例全部 PASS |

关机替身在被调用时检查 `ticks=300`、重设次数为 300、累计统计输出为三次，并核对输出已完成。因此，第三次统计与关机请求的先后关系具有函数级验证结果；完整 QEMU 运行中第三次 `100 ticks` 输出后内核确实退出，与函数级结果一致。

独立验证的原始执行日志、复现材料及截图归档：待填写。

### 5.3 完整构建与集成状态

补齐 `exception_handler()` 后，`trap_dispatch()` 处的隐式声明错误消除，全量构建通过。`make`、`make qemu`、`make grade` 均已在集成后的代码上执行：

- `make`：全部目标编译、链接成功；
- `make qemu`：内核正常启动，打印 `(THU.CST) os is loading ...` 与 `++ setup timer interrupts`，随后每 100 次时钟中断输出一行 `100 ticks`，第三次输出后经由 `sbi_shutdown()` 结束；
- `make grade`：三个用例分别得到 35/25/40，合计 **100/100**。

评分流程把源码复制到临时目录后再注入测试，不修改工作区源码。本机 QEMU 版本较新，启动适配只在本地生效；提交代码中的入口、处理函数、测试注入与评分脚本均保持课程要求的形式。

集成结果：Test 1 的断点返回与寄存器恢复通过；Test 2 的非法指令返回、软中断提示、未知中断提示与未知异常现场输出通过；Test 3 的持续时钟中断到 `ticks=200` 通过。

---

## 六、实验总结与收获

### 6.1 中断、异常与执行现场

中断和异常共享 Trap 入口与现场结构，但触发来源和返回语义不同。`trap_dispatch()` 利用原因值的最高位区分两类事件；时钟中断响应异步时间事件，保留原返回位置，异常处理则需要解释触发指令并确定恢复策略。这使事件分类与执行连续性形成直接联系。

Trap frame 是 CPU 执行状态的软件表示，也是汇编与 C 之间的接口。保存区中的寄存器顺序、CSR 内容和原栈指针必须相互一致，处理函数才能正确读取原因，返回路径才能恢复同一执行现场。现场内存保持不变和 CPU 寄存器实际恢复是两个验证层次，需要分别检查。


### 6.2 特权级、中断使能与定时服务

S 模式内核通过 `stvec` 配置入口，通过 `sie` 和 `sstatus.SIE` 控制事件来源与全局使能，再由返回路径恢复状态并执行 `sret`。时钟和关机功能通过 SBI 与固件服务衔接，体现了内核与更高特权级服务之间的分工。

时钟初始化只提供首个事件，处理函数中的重新设置才使定时服务持续运行。`ticks` 将离散事件转换为可统计的周期，能够为后续时间片、超时和唤醒机制提供基础。当前统计与关机属于实验采用的观测策略，实际进程调度仍需要任务状态、调度队列和切换机制。

### 6.3 AI 辅助开发经验

AI 辅助实现以实际接口、课程测试和允许修改范围构造规格，再检查生成代码是否满足原因提取、持续重设、计数和现场保护等要求。按 Case 描述行为，使已知分类、未知原因和时钟路径都有明确检查目标。

初版未取得关机阈值时先完成核心功能，明确策略后再加入三次统计输出后的关机逻辑。通过既有 `ticks` 推导输出次数，避免维护额外状态；显式类型转换则使格式化接口约定更直观。关机测试同时检查 `ticks`、输出次数和调用顺序，覆盖先输出再关机的要求。

完整构建遇到其他模块缺失时，采用独立编译与接口替身验证负责的函数，并保留集成测试的检查目标。这种分层方法有助于定位模块问题，但最终仍需跨越真实入口、处理和返回路径，才能评价整体正确性。

### 6.4 实验范围与进一步学习

本实验建立了中断分类、S 模式时钟统计和终止策略的函数实现，并围绕 Trap 上下文分析了入口与返回的协作关系。完整设备中断驱动中的中断控制器配置、来源确认与清除，中断嵌套和优先级、多核同步、独立中断栈，以及时钟驱动的实际任务调度等机制，仍需在后续学习中展开。
