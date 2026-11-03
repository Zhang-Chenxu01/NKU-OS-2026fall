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
