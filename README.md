## 项目介绍

这是复旦大学 2020 年秋季学期《操作系统》课程的配套实验内容。我们将建立一个基于 ARM 架构的简易教学操作系统，实验（预期）会有如下三次迭代。

### 第一次迭代

在本轮迭代中，我们将实现一个非常简陋的操作系统内核，它仅有一个很小的目标：能够运行一个用户程序且打印字符串。尽管实现上述目标没有内核的支持也可完成，但为了辅助理解操作系统的概念与知识，我们还是希望本轮迭代后的内核能具备现代操作系统内核的基本结构，实验内容将涉及：

- 工具链
- 启动（Booting）
- 异常（中断）处理
- 内存管理
- 进程与调度
- 时钟中断与系统调用

### 第二次迭代

本轮迭代中，我们将在第一轮迭代的基础上进一步完善内核，以更加符合现代操作系统。本轮迭代将涉及：

- 完善内存管理
- 块存储设备管理与驱动
- 文件系统
- 简易 shell

### 第三次迭代（optional）

本次迭代以对操作系统的设计与研究为主，学生可自行构思在不同场景下的操作系统优化与设计，示例设计如下：

- 内存管理优化
- 多核心
- 其他的调度策略
- 更多的设备适配

## 参考资料

- [Arm® Architecture Reference Manual](https://cs140e.sergio.bz/docs/ARMv8-Reference-Manual.pdf)
- [Arm® Instruction Set Reference Guide](https://ipads.se.sjtu.edu.cn/courses/os/reference/arm_isa.pdf)
- [ARM Cortex-A Series Programmer’s Guide for ARMv8-A](https://cs140e.sergio.bz/docs/ARMv8-A-Programmer-Guide.pdf)
- [ARM GCC Inline Assembler Cookbook](https://www.ic.unicamp.br/~celio/mc404-s2-2015/docs/ARM-GCC-Inline-Assembler-Cookbook.pdf)
