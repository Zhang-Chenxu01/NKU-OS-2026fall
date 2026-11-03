# 入口与上下文恢复

`````text

[PROMPT]

**任务**：

你是一名熟悉 RISC-V RV64 特权级架构、汇编语言和 uCore 操作系统的开发者。请根据当前项目的真实源码，实现 `kern/trap/trapentry.S` 中的两个待完成部分：

1. `RESTORE_ALL`：从 Trap frame 恢复处理器上下文。
2. `__alltraps`：建立汇编 Trap 入口与 C 语言 `trap()` 函数之间的调用关系。

实现完整的：

`Trap 发生 → SAVE_ALL → __alltraps → trap(tf) → __trapret → RESTORE_ALL → sret`

**操作要求**：

1. 必须检查当前工作区的真实代码。
2. 首先阅读 `lab3.md`、`kern/trap/trapentry.S`、`kern/trap/trap.h`、`kern/trap/trap.c`、`libs/riscv.h`，并检查相关的测试文件及 Makefile。
3. 只修改 `kern/trap/trapentry.S` 中标有 `lab3 function1 YOUR CODE` 的两处待实现区域。不得修改已经提供的 `SAVE_ALL` 宏、Trap frame 布局和 `__trapret` 的既有返回逻辑。
4. 成员 B 负责 `interrupt_handler()`，成员 C 负责 `exception_handler()`。不得实现、修改或覆盖这两位成员负责的代码。
5. 不得修改 `trap()`、`trap_dispatch()`、`idt_init()` 或其他已有接口的定义，也不得通过修改测试代码规避失败。
6. 保留 `lab3 function1` 标记。提交前应将 `YOUR CODE` 替换为本人成员学号；当前若无法从工作区确定学号，不得编造，保留标记并在最终总结中提醒我手动填写。
7. 采用最小修改原则，不重构无关代码，不增加不必要的文件或运行时功能。
8. 在实现前检查工作区现有的 Git 修改，不覆盖他人的未提交工作，不擅自提交或推送。

**输出要求**：

1. 依据下列 `[RELY]`、`[GUARANTEE]` 和 `[SPECIFICATION]` 实现代码，以实际源码定义为最终依据。
2. 先分析 Trap frame 与 `SAVE_ALL` 的关系，再实施修改，最后验证。
3. 完成后展示关键代码变更、设计理由、验证结果以及 `git diff` 摘要。
4. 为后续实验报告生成 `docs/lab3_member_a.md`，若 `docs` 不存在可以创建。文档包含 Trap frame 布局、实现原理、关键指令解释、规格说明、测试记录与问题分析。
5. 区分真实执行的测试结果和未执行的测试。不得编造日志、PASS 结果或 Bug 修复经历。

[RELY]

以下信息已从 Lab3 原始工程中核对，执行时仍应重新检查当前工作区中的对应定义。

### 1. RISC-V 汇编基础定义

文件：`libs/riscv.h`

在当前 RV64 编译目标下：

- 一个寄存器占据 8 字节。
- Trap frame 包含 36 个寄存器宽度的槽位。
- Trap frame 的总大小为 `36 * REGBYTES = 288` 字节。
- 288 是 16 的整数倍，因此在进入 Trap 前 `sp` 满足 16 字节对齐时，分配 Trap frame 后仍满足该约束。

### 2. Trap frame 数据结构

文件：`kern/trap/trap.h`

其中，`struct pushregs` 的字段依次对应 x0～x31，对应的 Trap frame 偏移量为：

| 槽位 | 保存内容 |
|---|---|
| 0～31 | x0～x31 |
| 32 | sstatus |
| 33 | sepc |
| 34 | sbadaddr / Trap frame 的 badvaddr |
| 35 | scause |

### 3. 已有 SAVE_ALL

文件：`kern/trap/trapentry.S`

当前 `SAVE_ALL` 已经实现，其核心如下：

- `SAVE_ALL` 完成时，`sp` 指向 Trap frame 的起始地址。
- `2*REGBYTES(sp)` 存放原始 `sp`。
- `SAVE_ALL` 已将 `sscratch` 清零。
- 原始通用寄存器已保存，即使调用 C 函数修改了调用者保存寄存器，也应能通过 Trap frame 恢复。

### 4. 已有测试接口

相关文件：

```text
tools/test_lab3_context.S
tools/test_lab3.c
tools/test_lab3.h
tools/grade.sh
tools/inject.py
```

`tools/test_lab3_context.S` 中的 `test_lab3_register_context` 会设置多个通用寄存器的特征值，通过 `ebreak` 触发 Trap，并在返回后比较寄存器值。

`tools/grade.sh` 的测试包括：

- Test 1：Trap 入口、寄存器恢复和断点 EPC，35 分。
- Test 2：非法指令 EPC 更新和异常兜底，25 分。
- Test 3：重复时钟中断与 Trap 返回，40 分。

Test 1 依赖成员 C 的断点异常处理；Test 3 依赖成员 B 的时钟中断处理。因此这些测试只有在对应模块完成后才能作为完整的联合验收依据。

[GUARANTEE]

必须实现或修改的接口：

### 1. `RESTORE_ALL`

目标文件：

```text
kern/trap/trapentry.S
```

接口形式：

```asm
.macro RESTORE_ALL
    # 在指定位置实现
.endm
```

必须具备以下能力：

- 根据已保存的 Trap frame 恢复必要的 CSR。
- 恢复全部可写通用寄存器的原始值。
- 正确恢复原始栈指针。
- 使后续 `sret` 能返回正确的执行位置和特权状态。

### 2. `__alltraps`

目标文件：

    ```text
    kern/trap/trapentry.S
    ```

接口形式：

    ```asm
    .globl __alltraps
    .align(2)
    __alltraps:
        SAVE_ALL
        # 在指定位置实现
    ```

必须具备以下能力：

- 使 `trap()` 接收到指向当前 Trap frame 的有效指针。
- 遵循 RISC-V 调用约定。
- 在 `trap()` 返回后保留正确的 Trap frame 基址。
- 将控制流衔接至已有的 `__trapret`。

### 3. 保持不变的接口

以下内容不允许重新实现或修改：

    ```c
    void trap(struct trapframe *tf);
    ```

以及：

    ```asm
    .macro SAVE_ALL
    ```

和：

    ```asm
    .globl __trapret
    __trapret:
        RESTORE_ALL
        sret
    ```

除两处待完成区域之外，不得改变其他代码行为。

[SPECIFICATION]

## 1. RESTORE_ALL

**Pre-Condition**：

- 已发生一次 Trap，且 `SAVE_ALL` 成功保存处理器上下文。
- 当前执行处于 S 模式的 Trap 返回流程中。
- 当前 `sp` 指向有效的 Trap frame。
- Trap frame 的结构与 `struct trapframe` 完全一致。
- C 语言 `trap()` 已完成处理，可能修改了 Trap frame 中允许更新的字段。

**Post-Condition**：

- Trap frame 中保存的通用寄存器值恢复到对应硬件寄存器。
- `sstatus` 和 `sepc` 被恢复为 Trap frame 中的目标值。
- `sp` 恢复为原始栈指针。
- 原 Trap frame 不再依赖于当前 `sp` 作为工作基址。
- 随后执行 `sret` 时，处理器能够按照恢复后的状态返回目标执行流。

**Case 1：正常 Trap 返回**

当 Trap 处理程序没有修改原有执行位置时，恢复后的 `sepc` 与保存的执行位置一致。执行上下文恢复后，能够返回被中断的原执行流，不丢失任何通用寄存器值。

**Case 2：Trap 处理程序修改了 EPC**

当同步异常处理程序已经调整 `tf->epc` 时，应恢复修改后的 EPC，而不是覆盖成异常发生前的旧值。这样 `sret` 才能返回调整后的目标指令地址，避免重复执行已处理的异常指令。

**Case 3：栈指针的特殊恢复**

当恢复操作需要读取 Trap frame 中的多个寄存器槽位时，必须保证用于访问 Trap frame 的基址始终有效。不能在其他寄存器和 CSR 尚未恢复完成时，过早改变作为工作基址的 `sp`，从而造成错误访存或寄存器恢复不完整。

## 2. __alltraps

**Pre-Condition**：

- RISC-V 处理器已将当前 Trap 路由到 S 模式。
- `stvec` 指向 `__alltraps`。
- 入口执行的 `SAVE_ALL` 已保存 Trap 上下文。
- `sp` 指向当前 Trap frame。
- `trap(struct trapframe *tf)` 已由 C 语言模块提供。

**Post-Condition**：

- 已正确调用 `trap()`。
- `trap()` 收到的参数为当前 Trap frame 的地址。
- C 语言处理函数可以访问和按需修改 Trap frame。
- `trap()` 正常返回后，`sp` 仍指向同一个 Trap frame。
- 控制流能够继续进入 `__trapret`，完成上下文恢复。

**Case 1：正常中断入口**

当发生异步中断时，CPU 转入 `__alltraps`。入口保存上下文后，将有效的 Trap frame 传给 C 语言中断处理逻辑；处理完成后返回恢复路径。中断处理程序应能够利用 Trap frame 判断中断来源，而汇编入口不需要自行区分具体中断类型。

**Case 2：同步异常入口**

当发生断点异常或非法指令异常时，同样通过统一的汇编入口进入 `trap()`。如果 C 语言异常处理程序修改了 `tf->epc`，汇编入口不得丢弃这一修改。

**Case 3：C 函数调用导致寄存器变化**

当 `trap()` 执行过程中改变临时寄存器、参数寄存器和返回地址寄存器时，Trap 返回仍应恢复被打断执行流的原始寄存器状态。这些恢复所需的信息必须保存在 Trap frame 中，不依赖 C 函数保留所有寄存器。

## 3. 验证与验收要求

在完成实现后，请按照以下阶段验证。

**阶段一：静态分析**

核对：

- 32 个 GPR 槽位和 4 个 CSR 槽位是否完整对应。
- Trap frame 总大小及栈对齐。
- `RESTORE_ALL` 的恢复顺序。
- 原始 `sp` 的特殊处理。
- `a0` 参数传递和 `trap()` 的调用约定。
- `ra` 在调用 C 函数后的恢复。
- `__trapret` 和 `sret` 的正确衔接。

**阶段二：独立汇编检查**

优先对 `kern/trap/trapentry.S` 进行单独交叉编译和反汇编验证。

使用当前工程配置的 RISC-V 工具链，不要假设环境中一定存在某个版本的编译器。

检查：

- 是否存在汇编语法错误。
- CSR 指令是否合法。
- 宏是否能够正确展开。
- `__alltraps` 与 `__trapret` 是否正确生成。
- Trap 返回流程中是否出现意外修改 `sp` 或控制流的操作。
最后，请向我汇报：

- 实际修改了哪些文件。
- `RESTORE_ALL` 的关键设计。
- `__alltraps` 的关键设计。
- 已执行的验证命令及真实结果。
- 是否存在尚待解决的问题。
- 后续与其他成员集成时需要特别注意的接口。
- 当前代码是否满足当前成员的全部交付要求。

`````

# 中断与时钟中断功能实现提示词

```text
中断处理与时钟中断功能的实现
工作目录：E:\Operating_System\ucore\NKU-OS-2026fall-lab3\code
[PROMPT]

请基于当前项目的真实源码，实现 kern/trap/trap.c 中的中断处理功能 interrupt_handler()。

仅修改该函数所在区域，必要时补充头文件引用。保持现有接口不变，不修改其他功能模块。

[RELY]

根据实际源码核对并使用以下接口：

struct trapframe 及 tf->cause
IRQ_S_TIMER、IRQ_S_SOFT 等中断常量
clock_set_next_event()
volatile size_t ticks
TICK_NUM（100）
print_ticks()
print_trapframe()
sbi_shutdown()

[GUARANTEE]

实现以下函数：

void interrupt_handler(struct trapframe *tf);

确保函数能够正确分类和处理中断，与现有 trap_dispatch() 及 Trap 返回流程正常衔接。

[SPECIFICATION]

Pre-Condition：
tf 指向有效的 Trap frame，通过 tf->cause 获取中断原因。

Post-Condition：
正确处理对应中断，时钟中断能够持续触发，计数准确，不破坏原有执行状态。

Case 1：S 模式时钟中断
识别 IRQ_S_TIMER，调用 clock_set_next_event() 设置下一次时钟事件，每次中断使 ticks 增加一次。当 ticks 达到 TICK_NUM 的整数倍时调用 print_ticks()。

Case 2：S 模式软件中断
识别 IRQ_S_SOFT，输出 Supervisor software interrupt。

Case 3：其他已知中断
根据 riscv.h 中定义的中断类型进行分类，输出对应提示信息。

Case 4：未知中断
输出中断原因，例如 Unknown interrupt: cause 15，并调用 print_trapframe(tf) 提供调试信息。

Case 5：关机条件
检查项目中是否已经定义 SHUTDOWN_AFTER。若存在，按其约定实现累计输出后的关机逻辑；若不存在，不自行猜测阈值，先完成其他功能并在结果中说明。

Requirements：

1. 正确处理 scause 的中断标志位和原因码。
2. 时钟中断不能只触发一次，必须能够持续运行至 ticks=200。
3. 不随意修改 tf->epc 或其他上下文信息。
4. 保留 lab3 标记，不覆盖 exception_handler() 的实现区域。
5. 不修改 trap_dispatch()、trapentry.S、clock.c、init.c 及评分脚本。
6. 参考 tools/grade.sh 和 tools/test_lab3.c 中相关的测试要求。
7. 不进行无关重构或额外功能开发。

完成后检查代码差异，并在其他依赖模块已具备的情况下执行必要的编译与测试。如果测试因其他尚未实现的模块而无法通过，直接说明原因，不修改其他模块。

最后简要报告修改内容、测试结果及遗留问题。

本次不要执行 Git commit 或 push，等待我审核后再提交。
```
