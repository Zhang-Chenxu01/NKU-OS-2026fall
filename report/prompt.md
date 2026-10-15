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
