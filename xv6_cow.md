# The problem

The fork() system call in xv6 copies all of the parent process's user-space memory into the child. If the parent is large, copying can take a long time. Worse, the work is often largely wasted: fork() is commonly followed by exec() in the child, which discards the copied memory, usually without using most of it. On the other hand, if both parent and child use a copied page, and one or both writes it, the copy is truly needed.

# The solution

Your goal in implementing copy-on-write (COW) fork() is to defer allocating and copying physical memory pages until the copies are actually needed, if ever.

COW fork() creates just a pagetable for the child, with PTEs for user memory pointing to the parent's physical pages. COW fork() marks all the user PTEs in both parent and child as read-only. When either process tries to write one of these COW pages, the CPU will force a page fault. The kernel page-fault handler detects this case, allocates a page of physical memory for the faulting process, copies the original page into the new page, and modifies the relevant PTE in the faulting process to refer to the new page, this time with the PTE marked writeable. When the page fault handler returns, the user process will be able to write its copy of the page.

COW fork() makes freeing of the physical pages that implement user memory a little trickier. A given physical page may be referred to by multiple processes' page tables, and should be freed only when the last reference disappears. In a simple kernel like xv6 this bookkeeping is reasonably straightforward, but in production kernels this can be difficult to get right; see, for example, [Patching until the COWs come home](https://lwn.net/Articles/849638/).

# Implement copy-on-write fork([hard](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> > Your task is to implement copy-on-write fork in the xv6 kernel. You are done if your modified kernel executes both the `cowtest` and 'usertests -q' programs successfully.
>
> To help you test your implementation, we've provided an xv6 program called cowtest (source in `user/cowtest.c`). cowtest runs various tests, but even the first will fail on unmodified xv6. Thus, initially, you will see:
>
> ```shell
> $ cowtest
> simple: fork() failed
> $ 
> ```
>
> The "simple" test allocates more than half of available physical memory, and then fork()s. The fork fails because there is not enough free physical memory to give the child a complete copy of the parent's memory.
>
> When you are done, your kernel should pass all the tests in both `cowtest` and `usertests -q`. That is:
>
> ```shell
> $ cowtest
> simple: ok
> simple: ok
> three: zombie!
> ok
> three: zombie!
> ok
> three: zombie!
> ok
> file: ok
> ALL COW TESTS PASSED
> $ usertests -q
> ...
> ALL TESTS PASSED
> $
> ```
>
> Here's a reasonable plan of attack.
>
> 1. Modify `uvmcopy()` to map the parent's physical pages into the child, instead of allocating new pages. Clear `PTE_W` in the PTEs of both child and parent for pages that have `PTE_W` set.
> 2. Modify `usertrap()` to recognize page faults. When a write page-fault occurs on a COW page that was originally writeable, allocate a new page with `kalloc()`, copy the old page to the new page, and install the new page in the PTE with `PTE_W` set. Pages that were originally read-only (not mapped `PTE_W`, like pages in the text segment) should remain read-only and shared between parent and child; a process that tries to write such a page should be killed.
> 3. Ensure that each physical page is freed when the last PTE reference to it goes away -- but not before. A good way to do this is to keep, for each physical page, a "reference count" of the number of user page tables that refer to that page. Set a page's reference count to one when `kalloc()` allocates it. Increment a page's reference count when fork causes a child to share the page, and decrement a page's count each time any process drops the page from its page table. `kfree()` should only place a page back on the free list if its reference count is zero. It's OK to keep these counts in a fixed-size array of integers. You'll have to work out a scheme for how to index the array and how to choose its size. For example, you could index the array with the page's physical address divided by 4096, and give the array a number of elements equal to highest physical address of any page placed on the free list by `kinit()` in `kalloc.c`. Feel free to modify `kalloc.c` (e.g., `kalloc()` and `kfree()`) to maintain the reference counts.
> 4. Modify `copyout()` to use the same scheme as page faults when it encounters a COW page.
>
> Some hints:
>
> - It may be useful to have a way to record, for each PTE, whether it is a COW mapping. You can use the RSW (reserved for software) bits in the RISC-V PTE for this.
> - `usertests -q` explores scenarios that `cowtest` does not test, so don't forget to check that all tests pass for both.
> - Some helpful macros and definitions for page table flags are at the end of `kernel/riscv.h`.
> - If a COW page fault occurs and there's no free memory, the process should be killed.

这次实验只有一道题，这不分分钟刷完(bushi

根据提示，可以使用 PTE 中的 RSW 位来记录当前项是否是 COW 映射的页表项。并且在 COW 页错误时如果内存不足，则当前进程应该被杀死。

根据提供的 reasonable plan：

- 修改 `uvmcopy()` 函数来使复制页表的时候不复制物理内存，并且将父子目录的所有可写页的 `PTE_W` 复位。
- 修改 `usertrap()` 来处理 COW 页的页错误。当一个原本可写的 COW 页遇到写入页错误时应该使用 `kalloc()` 给它分配新的物理页并复制旧页中的内容到新的物理页，并将新的物理页 `PTE_W` 置位。原先就是只读的页需要保持依然只读，且仍保持父子进程共享一页，尝试写入这种页面的进程应该被杀死。
- 需要为物理页加上引用计数，当引用计数为 1 的物理页被释放时才会真正释放物理页，否则只是减掉相应的引用计数。需要修改 `kalloc()`、`kfree()` 中对应的部分。引用计数部分的初始化要放在 `kinit()` 函数中。
- 需要为 `copyout()` 添加手动的 COW 检测与处理

目前物理内存是靠 `kmem` 链表管理的，感觉其实完全可以用引用计数来管理物理内存，用链表只是检索的开销小一些。不过就先维持原状吧，不对物理管理做大的修改了。

直接建立一个全局数组 `int mem_count[PHYSTOP >> 12]` 管理所有的物理页的引用计数，`PHYSTOP` 宏根据定义指向的是物理内存的末尾。因为是全局数组所以也不需要额外初始化，默认就全为 0. 这个数组也需要加锁，需要在 `kinit()` 函数中初始化锁。这里暂时不考虑并发性能，所以不如对整个数组整体加锁，更方便一些。

当 `kalloc()` 时，需要将对应物理页的引用计数设为 1，`kfree()` 时对应引用计数减 1，直到引用计数为 0 时才将该物理页放入空闲页链表，此外还可以添加对当前要释放页的引用计数的判断，若引用计数为 0 则 panic，防止出现引用计数为负的情况。这很重要，这个判断有助于帮我们检查出包括 `freerange()` 在内的其他问题。

在物理内存管理初始化的时候，`freerange()` 是靠 `free()` 将所有物理页加入到空闲链表的，如果在直接调用 `kfree()`，并且 `kfree()` 不判断引用计数为 0，那么初始化时所有引用计数都为 0，`kfree()` 一次就成了 -1. 因此要么在 `freerange()` 调用 `kfree()` 之前先把对应物理页的引用计数设为 1，要么在 `kfree()` 中额外判断为 0 时不减引用计数。这里选择第一种，并且在 `kfree()` 中对引用计数为 0 仍需释放的情况直接 panic，因为这可以发现更多其他的问题。

`kalloc.c` 的全部代码可以去[仓库](https://github.com/moehanabi/xv6-labs-2022/blob/cow/kernel/kalloc.c)查看，这里就不贴了。

随后去 `kernel/vm.c` 中修改 fork 时的相关流程。首先在文件开头引入外部全局变量 `mem_count` 和 `mem_count_lock`（实际上这两个外部变量的声明可以放在 `kernel/defs.h` 里面，但是不是很想大改这些结构，就还是先在用到的文件里引入吧）。

看一下 `fork()` 的主要逻辑，就是靠 `uvmcopy()` 函数对父进程的页表和内存内容进行拷贝的，这里直接去掉对内存内容拷贝的部分，将新进程的页表映射到父进程的物理内存即可。此外，如果父进程原页表项是可写的，就对其可写位复位，并置位 RSW 的低位（可以在 `kernel/riscv.h` 中加一条 `#define PTE_RSW_L (1L << 8)` 的宏定义方便操作）作为 COW 页的标记。原先只读的页不做权限 flags 的修改，方便区分。修改后子进程页表项的 flags 可以直接继承父进程页表项的 flags。当然都得做物理页引用计数加 1 的操作。另外记得如果后面出错跳转到 `err` 的时候根据 `PTE_RSW_L` 位改回原来的权限。

`uvmcopy()` 的具体代码如下：

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  // char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    if(*pte & PTE_W){
      *pte &= ~PTE_W;
      *pte |= PTE_RSW_L;
    }
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    acquire(&mem_count_lock);
    mem_count[pa >> 12] += 1;
    release(&mem_count_lock);
    // if((mem = kalloc()) == 0)
    //   goto err;
    // memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      // kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    if(*pte & PTE_RSW_L){
      *pte |= PTE_W;
      *pte &= ~PTE_RSW_L;
    }
  }
  return -1;
}
```

现在来到 `kernel/trap.c` 添加写入 COW 页时发生页错误的处理。首先也是在文件开头加上对外部变量的声明。随后在 `usertrap()` 中的中断原因判断流程中添加对 0xf 中断的处理，即加上以下两行：

```c
else if(r_scause() == 0xf)
    store_fault_handler();
```

随后添加 `store_fault_handler()` 函数用来处理异常。这个函数只在这个文件中使用，可以设置为静态函数。

出错的虚存地址会被保存在 `stval` 寄存器中，可以使用 `r_stval()` 读取。首先判断出错的虚存地址是否超过 `MAXVA`，如果超过则杀死进程。

随后使用 `walk()` 函数读取 PTE，判断 PTE 是否存在并且有效。如果 PTE 不存在，`walk()` 函数会返回 0，此时试图去读取会引发 load page fault。如果页表项无效也可以直接 panic，这两个逻辑可以直接从 `uvmcopy()` 中抄来（但是实际一想，页表项无效以后可能是由 swap 或者 lazy load 导致的，不过这个倒时候再处理也不迟，现在先 panic）。

随后判断 `PTE_RSW_L` 位是否置位，只有在其置位的时候才表明这是原先可写的页，由于 COW 的原因才导致只读的。如果未置位，则表明进程确实写到了不该写的地方，直接杀死进程。

随后的逻辑可以参考原来的 `uvmcopy()`，实际上就是将 `uvmcopy()` 中的物理内存的复制延后到了 COW 的异常处理中进行。

判断当前物理页的引用计数是否为 1，如果是，则表明共享这个页的所有其他进程都已经完成了自己的写时复制，这是最后一个对这个页进行写的进程，可以直接修改 PTE 中的标志位，将它改为可写即可。如果此时仍然选择复制到新物理页，则会导致原物理页永远不会被释放（释放进程时也是根据最后的进程的页表来释放物理内存的），导致内存泄漏，过不了 cowtest 的 three 测试。

否则表明还有其他进程也在使用这个页，则需要自己复制一份页的内容，将自己的页表项映射到新的物理页上，同时对老物理页的引用计数减 1。新物理页的页表项权限可写，并复位 `PTE_RSW_L` 位。

完整的 `store_fault_handler()` 如下：

```c
static void store_fault_handler() {
  struct proc *p = myproc();
  pagetable_t pt = p->pagetable;
  uint64 stval = r_stval();

  if(stval >= MAXVA) {
    setkilled(p);
    return;
  }
  
  pte_t *pte;
  if ((pte = walk(pt, stval, 0)) == 0)
    panic("store_fault_handler: pte should exist");
  if ((*pte & PTE_V) == 0)
    panic("store_fault_handler: page not present");

  if (!(*pte & PTE_RSW_L)) {
    setkilled(p);
    return;
  }

  uint64 pa;
  uint flags;
  char *mem;

  pa = PTE2PA(*pte);
  flags = PTE_FLAGS(*pte);

  acquire(&mem_count_lock);
  if (mem_count[pa >> 12] == 1) {
    release(&mem_count_lock);
    *pte = PA2PTE(pa) | flags | (PTE_W & ~PTE_RSW_L);
    return;
  } else {
    mem_count[pa >> 12] -= 1;
    release(&mem_count_lock);
  }

  if ((mem = kalloc()) == 0) {
    setkilled(p);
    return;
  }

  memmove(mem, (char *)pa, PGSIZE);
  *pte = PA2PTE(mem) | flags | (PTE_W & ~PTE_RSW_L);
}
```

最后还有 `copyout()` 函数需要修改，这个函数是在内核中向进程空间些内容的工具，影响 cowtest 的 file 测试。因为内核的页表中没有对用户进程的内存空间的映射，因此 `copyout()` 函数是直接读取进程的页表获得虚拟地址对应的物理地址的，并直接将内容拷贝到该物理页上。这一系列操作都不经过 MMU 针对 PTE 的权限检查并通过异常处理程序来解决 COW 页的共享问题，因此需要我们手动来实现这一个检查逻辑，不然可能会破坏已共享的物理页。

函数的整体逻辑就不去重构了（后来感觉直接重构可能更优雅，可以精简很多代码），只是在获取物理地址之前做一次 COW 的判断。

在使用 `walk()` 找到 PTE 之前需要先判断 `va0 >= MAXVA`，因为 `walk()` 对 `va0 >= MAXVA` 的处理是直接 panic，而看 `copyout()` 原来的结构，使用的是 `walkaddr()` 获取物理地址，`walkaddr()` 在调用 `walk()` 之前判断了 `va >= MAXVA` 的情况，并对此返回 0，随后 `copyout()` 检测到返回值为 0 时返回 -1，因此我们也沿用这套逻辑，`if(va0 >= MAXVA) return -1;`，避免被 `walk()` 给 panic 了。

后续的逻辑和 `store_fault_handler()` 类似，检测物理页引用计数是否为 1，为 1 则直接加上可写标记，去掉 `PTE_RSW_L` 标记，否则分配新的物理页并修改原页表指向新的物理页，减去原物理地址的引用计数。修改后的函数如下：

```c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);

    if(va0 >= MAXVA) return -1;
    pte_t *pte = walk(pagetable, va0, 0);
    if (pte && (*pte & PTE_V) && (*pte & PTE_RSW_L)) {
      char *mem;
      uint64 pa;
      uint flags;

      pa = PTE2PA(*pte);
      flags = PTE_FLAGS(*pte);

      acquire(&mem_count_lock);
      if (mem_count[pa >> 12] == 1) {
        release(&mem_count_lock);
        *pte = PA2PTE(pa) | flags | (PTE_W & ~PTE_RSW_L);
      } else {
        mem_count[pa >> 12] -= 1;
        release(&mem_count_lock);
        if ((mem = kalloc()) == 0) {
          return -1;
        }
        memmove(mem, (char *)pa, PGSIZE);
        *pte = PA2PTE(mem) | flags | (PTE_W & ~PTE_RSW_L);
      }
    }

    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

注意测试 `usertests -q`：

- 如果在 `copyout()` 中 `walk()` 前不判断 `va >= MAXVA`，那可能会在满足条件时直接在 `walk()` 函数中被 panic。
- 如果在 `copyout()` 中 `walk()` 后不判断 PTE 的有效性或是否存在，那么你可能会在 copyout 测试中遇到 panic，panic 的原因是读到了没有页表项的无效页，此时 `walk()` 返回 0，而 S 态试图访问 0 地址就是 panic。
- 如果在 `store_fault_handler()` 中没有判断 `stval >= MAXVA` 并杀死进程，那么就无法通过 MAXVAplus 测试。

另外就是 `usertests -q` 运行时可能会有一堆 unexpected 中断，这些都是正常的，只要没有 panic 或者 failed 就行了。
