# 功能模块一——用户态系统调用

`````
[PROMPT]

**任务**：基于当前已合并的 Lab5 工程，实现用户态系统调用功能模块，完成从用户程序执行 `ecall`、内核识别异常、系统调用分发、执行对应内核服务，到返回用户态的处理链路。

主要修改文件：

- `code/kern/trap/trap.c`：完善用户态系统调用异常处理。
- `code/kern/syscall/syscall.c`：实现系统调用分发及各 `sys_*` 处理函数。

**操作要求**：

1. 必须直接修改项目中的实际代码，完成函数实现，而非仅提供代码片段或实现建议。
2. 保留 Lab1–Lab4 已完成的代码及现有异常处理逻辑，尽量只修改本模块涉及的函数和必要依赖。
3. 只负责成员 A 的功能模块一，不修改其他成员负责的 `do_*` 进程管理函数、虚拟内存实现或 `trapentry.S`。对于尚未完成的依赖，应记录并留待集成验证。
4. 实现前检查已有代码、函数声明及系统调用 ABI，以工程中的实际接口为准，避免重复定义或修改公开接口。
5. **Git 仅允许只读操作**，例如 `git status`、`git diff`、`git log`、`git show`。禁止执行 `git add`、`git commit`、`git push`、`git checkout`、`git reset`、`git stash` 及其他 Git 写操作，不创建或切换分支。源码与文档可以正常修改，但不进行 Git 暂存、提交或其他状态操作。

**输出要求**：

完成代码实现后，进行编译及必要的功能检查，并在 `docs/lab5_member_a_module1.md` 中输出交付文档，说明：

- 各函数的完成状态及对应代码位置。
- 修改的文件、主要实现内容与关键逻辑。
- 系统调用的完整执行流程。
- 关键 Case 及其处理方式。
- 实际执行的编译、测试命令和结果。
- 当前尚未完成或需要其他成员配合验证的内容。

文档必须依据真实实现与测试结果，不编造测试通过情况。

[RELY]

实现前读取以下相关文件，确认实际接口：

**1. 系统调用相关**

- `libs/unistd.h`：系统调用编号。
- `user/libs/syscall.c`：用户态系统调用参数约定。
- `kern/syscall/syscall.h`：系统调用入口声明。

**2. 异常及上下文相关**

- `kern/trap/trap.h`：`trapframe`、`pushregs` 等结构。
- `kern/trap/trap.c`：现有异常处理逻辑。
- `kern/trap/trapentry.S`：寄存器保存与恢复逻辑。
- `libs/riscv.h`：RISC-V 相关 CSR 和状态位定义。

**3. 进程与内存管理相关**

- `kern/process/proc.h`：`current`、`proc_struct` 及 `do_*` 接口。
- `kern/mm/vmm.h`：`mm_struct`、`user_mem_check` 等接口。
- 控制台输出及页表打印相关已有函数声明。

可以依赖工程中已有的 `do_exit`、`do_fork`、`do_wait`、`do_execve`、`do_yield`、`do_kill`、`cputchar` 等函数。

上述依赖的具体签名、参数和返回值应通过实际代码确认，不自行假设不存在的接口。

[GUARANTEE]

必须实现或完善以下函数：

**文件：**`**kern/trap/trap.c**`

```
void exception_handler(struct trapframe *tf);
```

**文件：**`**kern/syscall/syscall.c**`

```
void syscall(void);

static int sys_exit(uint64_t arg[]);
static int sys_fork(uint64_t arg[]);
static int sys_wait(uint64_t arg[]);
static int sys_exec(uint64_t arg[]);
static int sys_yield(uint64_t arg[]);
static int sys_kill(uint64_t arg[]);
static int sys_getpid(uint64_t arg[]);
static int sys_putc(uint64_t arg[]);
static int sys_pgdir(uint64_t arg[]);
```

要求所有函数均具有实际实现，保持与工程已有声明一致。允许在模块内部增加必要的 `static` 辅助函数，但不引入无关功能。

[SPECIFICATION]

## 1. exception_handler

**Pre-Condition**：

- 已通过 Trap 入口保存异常上下文。
- `tf` 指向当前有效的异常上下文。

**Post-Condition**：

- 用户态系统调用异常被正确转发。
- 异常返回地址和上下文保持正确。
- 原有异常处理功能不受影响。

**Case 1：用户态系统调用**

当异常原因为 `CAUSE_USER_ECALL` 时，将 `epc` 前移 4 字节，并调用 `syscall()` 完成处理，避免返回后重复执行 `ecall`。

**Case 2：其他异常**

保留已有异常处理路径，不改变 Lab3、Lab4 的原有行为。

## 2. syscall

**Pre-Condition**：

- 当前进程具有有效的 `trapframe`。
- 用户程序已按照约定将系统调用号和参数放入对应寄存器。

**Post-Condition**：

- 合法系统调用被分发到对应的 `sys_*` 函数。
- 对于正常返回的系统调用，返回值写入用户可见的返回寄存器。
- 非法请求得到正确处理，不破坏当前进程上下文。

**Case 1：合法系统调用**

读取 `a0` 中的系统调用号，以及 `a1`–`a5` 中的参数，调用对应处理函数。

**Case 2：非法或未实现的系统调用**

包括编号越界、未注册处理函数等情况，应按照工程已有错误码约定返回错误，不发生数组越界或空函数指针调用。

**Requirements**：

- 保证分发表下标及有效范围正确。
- 返回值按照用户态 ABI 写回。
- 注意 `exit`、`exec` 等可能改变当前执行上下文的特殊路径。

## 3. sys_exit / sys_fork / sys_wait / sys_exec

**Pre-Condition**：

- 系统调用参数已由 `syscall()` 获取。
- 当前进程及相关进程管理接口处于有效状态。

**Post-Condition**：

- 对应操作正确转交内核进程管理层。
- 参数及返回值符合用户态接口语义。
- 进程上下文及资源状态保持一致。

**Case 1：sys_exit**

调用现有退出接口，传递退出码。成功退出后当前进程不应继续执行原用户程序。

**Case 2：sys_fork**

调用现有进程创建接口，正确传递当前执行上下文及所需参数。保证调用约定支持父进程获得子 PID、子进程获得 0 的语义。

**Case 3：sys_wait**

传递目标 PID 和退出码存储地址，处理合法等待请求。对非空用户指针进行必要的地址及写权限检查，遵循底层接口的内存访问约定。

**Case 4：sys_exec**

正确传递程序名称、长度、二进制地址及大小等实际接口要求的参数。检查用户地址、长度及访问权限，避免非法用户指针导致内核异常。

**Requirements**：

- 不重复实现其他成员负责的进程创建、复制、退出和程序装载逻辑。
- 用户指针验证必须与工程当前 `user_mem_check`、`do_wait`、`do_execve` 的真实实现相匹配。
- 考虑 `fork` 返回值、`exec` 成功后的上下文变化及 `exit` 不返回等特殊情况。

## 4. sys_yield / sys_kill / sys_getpid

**Pre-Condition**：

- 当前进程及所需进程管理接口有效。

**Post-Condition**：

- 系统调用请求正确转交对应内核接口。

**Case 1：sys_yield**

请求当前进程主动让出 CPU，正确处理对应接口的返回语义。

**Case 2：sys_kill**

传递目标 PID，正确处理合法进程、无效 PID、目标不存在等情况，具体错误码遵循已有进程管理接口。

**Case 3：sys_getpid**

返回当前进程 PID，不修改进程状态。

## 5. sys_putc / sys_pgdir

**Pre-Condition**：

- 系统调用参数有效。
- 控制台及相关页表接口已初始化。

**Post-Condition**：

- 请求通过内核已有接口完成。
- 不直接暴露不允许访问的内核资源。

**Case 1：sys_putc**

读取字符参数，调用现有控制台输出接口，按照已有约定返回结果。

**Case 2：sys_pgdir**

按照工程已有页表查看接口完成系统调用，保持其参数、输出及返回值语义，不自行创建新的页表管理机制。

## 6. 验证与验收

完成实现后，执行以下检查：

1. 所有规定函数均有完整实现，函数签名与现有声明一致。
2. 执行项目已有的编译流程，检查编译和链接结果。
3. 检查系统调用分发表的编号映射、数组边界和返回值写回。
4. 验证 `CAUSE_USER_ECALL` 的处理及 `epc` 更新。
5. 检查非法系统调用号、非法用户指针、空指针及异常参数等典型情况。
6. 在现有环境允许的情况下执行用户态系统调用测试，并记录实际结果。

若其他成员尚未完成依赖函数，允许先完成静态检查、编译及可独立执行的测试。需要联合验证的功能应在交付文档中明确标记。

不修改或重写 Lab5 现有测试脚本，必要的临时测试应放在项目外的临时目录。

**最终交付**：实际修改后的代码，以及 `docs/lab5_member_a_module1.md`。完成后简要汇报实现情况、测试结果及剩余集成问题。
`````

