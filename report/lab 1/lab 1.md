# Lab 1: Booting

## 习题解答

### 1

> 未初始化的全局变量和局部静态变量通常会被编译器安排在 BSS 段中，而为了减小可执行文件的大小，编译器不会把这段编入 ELF 文件中。我们需要手动对其进行零初始化，请补全 kern/main.c 中的代码，你可能需要根据 kern/linker.ld 了解 BSS 段的起始和终止位置。

利用函数 `memset()`，我们可以对一段选定的内存空间进行初始化。通过 kern/linker.ld 可知，BSS 段的起始位置为 `edata`，终止位置为 `end`。因此我们需要进行零初始化的内存即从 `edata` 到 `end`。

```c
// kern/main.c
extern char edata[], end[];
memset(edata, 0, end - edata);
```

### 2

> 请补全 kern/main.c 中的代码，完成 console 的初始化并输出 `hello, world\n`，你可能需要阅读 inc/console.h, kern/console.c。

根据 kern/console.c 的代码可知，利用函数 `console_init()` 可将 console 初始化，利用函数 `cprintf()` 可输出字符到 console。

```c
// kern/main.c
console_init();
cprintf("hello, world\n");
```
