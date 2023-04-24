# Speed up system calls ([easy](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> Some operating systems (e.g., Linux) speed up certain system calls by sharing data in a read-only region between userspace and the kernel. This eliminates the need for kernel crossings when performing these system calls. To help you learn how to insert mappings into a page table, your first task is to implement this optimization for the `getpid()` system call in xv6.
>
> > When each process is created, map one read-only page at USYSCALL (a virtual address defined in `memlayout.h`). At the start of this page, store a `struct usyscall` (also defined in `memlayout.h`), and initialize it to store the PID of the current process. For this lab, `ugetpid()` has been provided on the userspace side and will automatically use the USYSCALL mapping. You will receive full credit for this part of the lab if the `ugetpid` test case passes when running `pgtbltest`.
>
> Some hints:
>
> - You can perform the mapping in `proc_pagetable()` in `kernel/proc.c`.
> - Choose permission bits that allow userspace to only read the page.
> - You may find that `mappages()` is a useful utility.
> - Don't forget to allocate and initialize the page in `allocproc()`.
> - Make sure to free the page in `freeproc()`.
>
> > Which other xv6 system call(s) could be made faster using this shared page? Explain how.

这个实验的想法就是将本来通过系统调用获取的一些数据直接通过一个内核和用户进程共享的内存页传递，数据预先由内核写入该内存区，用户进程需要的时候就可以从中读取。这样可以降低开销（从系统调用开销降低到内存读取的开销）。

根据提示，共享 PID 信息的相关数据结构已经定义为 `struct usyscall`。

首先在 PCB（`kernel/proc.h` 的 `struct proc`）中添加一项 `struct usyscall* usyscall`，用于指向共享页中结构体的地址（根据 `ugetpid()`，也就是 `USYSCALL` 处，定义为页的起始地址）。

随后在 `allocproc()` 中模仿 `trapframe` 的形式添加共享的物理页的分配与数据的写入。因为在内核中，`kalloc()` 分配得到的页的虚拟地址和物理地址相同（根据之前的实验的介绍可以知道这部分是“直接”映射的），因此可以直接使用“物理地址”访问：

```c
  if((p->usyscall = (struct usyscall *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  p->usyscall->pid = p->pid;
```

随后在 `proc_pagetable()` 中模仿已有的项添加相关页的映射，将用户进程的虚拟地址 `USYSCALL` 映射到物理地址 `p->usyscall` 处，映射一页，权限为用户可读（`PTE_R | PTE_U`）。如果映射失败，需要将之前成功的映射也回滚（参考 `TRAPFRAME` 映射的代码）。代码如下：

```c
  if(mappages(pagetable, USYSCALL, PGSIZE,
              (uint64)(p->usyscall), PTE_R | PTE_U) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
```

进程销毁时也需要撤销映射，释放页表，并且释放分配的共享页，清空 PCB 中的指针：

- 在 `proc_freepagetable()` 中添加 `uvmunmap(pagetable, USYSCALL, 1, 0);`
- 在 `freeproc()` 中添加：
  ```c
    if(p->usyscall)
        kfree((void *)p->usyscall);
    p->usyscall = 0;
  ```

# Print a page table ([easy](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> To help you visualize RISC-V page tables, and perhaps to aid future debugging, your second task is to write a function that prints the contents of a page table.
>
> > Define a function called `vmprint()`. It should take a `pagetable_t` argument, and print that pagetable in the format described below. Insert `if(p->pid==1) vmprint(p->pagetable)` in exec.c just before the `return argc`, to print the first process's page table. You receive full credit for this part of the lab if you pass the `pte printout` test of `make grade`.
>
> Now when you start xv6 it should print output like this, describing the page table of the first process at the point when it has just finished `exec()`ing `init`:
>
> ```
> page table 0x0000000087f6b000
>  ..0: pte 0x0000000021fd9c01 pa 0x0000000087f67000
>  .. ..0: pte 0x0000000021fd9801 pa 0x0000000087f66000
>  .. .. ..0: pte 0x0000000021fda01b pa 0x0000000087f68000
>  .. .. ..1: pte 0x0000000021fd9417 pa 0x0000000087f65000
>  .. .. ..2: pte 0x0000000021fd9007 pa 0x0000000087f64000
>  .. .. ..3: pte 0x0000000021fd8c17 pa 0x0000000087f63000
>  ..255: pte 0x0000000021fda801 pa 0x0000000087f6a000
>  .. ..511: pte 0x0000000021fda401 pa 0x0000000087f69000
>  .. .. ..509: pte 0x0000000021fdcc13 pa 0x0000000087f73000
>  .. .. ..510: pte 0x0000000021fdd007 pa 0x0000000087f74000
>  .. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
> init: starting sh
>
> ```
>
> The first line displays the argument to `vmprint`. After that there is a line for each PTE, including PTEs that refer to page-table pages deeper in the tree. Each PTE line is indented by a number of `" .."` that indicates its depth in the tree. Each PTE line shows the PTE index in its page-table page, the pte bits, and the physical address extracted from the PTE. Don't print PTEs that are not valid. In the above example, the top-level page-table page has mappings for entries 0 and 255. The next level down for entry 0 has only index 0 mapped, and the bottom-level for that index 0 has entries 0, 1, and 2 mapped.
>
> Your code might emit different physical addresses than those shown above. The number of entries and the virtual addresses should be the same.
>
> Some hints:
>
> - You can put `vmprint()` in `kernel/vm.c`.
> - Use the macros at the end of the file kernel/riscv.h.
> - The function `freewalk` may be inspirational.
> - Define the prototype for `vmprint` in kernel/defs.h so that you can call it from exec.c.
> - Use `%p` in your printf calls to print out full 64-bit hex PTEs and addresses as shown in the example.
>
> > Explain the output of `vmprint` in terms of Fig 3-4 from the text. What does page 0 contain? What is in page 2? When running in user mode, could the process read/write the memory mapped by page 1? What does the third to last page contain?

这个实验是要在 `kernel/vm.c` 中添加 `vmprint()` 函数，用于打印当前用户进程的页表。实现方式可以参考 `freewalk()` 函数便利整个页表。`freewalk()` 的实现方式是使用递归调用的形式，层层打印有数据的 PTE，直到页表叶节点。

为了便于打印缩进，参数中额外添加了页表的层级。

流程主要是从参数给出的 `pagetable` 作为根节点开始遍历页表项，取出非空页表项打印，物理地址可以使用 `PTE2PA` 宏来解析。若 PTE 非叶节点（即 PTE_R, PTE_W, PTE_X 都为 0 且 PTE_V 为 1），则将该页表项所指的页的起始物理地址作为根节点再次遍历。具体代码如下：

```c
void vmprint(pagetable_t pagetable, int level){
  if (level == 1)
    printf("page table %p\n", pagetable);
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if (pte) {
      for (int i = 0; i < level; i++) {
        printf(" ..");
      }
      printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
    }
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      vmprint((pagetable_t)child, level + 1);
    }
  }
}
```

最后根据要求在 `exec()` 的 `return argc;` 之前加入 `if(p->pid==1) vmprint(p->pagetable, 1);`，并在 `kernel/defs.h` 中添加函数签名 `void vmprint(pagetable_t pagetable, int level);`

# Detect which pages have been accessed ([hard](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> Some garbage collectors (a form of automatic memory management) can benefit from information about which pages have been accessed (read or write). In this part of the lab, you will add a new feature to xv6 that detects and reports this information to userspace by inspecting the access bits in the RISC-V page table. The RISC-V hardware page walker marks these bits in the PTE whenever it resolves a TLB miss.
>
> > Your job is to implement `pgaccess()`, a system call that reports which pages have been accessed. The system call takes three arguments. First, it takes the starting virtual address of the first user page to check. Second, it takes the number of pages to check. Finally, it takes a user address to a buffer to store the results into a bitmask (a datastructure that uses one bit per page and where the first page corresponds to the least significant bit). You will receive full credit for this part of the lab if the `pgaccess` test case passes when running `pgtbltest`.
>
> Some hints:
>
> - Read `pgaccess_test()` in `user/pgtlbtest.c` to see how `pgaccess` is used.
> - Start by implementing `sys_pgaccess()` in `kernel/sysproc.c`.
> - You'll need to parse arguments using `argaddr()` and `argint()`.
> - For the output bitmask, it's easier to store a temporary buffer in the kernel and copy it to the user (via `copyout()`) after filling it with the right bits.
> - It's okay to set an upper limit on the number of pages that can be scanned.
> - `walk()` in `kernel/vm.c` is very useful for finding the right PTEs.
> - You'll need to define `PTE_A`, the access bit, in `kernel/riscv.h`. Consult the [RISC-V privileged architecture manual](https://github.com/riscv/riscv-isa-manual/releases/download/Ratified-IMFDQC-and-Priv-v1.11/riscv-privileged-20190608.pdf) to determine its value.
> - Be sure to clear `PTE_A` after checking if it is set. Otherwise, it won't be possible to determine if the page was accessed since the last time `pgaccess()` was called (i.e., the bit will be set forever).
> - `vmprint()` may come in handy to debug page tables.

这个实验要创建一个系统调用，用于检查当前页表中已访问过的页。在当前的 RISC-V 内存访问模型下，当某一个（TLB 未命中的）虚拟页被访问时，会在其页表项上添加 A 位。

首先在 `syscall.c` 中添加系统调用所用到的函数的声明和数组的对应项。这个实验中添加系统调用不需要修改 `kernel/syscall.h`、`user/user.h` 和 `user/usys.pl`，因为这些文件已经被预先修改好了。

在 `kernel/riscv.h` 中添加对 PTE 中 A(Access) 位访问的宏 `#define PTE_A (1L << 6)`。

在 `kernel/sysproc.c` 中添加处理 pgaccess 系统调用的函数 `sys_pgaccess()`（实际上也已经添加好，只需要对其进行补全）。根据提示，这个系统调用有三个参数，分别是起始虚拟地址、遍历的页数和用户空间的数据缓冲区用于传回数据。看一下 `user/pgtlbtest.c` 中的 `pgaccess_test()`，实际上这个缓冲区是一个 `unsigned int` 变量，因此我们也建立一个同类型变量存储页表项访问位的位图 `bitmask`。

位图就是以（一个或多个）位来表示一项数据，以一位表示一项数据来说，一个 `unsigned int` 变量（32 bit 长）可以存储 32 项数据。根据 `pgaccess_test()`，一位表示一个页表项是否有 A 位，其中位图的最低位表示第一页。由于变量长度有限，因此要判断需要检查的页的数量是否超过变量长度，如果超过，直接返回错误。

`argaddr()` 与 `argint()` 的区别就在于获取到的数据的长度和类型，对于需要 64 bit 长的数据，使用 `argaddr()` 获取，需要 32 bit 长的数据使用 `argint()`。

从起始的虚拟地址开始，每次使用 `walk()` 函数获取对应的 PTE，判断 A 位是否置位，如果有，则对 `bitmask` 对应位置 1，并根据题目要求将 PTE 的 A 位清除。每次循环虚拟地址加上 PGSIZE，就得到下一个页的起始虚拟地址。

最后使用 `copyout()` 将 `bitmask` 从内核区拷贝到用户进程区，因为内核态页表没有映射用户进程的那部分的虚拟地址空间，因此需要使用这种方式通过物理地址直接拷贝。

完整函数如下：

```c
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 start_va;
  int num;
  uint64 user_buffer;
  unsigned int bitmask = 0;

  argaddr(0, &start_va);
  argint(1, &num);
  argaddr(2, &user_buffer);

  if (num > 32)
    return -1;

  for (int i = 0; i < num; i++) {
    pte_t *pte = walk(myproc()->pagetable, start_va + i * PGSIZE, 0);
    if (*pte & PTE_A) {
      bitmask |= (1L << i);
      *pte^=PTE_A;
    }
  }
  if (copyout(myproc()->pagetable, user_buffer, (char *)&bitmask,
              sizeof(unsigned int)) != 0)
    return -1;
  return 0;
}
```
