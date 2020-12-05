# Lab 5: Process Management and System Call

## 习题解答

### 1. 进程管理

#### 1.1 关于 PCB 设计

> 在 proc（即 PCB）中仅存储了进程的 trapframe 与 context 的指针，请说明 trapframe 与 context 的实例存在何处，为什么要这样设计？

![trapframe](./assets/trapframe.png)

本图引自 *xv6: a simple, Unix-like teaching operating system* [^1]。

如图所示，trapframe 的实例存在进程的用户地址空间（user address space）。为什么 PCB 中仅存储 trapframe 的指针？因为 trapframe 是一个保存了所有通用寄存器和一些特殊寄存器的结构。一方面，trap 时需要用到 trapframe 中的数据，所以它应当以某种形式存储在 PCB 中；另一方面，trap 时我们只能传入 trapframe 指针，因为硬件没有提供足够多的寄存器来在 trap 时传入整个 trapframe 结构。因此，在 PCB 中仅存储 trapframe 指针是一个节省空间的方案，trap 时只需一个寄存器用于传入 trapframe 指针即可 [^1]。

![context switch](./assets/context_switch.png)

本图引自 *xv6: a simple, Unix-like teaching operating system* [^1]。

context 的实例存在执行 context switch 的内核所对应的 kernel stack 处。与 trapframe 类似，context 也是一个保存了一组通用寄存器的结构。scheduler 在 context switch 时需要交换进程的 context，然而我们并没有这么多寄存器来在函数调用时传入整个 context 结构。因此，我们在 PCB 中仅存储 context 的指针，这样在调度时 scheduler 就只需用到两个寄存器，分别存放了将被调入和调出的新旧进程的 context 指针 [^1]。

#### 1.2 Context switch

> 请完成 `inc/proc.h` 中 `struct context` 的定义以及 `kern/swtch.S` 中 context switch 的实现。

##### 1.2.1 Context 设计

context 中需要保存所有的 callee-saved 寄存器 [^1]，根据 ARM 开发文档 [^2]，即通用寄存器 X19 ~ X28。此外，我们额外保存寄存器 X29 (Frame Pointer) 和 X30 (Procedure Link Register)，其中 X30 用于指定用户进程初次运行的地址。

```c {.line-number}
// inc/proc.h

struct context {
    // Callee-saved Registers
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;

    uint64_t x29;  // Frame Pointer
    uint64_t x30;  // Procedure Link Register
};
```

##### 1.2.2 Context switch 实现

context switch 主要做了以下几件事情 [^3]：

1. 将当前（将被调出的）旧进程的 callee-saved 寄存器压栈
2. 将当前栈指针的地址保存在 `*old`（其中 `old` 是函数调用传入的第一个参数，位于寄存器 X0），构建旧进程的 context
3. 将 `new`（即函数调用传入的第二个参数，位于寄存器 X1）的值覆盖当前栈指针的地址，切换到（将被调入的）新进程的 context
4. 将新进程的 callee-saved 寄存器弹栈，函数 `swtch` 返回（`ret`，等价于 `mov pc, x30`）

```assembly {.line-number}
# kern/swtch.S

/*
 * Context switch
 *
 *   void swtch(struct context **old, struct context *new);
 *
 * Save current register context on the stack,
 * creating a struct context, and save its address in *old.
 * Switch stacks to new and pop previously-saved registers.
 */
.global swtch

swtch:
    # Save old callee-saved registers
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!

    # Switch stacks
    mov x19, sp
    str x19, [x0]
    mov sp, x1

    # Load new callee-saved registers
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16

    ret
```

#### 1.3 关于 Context switch 设计

##### 1.3.1

> 在 `kern/proc.c` 中将 `swtch` 声明为 `void swtch(struct context**, struct context*)`，请说明为什么要这样设计？

因为如果第一个参数传的是 `struct context*`，那么在函数 `swtch` 中对第一个参数值的修改（也就是将栈指针的地址保存在寄存器 X0）将无法反映到函数外部。即在函数返回后，这个局部变量就会失效，这样也就无法保存旧进程的 context 指针。而传入 `struct context**`，就可以通过修改这个指针所指向的地址，来将旧进程的 context 地址传给函数外部。

##### 1.3.2

> `context` 中仅需要存储 callee-saved registers，请结合 PCS 说明为什么？

因为根据 PCS (Procedure Call Standard) [^2]，函数调用时，callee 只需要确保约定的 callee-saved 寄存器中的数据不被损坏（corrupt），而其他寄存器中的数据是可以损坏的。因此在 context switch 中，context 不需要存储 callee-saved 寄存器以外的其他寄存器，因为即使这些数据在 context switch 的过程中被损坏了也没有关系。context 只需保护 callee-saved 寄存器中的数据不受 context switch 影响即可。

##### 1.3.3

> 与 trapframe 对比，请说明为什么 trapframe 需要存储这么多信息？

因为 trap 过程不是函数调用，没有 caller 和 callee 的说法，不遵循也无法遵循 PCS 规范。例如系统中断时，内核可以直接中断用户程序，用户程序并不会有机会提前保存所谓的 caller-saved 寄存器，但这些数据同样是不应在 trap 后被内核程序损坏的。因此 trapframe 需要存储所有通用寄存器，才能保证之后回到用户态时可以正确还原用户程序的数据。

##### 1.3.4

> trapframe **似乎** 已经包含了 context 中的内容，为什么上下文切换时还需要先 trap 再 switch？

因为 trap 过程是从用户态切换到内核态的过程，switch 过程是内核态中的过程。上下文切换需要在内核态中进行，因此还是要先 trap 再 switch。虽然 trapframe 似乎包含了 context 中的内容，但它们完全是两个不同的东西，有着不同的用途，保存在不同的位置，因此也无法复用其中的数据。

#### 1.4 内核进程管理模块

> 请根据 `kern/proc.c` 中相应代码的注释完成内核进程管理模块以支持调度第一个用户进程 `user/initcode.S`。

##### 1.4.1 PCB 设计

每个用户进程的 PCB 中保存了以下数据，详见注释 [^4]：

```c {.line-number}
// inc/proc.h

struct proc {
    struct spinlock lock;

    // p->lock must be held when using these:
    enum procstate state;  // Process state
    void* chan;            // If non-zero, sleeping on chan
    int killed;            // If non-zero, have been killed
    int xstate;            // Exit status to be returned to parent's wait
    int pid;               // Process ID

    // wait_lock must be held when using these:
    struct proc* parent;  // Parent process

    // no lock needs to be held when using these:
    char* kstack;             // Bottom of kernel stack for this process
    uint64_t sz;              // Size of process memory (bytes)
    uint64_t* pgdir;          // Page table
    struct trapframe* tf;     // Trapframe for current syscall
    struct context* context;  // swtch() here to run process
    char name[16];            // Process name (debugging)
};
```

这里我们选择在每个 `struct proc` 中而非在整个 `struct ptable` 中新增一个自旋锁。这样做的目的是为了使锁的控制粒度更细，实际上这也是 Xv6 for RISC-V [^4] 的实现方法。

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

[^1]: [xv6: a simple, Unix-like teaching operating system - MIT](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf)  
[^2]: [AArch64 Instruction Set Architecture | Procedure Call Standard – Arm Developer](https://developer.arm.com/architectures/learn-the-architecture/aarch64-instruction-set-architecture/procedure-call-standard)  
[^3]: [mit-pdos/xv6-public: xv6 OS - GitHub](https://github.com/mit-pdos/xv6-public)  
[^4]: [mit-pdos/xv6-riscv: Xv6 for RISC-V - GitHub](https://github.com/mit-pdos/xv6-riscv)
