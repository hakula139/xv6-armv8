# Lab 7: File System and Shell

## 习题解答

### 1. 文件系统

> 请实现文件系统，本实验中的文件系统遵循 xv6 的设计，你也可以从 0 开始设计属于你的文件系统。如果你的文件系统不同于 xv6 的话，请修改 `user/src/mkfs`。你需要添加测试证明你实现的文件系统可以读取到你打包的文件，在数量、内容上是正确的。

#### 1.0 总览

![Layers of the xv6 file system](./assets/file_system.png)

本图引自 *xv6: a simple, Unix-like teaching operating system* [^1]。

文件系统的总体结构参考 Xv6 [^2] 的设计，其 7 层结构如图所示。以下我们将自底向上依次进行阐述。

#### 1.1 Disk

第 1 层是磁盘驱动，作为物理磁盘的抽象层，为操作系统提供了读写磁盘块的方法。我们已经在 `kern/sd.c` 中实现，详见 Lab 6 第 2 节。

在这一层中，我们提供了以下方法：

- `sd_init`：初始化 SD 卡并解析主引导记录
- `sd_intr`：处理 SD 卡设备中断
- `sd_rw`：读写 SD 卡磁盘块

#### 1.2 Buffer cache

第 2 层是磁盘块缓存，用于将磁盘块缓存到内存中，从而加快磁盘读写。我们已经在 `kern/bio.c` 中实现，详见 Lab 6 第 1 节。

在这一层中，我们提供了以下方法：

- `binit`：初始化 `buf` 队列 `bcache`
- `bread`：从磁盘读取 `buf` 到内存
- `bwrite`：将 `buf` 从内存写回到磁盘
- `brelse`：释放一个不在使用中的 `buf`
- `bpin`：将 `buf` 的引用数（`refcnt`）加 `1`，其中引用数表示当前正在等待此 `buf` 的设备数量
- `bunpin`：将 `buf` 的引用数（`refcnt`）减 `1`

#### 1.3 Logging

第 3 层是磁盘改动日志，用于维护文件系统的崩溃一致性（crash consistency），确保写文件的事务是原子（atomic）的。我们在 `kern/log.c` 中实现。

#### 1.4 Inode

#### 1.5 Directory

#### 1.6 Pathname

#### 1.7 File descriptor

### 2. 系统调用

> 请修改 `syscall.c` 以及 `trapasm.S` 来接上 musl，或者修改 Makefile 并搬运 xv6 的简易 libc，从而允许用户态程序通过调用系统调用来操作文件系统。

### 3. Shell

> 我们已经把 xv6 的 shell 搬运到了 `user/src/sh` 目录下，但需要实现 brk 系统调用来使用 malloc，你也可以自行实现一个简单的 shell。请在 `user/src/cat` 中实现 cat 命令并在你的 shell 中执行。

### 4. 测试（可选）

> 文件系统最重要的能力是在系统崩溃和恢复的时候不会出现数据不一致的情况。请你设计测试来验证文件系统的崩溃一致性。

## 运行结果

```bash
> make qemu
```

```text
```

## 测试环境

- OS: Ubuntu 18.04.5 LTS (5.4.72-microsoft-standard-WSL2)
- Compiler: gcc version 8.4.0 (Ubuntu/Linaro 8.4.0-1ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Debugger: GNU gdb 8.2 (Ubuntu 8.2-0ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Emulator: QEMU emulator version 5.0.50
- Using GNU Make 4.1

[^1]: [xv6: a simple, Unix-like teaching operating system - MIT](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf)  
[^2]: [mit-pdos/xv6-public: xv6 OS - GitHub](https://github.com/mit-pdos/xv6-public)  
[^3]: [mit-pdos/xv6-riscv: Xv6 for RISC-V - GitHub](https://github.com/mit-pdos/xv6-riscv)  
