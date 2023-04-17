# RISC-V assembly ([easy](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> It will be important to understand a bit of RISC-V assembly, which you were exposed to in 6.1910 (6.004). There is a file `user/call.c` in your xv6 repo. make fs.img compiles it and also produces a readable assembly version of the program in `user/call.asm`.
>
> Read the code in call.asm for the functions `g`, `f`, and `main`. The instruction manual for RISC-V is on the [reference page](https://pdos.csail.mit.edu/6.828/2022/reference.html). Here are some questions that you should answer (store the answers in a file answers-traps.txt): 
>
> > Which registers contain arguments to functions? For example, which register holds 13 in main's call to `printf`?

在 `printf` 下面可以看到三个传参的寄存器，分别是 `a2`、`a1` 和 `a0`，如果不看汇编代码的话也可以去看一下 [RISC-V 官方的 ABI 文档](https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-cc.adoc)

> > Where is the call to function `f` in the assembly code for main? Where is the call to `g`? (Hint: the compiler may inline functions.)

调用 `f` 是没有的，`26:	45b1 li	a1,12` 直接把 `f` 给算出来了可还行，编译器优化做得还真不错。调用 `g` 也没有，而是直接做了内联(inline)，`14: 250d addiw	a0,a0,3`。

> > At what address is the function `printf` located?

0x642

> > What value is in the register `ra` just after the `jalr` to `printf` in `main`?

`ra` 是函数调用完后的返回地址，0x38

> > Run the following code.
> >
> > ```c
> > unsigned int i = 0x00646c72;
> > printf("H%x Wo%s", 57616, &i);
> > ```
> >
> > What is the output? [Here's an ASCII table](https://www.asciitable.com/) that maps bytes to characters.

输出了 "HE110 World"

> > The output depends on that fact that the RISC-V is little-endian. If the RISC-V were instead big-endian what would you set `i` to in order to yield the same output? Would you need to change `57616` to a different value?

57616 不需要改，因为这是 0xE110 的十进制形式，`%x` 是以十六进制打印的意思。`i` 需要改成 0x726c6400

> > [Here's a description of little- and big-endian](http://www.webopedia.com/TERM/b/big_endian.html) and [a more whimsical description](https://www.rfc-editor.org/ien/ien137.txt). 
> >
> > In the following code, what is going to be printed after `'y='`? (note: the answer is not a specific value.) Why does this happen?
> >
> > ```c
> > printf("x=%d y=%d", 3);
> > ```

会打印一些不确定的东西，`printf` 需要三个寄存器来传参，但现在只有 `a1` 和 `a0` 被赋值了有意义的内容，进入 `printf` 后会保存 `a1`-`a7` 的值到栈上，同时根据 `a0` 的值输出相应个数的内容，此时 `y` 所对应的 `a2` 的值无意义。

# Backtrace ([moderate](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> For debugging it is often useful to have a backtrace: a list of the function calls on the stack above the point at which the error occurred. To help with backtraces, the compiler generates machine code that maintains a stack frame on the stack corresponding to each function in the current call chain. Each stack frame consists of the return address and a "frame pointer" to the caller's stack frame. Register `s0` contains a pointer to the current stack frame (it actually points to the the address of the saved return address on the stack plus 8). Your `backtrace` should use the frame pointers to walk up the stack and print the saved return address in each stack frame.
>
> > Implement a `backtrace()` function in `kernel/printf.c`. Insert a call to this function in `sys_sleep`, and then run bttest, which calls `sys_sleep`. Your output should be a list of return addresses with this form (but the numbers will likely be different):
> >
> > ```
> > backtrace:
> > 0x0000000080002cda
> > 0x0000000080002bb6
> > 0x0000000080002898
> > ```
> >
> > After `bttest` exit qemu. In a terminal window: run `addr2line -e kernel/kernel` (or `riscv64-unknown-elf-addr2line -e kernel/kernel`) and cut-and-paste the addresses from your backtrace, like this:
> >
> > ```
> > $ addr2line -e kernel/kernel
> > 0x0000000080002de2
> > 0x0000000080002f4a
> > 0x0000000080002bfc
> > Ctrl-D
> > ```
> >
> > You should see something like this:
> >
> > ```
> > kernel/sysproc.c:74
> > kernel/syscall.c:224
> > kernel/trap.c:85
> > ```
>
> Some hints: 
>
> - Add the prototype for your `backtrace()` to `kernel/defs.h` so that you can invoke `backtrace` in `sys_sleep`. 
>
> - The GCC compiler stores the frame pointer of the currently executing function in the register `s0`. Add the following function to `kernel/riscv.h`: 
>
>   ```c
>   static inline uint64
>   r_fp()
>   {
>       uint64 x;
>       asm volatile("mv %0, s0" : "=r" (x) );
>       return x;
>   }
>   ```
>   
>   and call this function in `backtrace` to read the current frame pointer. `r_fp()` uses [in-line assembly](https://gcc.gnu.org/onlinedocs/gcc/Using-Assembly-Language-with-C.html) to read `s0`. 
> 
> - These [lecture notes](https://pdos.csail.mit.edu/6.1810/2022/lec/l-riscv.txt) have a picture of the layout of stack frames. Note that the return address lives at a fixed offset (-8) from the frame pointer of a stackframe, and that the saved frame pointer lives at fixed offset (-16) from the frame pointer. 
> 
> - Your `backtrace()` will need a way to recognize that it has seen the last stack frame, and should stop. A useful fact is that the memory allocated for each kernel stack consists of a single page-aligned page, so that all the stack frames for a given stack are on the same page. You can use `PGROUNDDOWN(fp)` (see `kernel/riscv.h`) to identify the page that a frame pointer refers to. 
> 
> Once your backtrace is working, call it from `panic` in `kernel/printf.c` so that you see the kernel's backtrace when it panics.

看了一下提示，发现源代码里连 `r_fp()` 都没给，但是提示里又给了，这是何必呢…… 直接放进去不好吗……

还是先搭好框架：在 `kernel/printf.c` 加 `backtrace()` 函数，在 `kernel/defs.h` 添加函数声明，然后在 `sys_sleep()` 合适的位置调用，我选择在获取参数之后就调用（比较快一些？）。

函数内容嘛，根据提示，需要从栈帧中获取到相应的参数，然后再层层递归挖下去，一直挖到最底部的栈帧。根据提示，进程的内核栈内容都在同一个页内，可以使用 `PGROUNDDOWN(fp)` 来判断当前栈帧指针所指向的页面的起始地址，那么如果挖到了另一个页里去了就说明已经挖完了，可以结束了。

https://pdos.csail.mit.edu/6.1810/2022/lec/l-riscv.txt 里有 stack frame layout，但是一眼有问题。栈是从高地址往低地址增长的，`sp` 不会是在栈底，不过也有可能是我没看懂。去找了一下`fp` 会指向当前帧的起始位置，当前帧往前一个单位(8 bytes)是本次调用的返回地址，再往前一个单位是上一个帧指针 `fp` 的值，以此类推，可以回溯到最初的函数。

先不考虑终止条件，验证一下取值的逻辑：

```c
void backtrace(void) {
  uint64 fp_now = r_fp();
  uint64 ra;
  while (1) {
    ra = *((uint64 *)(fp_now)-1);
    fp_now = *((uint64 *)(fp_now)-2);
    printf("%p\n", ra);
  }
}
```

效果如下：

```
$ bttest
0x0000000080002138
0x000000008000201c
0x0000000080001d12
0x0000000000000012
scause 0x000000000000000d
sepc=0x0000000080005e1c stval=0x0000000000003fc8
panic: kerneltrap
QEMU: Terminated

$ riscv64-unknown-elf-addr2line -e kernel/kernel
0x0000000080002138
kernel/sysproc.c:61
0x000000008000201c
kernel/syscall.c:141
0x0000000080001d12
kernel/trap.c:76
```

看起来很不错，就是代码行号出了点问题。问题不大，先不管，先把终止条件的判断加上。事先算一下当前的内核栈页起始地址，然后算一下每次迭代后判断新的栈帧起始位置是否为同一个页即可（当然如果看了上一篇的文档的开头部分，可以发现其实也可以和 `p->kstack` 比较，前提是已经有进程 p 了的话）：

```c
void backtrace(void) {
  uint64 ra;
  uint64 fp_now = r_fp();
  uint64 page_begin = PGROUNDDOWN(fp_now);
  while (PGROUNDDOWN(fp_now) == page_begin) {
    ra = *((uint64 *)(fp_now)-1);
    fp_now = *((uint64 *)(fp_now)-2);
    printf("%p\n", ra);
  }
}
```

测试效果如下：

```
$ bttest
0x00000000800021aa
0x000000008000201c
0x0000000080001d12
```

看起来很不错，把它加到 `panic()` 的适当位置。

关于代码行号的问题，仔细一想现在输出的是返回地址，但是返回地址不等于调用发生时的地址。那就给输出的 `ra` 再减去一条指令的长（8 byte）即可：

```c
void backtrace(void) {
  uint64 ra;
  uint64 fp_now = r_fp();
  uint64 page_begin = PGROUNDDOWN(fp_now);
  while (PGROUNDDOWN(fp_now) == page_begin) {
    ra = *((uint64 *)(fp_now)-1) - 8;
    fp_now = *((uint64 *)(fp_now)-2);
    printf("%p\n", ra);
  }
}
```

测试如下：

```
$ bttest
0x0000000080002130
0x0000000080002014
0x0000000080001d0a
$ QEMU: Terminated
yu@Yu:~/xv6-labs-2022-hanabi$ addr2line -e kernel/kernel
0x0000000080002130
kernel/sysproc.c:59
0x0000000080002014
kernel/syscall.c:138 (discriminator 1)
0x0000000080001d0a
kernel/trap.c:67
```

好吧，其实还是不是很准，回溯的第二条出了些问题，调用函数定位到条件判断里了。事实上在遇到分支这种情况的时候确实不好直接减一条指令，毕竟你也不知道分支跳来跳去执行完后返回地址会是在调用者的哪里。risc-v 的 `call` 伪指令的实现是 `auipc` + `jalr` 的组合，会自动将当前 `pc+8`（+8 是因为 RISC-V 的指令都是定长 32 bit 的，两条指令就是 64 bit，8 byte。实际上其实是 `pc+4` 走到 `jalr`，然后 `jalr` 再将 +4 后的 `pc` 再 +4 存入 `rs`）存入一个寄存器（未指定的话就是 `ra`），所以 `ra-8` 对使用 `call` 的默认调用是有效的。

但是如果跳转的地址是由前面的指令计算得到的，后面直接使用 `jalr` 之类的跳转伪指令，可能就会出问题：

```assembly
if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
  80001e04:	37fd	addiw	a5,a5,-1
  80001e06:	4751	li	a4,20
  80001e08:	00f76e63	bltu	a4,a5,80001e24 <syscall+0x3a>
  80001e0c:	00369713	slli	a4,a3,0x3
  80001e10:	00005797	auipc	a5,0x5
  80001e14:	5c078793	addi	a5,a5,1472 # 800073d0 <syscalls>
  80001e18:	97ba	add	a5,a5,a4
  80001e1a:	639c	ld	a5,0(a5)
  80001e1c:	c781	beqz	a5,80001e24 <syscall+0x3a>
  // Use num to lookup the system call function for num, call it,
  // and store its return value in p->trapframe->a0
  p->trapframe->a0 = syscalls[num]();
  80001e1e:	9782	jalr	a5
  80001e20:	f8a8	sd	a0,112(s1)
  80001e22:	a839	j	80001e40 <syscall+0x56>
}
```

以 `kernel/syscall.c` 中 `syscall()` 的 `p->trapframe->a0 = syscalls[num]()` 为例，其汇编代码如上所示，它的跳转地址其实在条件判断里就计算好了（存在 `a5` 寄存器中），`p->trapframe->a0 = syscalls[num]()` 的实现仅仅就是 `jalr a5` 一条指令就实现了。

`jalr rs` 是 `jalr x1, 0(rs)` 的伪指令写法，`jalr x1, 0(rs)` 会将当前 `pc+4` 存入 `ra`，也即返回时跳到下一条指令。此时如果使用 `pc-8` 来定位调用的函数，就会跳过头，跳到了 `p->trapframe->a0 = syscalls[num]()` 之前的指令，也就是跳到了条件判断语句里了。

具体怎么解决我也不知道，感觉难度很大，看了一下别的系统，[rcore](http://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/8answer.html) 较为简单的答案是没有精确到调用函数的地址的，只回溯了返回地址，还有一个使用了 `libunwind` 的库的方法，看起来很高级，不知道能不能实现精确到调用函数的行号。而且我发现这个东西和 gcc 的参数也有关系，`-O0` 反而会导致回溯失败，不是很懂.jpg。

这里有两篇关于栈回溯的文章，看起来很厉害，不是很懂：

- [linux(栈回溯篇) - 知乎](https://zhuanlan.zhihu.com/p/460686470)
- [linux 栈回溯(x86_64) - 知乎](https://zhuanlan.zhihu.com/p/302726082)

# Alarm ([hard](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> > In this exercise you'll add a feature to xv6 that periodically alerts a process as it uses CPU time. This might be useful for compute-bound processes that want to limit how much CPU time they chew up, or for processes that want to compute but also want to take some periodic action. More generally, you'll be implementing a primitive form of user-level interrupt/fault handlers; you could use something similar to handle page faults in the application, for example. Your solution is correct if it passes alarmtest and 'usertests -q'
>
> You should add a new `sigalarm(interval, handler)` system call. If an application calls `sigalarm(n, fn)`, then after every `n` "ticks" of CPU time that the program consumes, the kernel should cause application function `fn` to be called. When `fn` returns, the application should resume where it left off. A tick is a fairly arbitrary unit of time in xv6, determined by how often a hardware timer generates interrupts. If an application calls `sigalarm(0, 0)`, the kernel should stop generating periodic alarm calls.
>
> You'll find a file `user/alarmtest.c` in your xv6 repository. Add it to the Makefile. It won't compile correctly until you've added `sigalarm` and `sigreturn` system calls (see below).
>
> `alarmtest` calls `sigalarm(2, periodic)` in `test0` to ask the kernel to force a call to `periodic()` every 2 ticks, and then spins for a while. You can see the assembly code for alarmtest in user/alarmtest.asm, which may be handy for debugging. Your solution is correct when `alarmtest` produces output like this and usertests -q also runs correctly:
>
> ```
> $ alarmtest
> test0 start
> ........alarm!
> test0 passed
> test1 start
> ...alarm!
> ..alarm!
> ...alarm!
> ..alarm!
> ...alarm!
> ..alarm!
> ...alarm!
> ..alarm!
> ...alarm!
> ..alarm!
> test1 passed
> test2 start
> ................alarm!
> test2 passed
> test3 start
> test3 passed
> $ usertest -q
> ...
> ALL TESTS PASSED
> $
> ```
>
> When you're done, your solution will be only a few lines of code, but it may be tricky to get it right. We'll test your code with the version of alarmtest.c in the original repository. You can modify alarmtest.c to help you debug, but make sure the original alarmtest says that all the tests pass.
>

## test0: invoke handler

> Get started by modifying the kernel to jump to the alarm handler in user space, which will cause test0 to print "alarm!". Don't worry yet what happens after the "alarm!" output; it's OK for now if your program crashes after printing "alarm!". Here are some hints:
>
> - You'll need to modify the Makefile to cause `alarmtest.c` to be compiled as an xv6 user program.
>
> - The right declarations to put in `user/user.h` are:
>
>   ```c
>   int sigalarm(int ticks, void (*handler)());
>   int sigreturn(void);
>   ```
>   
> - Update user/usys.pl (which generates user/usys.S), kernel/syscall.h, and kernel/syscall.c to allow `alarmtest` to invoke the sigalarm and  sigreturn system calls.
>
> - For now, your `sys_sigreturn` should just return zero.
>
> - Your `sys_sigalarm()` should store the alarm interval and the pointer to the handler function in new fields in the `proc` structure (in `kernel/proc.h`).
>
> - You'll need to keep track of how many ticks have passed since the last call (or are left until the next call) to a process's alarm handler; you'll need a new field in `struct proc` for this too. You can initialize `proc` fields in `allocproc()` in `proc.c`.
>
> - Every tick, the hardware clock forces an interrupt, which is handled in `usertrap()` in `kernel/trap.c`.
>
> - You only want to manipulate a process's alarm ticks if there's a timer interrupt; you want something like
>
>   ```c
>   if(which_dev == 2) ...
>   ```
>
> - Only invoke the alarm function if the process has a timer outstanding. Note that the address of the user's alarm function might be 0 (e.g., in user/alarmtest.asm, `periodic` is at address 0).
>
> - You'll need to modify `usertrap()` so that when a process's alarm interval expires, the user process executes the handler function. When a trap on the RISC-V returns to user space, what determines the instruction address at which user-space code resumes execution?
>
> - It will be easier to look at traps with gdb if you tell qemu to use only one CPU, which you can do by running
>
>   ```shell
>   make CPUS=1 qemu-gdb
>   ```
>
> - You've succeeded if alarmtest prints "alarm!".
>

## test1/test2()/test3(): resume interrupted code

> Chances are that alarmtest crashes in test0 or test1 after it prints "alarm!", or that alarmtest (eventually) prints "test1 failed", or that alarmtest exits without printing "test1 passed". To fix this, you must ensure that, when the alarm handler is done, control returns to the instruction at which the user program was originally interrupted by the timer interrupt. You must ensure that the register contents are restored to the values they held at the time of the interrupt, so that the user program can continue undisturbed after the alarm. Finally, you should "re-arm" the alarm counter after each time it goes off, so that the handler is called periodically.
>
> As a starting point, we've made a design decision for you: user alarm handlers are required to call the `sigreturn` system call when they have finished. Have a look at `periodic` in `alarmtest.c` for an example. This means that you can add code to `usertrap` and `sys_sigreturn` that cooperate to cause the user process to resume properly after it has handled the alarm.
>
> Some hints:
>
> - Your solution will require you to save and restore registers---what registers do you need to save and restore to resume the interrupted code correctly? (Hint: it will be many).
> - Have `usertrap` save enough state in `struct proc` when the timer goes off that `sigreturn` can correctly return to the interrupted user code.
> - Prevent re-entrant calls to the handler----if a handler hasn't returned yet, the kernel shouldn't call it again. `test2` tests this.
> - Make sure to restore a0. `sigreturn` is a system call, and its return value is stored in a0.
>
> Once you pass `test0`, `test1`, `test2`, and `test3` run `usertests -q` to make sure you didn't break any other parts of the kernel.