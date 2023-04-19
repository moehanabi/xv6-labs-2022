# Uthread: switching between threads ([moderate](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> In this exercise you will design the context switch mechanism for a user-level threading system, and then implement it. To get you started, your xv6 has two files user/uthread.c and user/uthread_switch.S, and a rule in the Makefile to build a uthread program. uthread.c contains most of a user-level threading package, and code for three simple test threads. The threading package is missing some of the code to create a thread and to switch between threads.
>
> > Your job is to come up with a plan to create threads and save/restore registers to switch between threads, and implement that plan. When you're done, `make grade` should say that your solution passes the `uthread` test.
>
> Once you've finished, you should see the following output when you run `uthread` on xv6 (the three threads might start in a different order):
>
> ```
> $ make qemu
> ...
> $ uthread
> thread_a started
> thread_b started
> thread_c started
> thread_c 0
> thread_a 0
> thread_b 0
> thread_c 1
> thread_a 1
> thread_b 1
> ...
> thread_c 99
> thread_a 99
> thread_b 99
> thread_c: exit after 100
> thread_a: exit after 100
> thread_b: exit after 100
> thread_schedule: no runnable threads
> $
> ```
>
> This output comes from the three test threads, each of which has a loop that prints a line and then yields the CPU to the other threads.
>
> At this point, however, with no context switch code, you'll see no output.
>
> You will need to add code to `thread_create()` and `thread_schedule()` in `user/uthread.c`, and `thread_switch` in `user/uthread_switch.S`. One goal is ensure that when `thread_schedule()` runs a given thread for the first time, the thread executes the function passed to `thread_create()`, on its own stack. Another goal is to ensure that `thread_switch` saves the registers of the thread being switched away from, restores the registers of the thread being switched to, and returns to the point in the latter thread's instructions where it last left off. You will have to decide where to save/restore registers; modifying `struct thread` to hold registers is a good plan. You'll need to add a call to `thread_switch` in `thread_schedule`; you can pass whatever arguments you need to `thread_switch`, but the intent is to switch from thread `t` to `next_thread`.
>
> Some hints:
>
> - `thread_switch` needs to save/restore only the callee-save registers. Why?
>
> - You can see the assembly code for uthread in user/uthread.asm, which may be handy for debugging.
>
> - To test your code it might be helpful to single step through your `thread_switch` using `riscv64-linux-gnu-gdb`.  You can get started in this way:
>
>   ```
>   (gdb) file user/_uthread
>   Reading symbols from user/_uthread...
>   (gdb) b uthread.c:60
>   ```
>
>   This sets a breakpoint at line 60 of `uthread.c`. The breakpoint may (or may not) be triggered before you even run `uthread`. How could that happen?
>
>   Once your xv6 shell runs, type "uthread", and gdb will break at line 60. If you hit the breakpoint from another process, keep going until you hit the breakpoint in the `uthread` process. Now you can type commands like the following to inspect the state of `uthread`:
>   
>   ```
>   (gdb) p/x *next_thread
>   ```
>
>   With "x", you can examine the content of a memory location:
>
>   ```
>   (gdb) x/x next_thread->stack
>   ```
>   
>   You can skip to the start of `thread_switch` thus:
>
>   ```
>   (gdb) b thread_switch
>   (gdb) c
>   ```
>   
>   You can single step assembly instructions using:
>   
>   ```
>   (gdb) si
>   ```
>   
>   On-line documentation for gdb is [here](https://sourceware.org/gdb/current/onlinedocs/gdb/).
>


在开始动手之前建议先看一下整个 `uthread.c` 的逻辑，想当然地去做会出现各种问题。根据代码逻辑：

- `struct thread` 是 TCB 的数据结构，所有 TCB 存在全局数组 `all_thread[]` 中，并有 `*current_thread` 全局变量指向当前执行中的线程。
- 根据 TCB 的设计，每个线程有自己单独的栈在 TCB 中。
- 首先使用 `thread_init()` 初始化主线程，主线程号为 0，并设置状态为 `RUNNING`，使开始线程调度(`thread_schedule()`)后的主线程在子线程结束全部之前永远不会被调度。
- 随后使用 `thread_create()` 查找状态为 `FREE` 的线程并分配给新线程。
- 最后主线程调用 `thread_schedule()` 开始线程调度。
- `thread_schedule()` 是主要的调度函数
  - 从当前线程往下一个线程开始顺序查找 `RUNNABLE` 线程的，并且找到末尾后会从头开始查找，直到找完整个数组。
  - `RUNNING` 和 `FREE` 的线程都不会被调度。
  - 如果没有找到 `RUNNABLE` 线程，则 `exit(-1)` **直接退出整个程序**，并不会退回主线程继续执行。
  - 如果找到了新的线程，切换代码需要我们补充。
  - 如果找到的 `RUNNABLE` 线程是当前线程本身，则退出函数，重新回到原线程调用 `thread_yield()` 之后的上下文继续执行（相当于 `thread_yield()` do nothing）。
- 各个线程需要自行调用 `thread_yield()` 以释放 CPU 以调度其他线程，且需要在结束后自行设置自己的状态为 `FREE` 并自行调用 `thread_schedule()` 调度其他线程。
- 调用 `thread_yield()` 释放 CPU 时会将当前线程设置为 `RUNNABLE` 以便下次再被调用。
- 根据上面描述，只有 `RUNNING` 线程既不会被调度，也不会被重新分配，因此可以将主线程设置为 `RUNNING`，保证其不会在有其他线程可用的情况下被调度。

根据上述描述：

- 不要在 `thread_create(void (*func)())` 中调度线程，因为主线程会在创建完所有线程之后再开始调度，不然无法回到主线程就无法继续创建其他线程，或者需要在子线程中创建新线程。
- 不要在 `thread_schedule()` 中设置当前线程状态为 `RUNNABLE`，不然会把主线程给转成 `RUNNABLE` 导致调度回主线程，然后就会走到主线程的 `exit(0)` 退出。还可能把 `current_thread->state = FREE` 的线程设置为 `RUNNABLE` 导致执行错误。

另外需要注意，栈由高地址往低地址增长，因此各线程栈的起点不是 `struct thread->stack[0]` 而是 `struct thread->stack[STACK_SIZE-1]`。

了解完所有逻辑并且能避免踩坑之后，设计不算太难。根据代码中的注释，需要在选择新的线程之后调用 `thread_switch()` 来从 `t` 线程切换到 `next_thread` 线程，而线程切换和进程切换一样，切换的其实就是上下文。由于用户态线程共用同一个进程的大多数内容，也不需要切换页表。因此仿照进程管理的内容：

- 在 TCB 中添加 `context` 域用于存放进程上下文，数据结构定义可以完全照抄 PCB 的 `struct context`
- `thread_switch()` 函数（位于 `user/uthread_switch.S` 可以完全照抄内核切换进程的 `swtch()` 函数（位于 `kernel/swtch.S`）

此外，在线程创建时需要设置一部分上下文的值：

- `ra`：要执行的函数的地址
- `sp`：栈顶

最终代码如下：

```c
// user/uthread.c
struct thread_context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct thread_context context;
};

void 
thread_schedule(void)
{
  // ......
  // ......

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    thread_switch((uint64)&t->context, (uint64)&current_thread->context);
  } else
    next_thread = 0;
}

void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
  t->context.ra = (uint64)func;
  t->context.sp = (uint64)&t->stack[STACK_SIZE-1];
}
```

```assembly
// user/uthread_switch.S
thread_switch:
	/* YOUR CODE HERE */
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)

	ret    /* return to ra */
```

可以参考 traps 实验中的文档，RISC-V ABI 约定了由 caller 和 callee 保存的寄存器，而 `thread_switch()`（以及 `swtch()`）是被作为普通的函数调用调用的（只是这个函数恰好有切换线程的功能），因此只需要保存 callee 需要保存的寄存器就行了，其余的寄存器由编译器自动在 caller 处保存到栈上了，等到时候切换回来的时候也会把 `sp` 一起切过来，就可以重新从栈上弹出那些寄存器了。（额外）保存 `ra` 和 `sp` 也是为了方便回到原来的执行流。

中断发生时需要保存所有寄存器，是因为中断发生不是一个普通的函数调用，并没有人能预料到中断什么时候发生，并提前帮助保存所有 caller 需要保存的寄存器，因此只能保存所有寄存器以便从中断中返回时恢复完整的状态。

在调试用户态程序的时候，因为多个进程共用同样的地址空间布局，而 GDB 打断点是靠 `pc` 执行到某个地址判断的，因此如果断点位置处多个用户态进程都有能执行到的代码，则有可能 GDB 会断在别的进程的相同地址处。

# Using threads ([moderate](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> In this assignment you will explore parallel programming with threads and locks using a hash table. You should do this assignment on a real Linux or MacOS computer (not xv6, not qemu) that has multiple cores. Most recent laptops have multicore processors.
>
> This assignment uses the UNIX `pthread` threading library. You can find information about it from the manual page, with `man pthreads`, and you can look on the web, for example [here](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_mutex_lock.html), [here](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_mutex_init.html), and [here](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_create.html).
>
> The file `notxv6/ph.c` contains a simple hash table that is correct if used from a single thread, but incorrect when used from multiple threads. In your main xv6 directory (perhaps `~/xv6-labs-2021`), type this:
>
> ```shell
> $ make ph
> $ ./ph 1
> ```
>
> Note that to build `ph` the Makefile uses your OS's gcc, not the 6.S081 tools. The argument to `ph` specifies the number of threads that execute put and get operations on the the hash table. After running for a little while, `ph 1` will produce output similar to this:
>
> ```
> 100000 puts, 3.991 seconds, 25056 puts/second
> 0: 0 keys missing
> 100000 gets, 3.981 seconds, 25118 gets/second
> ```
>
> The numbers you see may differ from this sample output by a factor of two or more, depending on how fast your computer is, whether it has multiple cores, and whether it's busy doing other things.
>
> `ph` runs two benchmarks. First it adds lots of keys to the hash table by calling `put()`, and prints the achieved rate in puts per second. Then it fetches keys from the hash table with `get()`. It prints the number keys that should have been in the hash table as a result of the puts but are missing (zero in this case), and it prints the number of gets per second it achieved.
>
> You can tell `ph` to use its hash table from multiple threads at the same time by giving it an argument greater than one. Try `ph 2`:
>
> ```
> $ ./ph 2
> 100000 puts, 1.885 seconds, 53044 puts/second
> 1: 16579 keys missing
> 0: 16579 keys missing
> 200000 gets, 4.322 seconds, 46274 gets/second
> ```
>
> The first line of this `ph 2` output indicates that when two threads concurrently add entries to the hash table, they achieve a total rate of 53,044 inserts per second. That's about twice the rate of the single thread from running `ph 1`. That's an excellent "parallel speedup" of about 2x, as much as one could possibly hope for (i.e. twice as many cores yielding twice as much work per unit time).
>
> However, the two lines saying `16579 keys missing` indicate that a large number of keys that should have been in the hash table are not there. That is, the puts were supposed to add those keys to the hash table, but something went wrong. Have a look at `notxv6/ph.c`, particularly at `put()` and `insert()`.
>
> > Why are there missing keys with 2 threads, but not with 1 thread? Identify a sequence of events with 2 threads that can lead to a key being missing. Submit your sequence with a short explanation in answers-thread.txt
> >
> > To avoid this sequence of events, insert lock and unlock statements in `put` and `get` in `notxv6/ph.c` so that the number of keys missing is always 0 with two threads. The relevant pthread calls are:
> >
> > ```
> > pthread_mutex_t lock;            // declare a lock
> > pthread_mutex_init(&lock, NULL); // initialize the lock
> > pthread_mutex_lock(&lock);       // acquire lock
> > pthread_mutex_unlock(&lock);     // release lock
> > ```
> >
> > You're done when `make grade` says that your code passes the `ph_safe` test, which requires zero missing keys with two threads. It's OK at this point to fail the `ph_fast` test.
> >
>
> Don't forget to call `pthread_mutex_init()`. Test your code first with 1 thread, then test it with 2 threads. Is it correct (i.e. have you eliminated missing keys?)? Does the two-threaded version achieve parallel speedup (i.e. more total work per unit time) relative to the single-threaded version?
>
> There are situations where concurrent `put()`s have no overlap in the memory they read or write in the hash table, and thus don't need a lock to protect against each other. Can you change `ph.c` to take advantage of such situations to obtain parallel speedup for some `put()`s? Hint: how about a lock per hash bucket?
>
> > Modify your code so that some `put` operations run in parallel while maintaining correctness. You're done when `make grade` says your code passes both the `ph_safe` and `ph_fast` tests. The `ph_fast` test requires that two threads yield at least 1.25 times as many puts/second as one thread.
> >

整个程序的逻辑是先初始化 NKEYS 个数的数组，随后分 n 个线程将自己那一块的数组的值依次与自己的线程号组成 K-V 对加入到哈希表中。完成哈希表写入后再创建 n 个线程用所有的 K 读取哈希表，检测对应的 K-V 对是否存在。现在多线程写入的时候会出问题，一部分的哈希表项创建会失败。

哈希表有 NBUCKET 个 hash bucket，以链表形式存放于 `table[]` 中。`put()` 函数会检测 K 值对应条目是否已存在，若存在则修改已有值为新的值，否则通过 `insert()` 创建新的 K-V 对并插入 `table[i]` 的开头。

问题出在 `put()` 函数与 `insert()` 函数上，线程 A 的 `put()` 函数在检测完之后发现没有条目，因此准备调用 `insert()` 插入，此时线程 B 也想用 `insert()` 插入同 bucket 不同 K 的条目，假如两者都同时执行完了 `e->next = n`，此时两者手上准备好的链分别是：

- 线程 A：A-new -> old-1 -> old-2 -> ...
- 线程 B：B-new -> old-1 -> old-2 -> ...

两个线程先后执行 `*p = e`，用自己的链替换当前 bucket，先替换的线程 A 的新创建的条目就被丢弃了。其问题就出在线程 B 无法获得线程 A 创建了新条目的信息。

因此就需要有锁来锁住 bucket 的读写。

提示给了几个锁相关的函数与类型，使用方法一眼就能看出来，就不细说了：

```c
pthread_mutex_t lock;            // declare a lock
pthread_mutex_init(&lock, NULL); // initialize the lock
pthread_mutex_lock(&lock);       // acquire lock
pthread_mutex_unlock(&lock);     // release lock
```

一种加锁的方式是给整个 `put()` 函数加一把锁，代码如下：

```c
static 
void put(int key, int value)
{
  int i = key % NBUCKET;

  // is the key already present?
  pthread_mutex_lock(&lock);
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&lock);
}
```

这种方式是可行的，但是会有性能损失，相当于退化成了单线程的顺序执行，而且由于多线程切换与锁的开销，效率会比单线程更低。

另一种加锁方式是对发生竞争的 `insert()` 函数加锁，注意不能加在 `insert()` 函数内，因为进入 `insert()` 函数之后，新链就已经确定了，两线程同时进入函数后依然会出现上述问题，加锁无效。需要在获取当前 bucket 链之前加锁，这样才能使线程 A 新加的节点被线程 B 获得。代码如下：

```c
static 
void put(int key, int value)
{
  int i = key % NBUCKET;

  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    pthread_mutex_lock(&lock);
    insert(key, value, &table[i], table[i]);
    pthread_mutex_unlock(&lock);
  }

}
```

可以过测！看似很美好，效率也比单线程高得多，实际会产生新的问题。回到上面的场景，假设线程 A 与线程 B 同时要加相同 K 值的条目（比如线程 A 要加 `100, 1`，线程 B 要加 `100, 2`），并且同时检查发现条目不存在，都来到了锁前，线程 A 先加，线程 B 等线程 A 加完之后也会再加条目，事实上这和哈希表的设定冲突了。按照代码设计的哈希表本不应该出现相同 K 值的两个条目。这大概是测试代码没有考虑到的问题，可以按照下面修改 `main()` 函数，多跑几次，总能发现有相同 K 的条目。问题事实上也是因为线程 B 获取信息滞后了，线程 B 在插入时能获得线程 A 创建了新条目的信息，但是检测时还无法获得。

```c
int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  double t1, t0;


  if (argc < 2) {
    fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);
  assert(NKEYS % nthread == 0);
  for (int i = 0; i < NKEYS; i++) {
    keys[i] = random()%100;
  }

  //
  // first the puts
  //
  t0 = now();
  pthread_mutex_init(&lock, NULL);
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         NKEYS, t1 - t0, NKEYS / (t1 - t0));

  struct entry *tmp;
  for (int i = 0; i < NBUCKET; i++) {
    for (tmp = table[i]; tmp; tmp = tmp->next) {
      printf("%d %d %d\n", i, tmp->key,tmp->value);
    }
  }
}
```

因此一种合理的解法是给每个 bucket 单独一把锁，并从检测到插入全程加锁，避免上述问题。由此可以看出 bucket 数量越多，遇到被上锁的 bucket 的概率越小，性能越高：

```c
// ......

struct entry *table[NBUCKET];
int keys[NKEYS];
int nthread = 1;
pthread_mutex_t lock[NBUCKET];

// ......

static 
void put(int key, int value)
{
  int i = key % NBUCKET;

  // is the key already present?
  struct entry *e = 0;
  pthread_mutex_lock(&lock[i]);
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&lock[i]);
}

// ......

int
main(int argc, char *argv[])
{
  // ......

  //
  // first the puts
  //
  t0 = now();
  for (int i = 0; i < NBUCKET; i++) {
    pthread_mutex_init(&lock[i], NULL);
  }
  
  for (int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  // ......
}
```

# Barrier([moderate](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> In this assignment you'll implement a [barrier](http://en.wikipedia.org/wiki/Barrier_(computer_science)): a point in an application at which all participating threads must wait until all other participating threads reach that point too. You'll use pthread condition variables, which are a sequence coordination technique similar to xv6's sleep and wakeup.
>
> You should do this assignment on a real computer (not xv6, not qemu).
>
> The file `notxv6/barrier.c` contains a broken barrier.
>
> ```
> $ make barrier
> $ ./barrier 2
> barrier: notxv6/barrier.c:42: thread: Assertion `i == t' failed.
> ```
>
> The 2 specifies the number of threads that synchronize on the barrier ( `nthread` in `barrier.c`). Each thread executes a loop. In each loop iteration a thread calls `barrier()` and then sleeps for a random number of microseconds. The assert triggers, because one thread leaves the barrier before the other thread has reached the barrier. The desired behavior is that each thread blocks in `barrier()` until all `nthreads` of them have called `barrier()`.
>
> > Your goal is to achieve the desired barrier behavior. In addition to the lock primitives that you have seen in the `ph` assignment, you will need the following new pthread primitives; look [here](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_cond_wait.html) and [here](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_cond_broadcast.html) for details.
> >
> > ```
> > pthread_cond_wait(&cond, &mutex);  // go to sleep on cond, releasing lock mutex, acquiring upon wake up
> > pthread_cond_broadcast(&cond);     // wake up every thread sleeping on cond
> > ```
> >
> > Make sure your solution passes `make grade`'s `barrier` test.
> >
>
> `pthread_cond_wait` releases the `mutex` when called, and re-acquires the `mutex` before returning.
>
> We have given you `barrier_init()`. Your job is to implement `barrier()` so that the panic doesn't occur. We've defined `struct barrier` for you; its fields are for your use.
>
> There are two issues that complicate your task:
>
> - You have to deal with a succession of barrier calls, each of which we'll call a round. `bstate.round` records the current round. You should increment `bstate.round` each time all threads have reached the barrier.
> - You have to handle the case in which one thread races around the loop before the others have exited the barrier. In particular, you are re-using the `bstate.nthread` variable from one round to the next. Make sure that a thread that leaves the barrier and races around the loop doesn't increase `bstate.nthread` while a previous round is still using it.
>
> Test your code with one, two, and more than two threads.
