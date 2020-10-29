# Lab 3: Interrupts and Exceptions

## 习题解答

### 1. 中断流程

> 请简要描述一下在你实现的操作系统中，中断时 CPU 进行了哪些操作。

参考 `kern/trap.c` 中函数 `trap` 的代码实现：

```c {.line-number}
void
trap(struct trapframe* tf)
{
    int src = get32(IRQ_SRC_CORE(cpuid()));
    if (src & IRQ_CNTPNSIRQ) {
        timer(), timer_reset();
    } else if (src & IRQ_TIMER) {
        clock(), clock_reset();
    } else if (src & IRQ_GPU) {
        if (get32(IRQ_PENDING_1) & AUX_INT)
            uart_intr();
        else
            goto bad;
    } else {
        switch (resr() >> EC_SHIFT) {
        case EC_SVC64:
            cprintf("hello, world\n");
            lesr(0); /* Clear esr. */
            break;
        default:
bad:
            panic("unexpected irq.\n");
        }
    }
}
```

中断时，CPU 进行了以下操作：

1. 检测当前为何种中断，并跳转到对应中断处理函数
2. `TODO`

### 2. trap frame 设计

> 请在 `inc/trap.h` 中设计你自己的 trap frame，并简要说明为什么这么设计。

```c {.line-number}
struct trapframe {
    // General-Purpose Registers
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x4;
    uint64_t x5;
    uint64_t x6;
    uint64_t x7;
    uint64_t x8;
    uint64_t x9;
    uint64_t x10;
    uint64_t x11;
    uint64_t x12;
    uint64_t x13;
    uint64_t x14;
    uint64_t x15;
    uint64_t x16;
    uint64_t x17;
    uint64_t x18;
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

    // Special Registers
    uint64_t sp_el0;    // Stack Pointer
    uint64_t spsr_el1;  // Program Status Register
    uint64_t elr_el1;   // Exception Link Register
};
```

trap frame 中包含了 31 个通用寄存器和 3 个特殊寄存器 `SP_EL0`, `SPSR_EL1`, `ELR_EL1`。

`TODO`

### 3. trap frame 构建与恢复

> 请补全 `kern/trapasm.S` 中的代码，完成 trap frame 的构建、恢复。
