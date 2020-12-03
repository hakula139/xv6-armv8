# Lab 5: Process Management and System Call

## 习题解答

### 1. 进程管理

#### 1.1 关于 PCB 设计

> 在 proc（即 PCB）中仅存储了进程的 trapframe 与 context 指针，请说明 trapframe 与 context 的实例存在何处，为什么要这样设计？

#### 1.2 Context switch

> 请完成 `inc/proc.h` 中 `struct context` 的定义以及 `kern/swtch.S` 中 context switch 的实现。

#### 1.3 关于 Context switch 设计

##### 1.3.1

> 在 `kern/proc.c` 中将 `swtch` 声明为 `void swtch(struct context**, struct context*)`，请说明为什么要这样设计？

##### 1.3.2

> `context` 中仅需要存储 callee-saved registers，请结合 PCS 说明为什么？

##### 1.3.3

> 与 trapframe 对比，请说明为什么 trapframe 需要存储这么多信息？

##### 1.3.4

> trapframe **似乎** 已经包含了 context 中的内容，为什么上下文切换时还需要先 trap 再 switch？

#### 1.4 内核进程管理模块

> 请根据 `kern/proc.c` 中相应代码的注释完成内核进程管理模块以支持调度第一个用户进程 `user/initcode.S`。

### 2. 系统调用

#### 2.1 系统调用模块

> 目前内核已经支持基本的异常处理，在本实验中还需要进一步完善内核的系统调用模块。

## 测试环境

- OS: Ubuntu 18.04.5 LTS (WSL2 4.19.128-microsoft-standard)
- Compiler: gcc version 8.4.0 (Ubuntu/Linaro 8.4.0-1ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Debugger: GNU gdb 8.2 (Ubuntu 8.2-0ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Emulator: QEMU emulator version 5.0.50
- Using GNU Make 4.1
