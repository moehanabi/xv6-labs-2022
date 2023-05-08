# Large files ([moderate](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> In this assignment you'll increase the maximum size of an xv6 file. Currently xv6 files are limited to 268 blocks, or 268*BSIZE bytes (BSIZE is 1024 in xv6). This limit comes from the fact that an xv6 inode contains 12 "direct" block numbers and one "singly-indirect" block number, which refers to a block that holds up to 256 more block numbers, for a total of 12+256=268 blocks.
>
> The `bigfile` command creates the longest file it can, and reports that size:
>
> ```
> $ bigfile
> ..
> wrote 268 blocks
> bigfile: file is too small
> $
> ```
>
> The test fails because `bigfile` expects to be able to create a file with 65803 blocks, but unmodified xv6 limits files to 268 blocks.
>
> You'll change the xv6 file system code to support a "doubly-indirect" block in each inode, containing 256 addresses of singly-indirect blocks, each of which can contain up to 256 addresses of data blocks. The result will be that a file will be able to consist of up to 65803 blocks, or 256*256+256+11 blocks (11 instead of 12, because we will sacrifice one of the direct block numbers for the double-indirect block).

## Preliminaries

> The `mkfs` program creates the xv6 file system disk image and determines how many total blocks the file system has; this size is controlled by `FSSIZE` in `kernel/param.h`. You'll see that `FSSIZE` in the repository for this lab is set to 200,000 blocks. You should see the following output from `mkfs/mkfs` in the make output:
>
> ```
> nmeta 70 (boot, super, log blocks 30 inode blocks 13, bitmap blocks 25) blocks 199930 total 200000
> ```
>
> This line describes the file system that `mkfs/mkfs` built: it has 70 meta-data blocks (blocks used to describe the file system) and 199,930 data blocks, totaling 200,000 blocks. 
> If at any point during the lab you find yourself having to rebuild the file system from scratch, you can run `make clean` which forces make to rebuild fs.img.

## What to Look At

> The format of an on-disk inode is defined by `struct dinode` in `fs.h`. You're particularly interested in `NDIRECT`, `NINDIRECT`, `MAXFILE`, and the `addrs[]` element of `struct dinode`. Look at Figure 8.3 in the xv6 text for a diagram of the standard xv6 inode.
>
> The code that finds a file's data on disk is in `bmap()` in `fs.c`. Have a look at it and make sure you understand what it's doing. `bmap()` is called both when reading and writing a file. When writing, `bmap()` allocates new blocks as needed to hold file content, as well as allocating an indirect block if needed to hold block addresses.
>
> `bmap()` deals with two kinds of block numbers. The `bn` argument is a "logical block number" -- a block number within the file, relative to the start of the file. The block numbers in `ip->addrs[]`, and the argument to `bread()`, are disk block numbers. You can view `bmap()` as mapping a file's logical block numbers into disk block numbers.

## Your Job

> > Modify `bmap()` so that it implements a doubly-indirect block, in addition to direct blocks and a singly-indirect block. You'll have to have only 11 direct blocks, rather than 12, to make room for your new doubly-indirect block; you're not allowed to change the size of an on-disk inode. The first 11 elements of `ip->addrs[]` should be direct blocks; the 12th should be a singly-indirect block (just like the current one); the 13th should be your new doubly-indirect block. You are done with this exercise when `bigfile` writes 65803 blocks and `usertests -q` runs successfully:
>
> ```
> $ bigfile
> ..................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................
> wrote 65803 blocks
> done; ok
> $ usertests -q
> ...
> ALL TESTS PASSED
> $ 
> ```
>
> `bigfile` will take at least a minute and a half to run.
>
> Hints:
>
> - Make sure you understand `bmap()`. Write out a diagram of the relationships between `ip->addrs[]`, the indirect block, the doubly-indirect block and the singly-indirect blocks it points to, and data blocks. Make sure you understand why adding a doubly-indirect block increases the maximum file size by 256*256 blocks (really -1, since you have to decrease the number of direct blocks by one).
> - Think about how you'll index the doubly-indirect block, and the indirect blocks it points to, with the logical block number.
> - If you change the definition of `NDIRECT`, you'll probably have to change the declaration of `addrs[]` in `struct inode` in `file.h`. Make sure that `struct inode` and `struct dinode` have the same number of elements in their `addrs[]` arrays.
> - If you change the definition of `NDIRECT`, make sure to create a new `fs.img`, since `mkfs` uses `NDIRECT` to build the file system.
> - If your file system gets into a bad state, perhaps by crashing, delete `fs.img` (do this from Unix, not xv6). `make` will build a new clean file system image for you.
> - Don't forget to `brelse()` each block that you `bread()`.
> - You should allocate indirect blocks and doubly-indirect blocks only as needed, like the original `bmap()`.
> - Make sure `itrunc` frees all blocks of a file, including double-indirect blocks.
> - `usertests` takes longer to run than in previous labs because for this lab `FSSIZE` is larger and big files are larger.

xv6 的 inode 中，由 `uint addrs[]` 存放磁盘块的索引，原始状态有了 `NDIRECT` 个直接索引和 1 个一级索引。磁盘块大小是 1KB，每个索引的大小是 `uint` 大小（32 bit），因此每个磁盘块可以放 $1KB / 4B = 256$ 个索引。

实现大文件实际上就是增加 inode 中的对磁盘块的索引数量与层级。实验要求保持 inode 结构体中的总索引数不变，将一个直接索引换成二级索引，这样就可以增加 $256 * 256 - 1$ 个磁盘块的容量。

首先修改 inode 结构体。xv6 有两套相互关联的 inode 结构体，分别是在内存中缓存的 `struct inode`（`kernel/file.h`）和在块设备中持久化的 `struct dinode`（`kernel/fs.h`），只需要将两个数据结构的 `uint addrs[NDIRECT+1]` 改成 `uint addrs[NDIRECT+1+1]` 就可以添加新的索引。此外需要添加修改 `kernel/fs.h` 中的一些宏定义，方便后续的使用：

```c
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT ((BSIZE / sizeof(uint)) * (BSIZE / sizeof(uint)))
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)
```

需要修改的函数是 `kernel/fs.c` 中的 `bmap()` 和 `itrunc()`。前者是通过给定的 inode 获取磁盘块地址，如果未分配块则分配新块后返回。后者是截断（清空）文件内容。

对于 `bmap()`，可以参考 `if(bn < NINDIRECT)` 分支的实现，添加请求的块号落在二级索引中的分支：

0. 当前块号减去 `NINDIRECT`，计算得到二级索引所表示的地址空间中的偏移。
1. 若偏移小于二级索引所能表示的地址空间的最大值：
   1. 先获取二级索引第一级的地址 `ip->addrs[NDIRECT + 1]`，如果为 0，就分配一个块作为二级索引的第一级，将其地址存入 `ip->addrs[NDIRECT + 1]`。
   2. 随后读取该块，使用 $bn / NINDIRECT$ 作为索引获取第二级的地址。同样的，如果为 0，就分配一个块作为二级索引的第二级，将其地址存入第一级。
   3. 随后读取该块，使用 $bn \% NINDIRECT$ 作为索引获取内容块的地址。同样的，如果为 0，就分配一个块作为内容块，将其地址存入第二级。
   4. 返回内容块的地址。

读取块的方式是使用 `bread()` 将数据块读入缓冲区，随后直接对缓冲区的内容（`struct buf->data`）做读写。注意 `bread()` 之后需要 `brelse()` 解锁，`balloc()` 的返回值为 0 表示无空闲块，需要返回 0. 写入缓冲区之后需要使用 `log_write()` 提交写入。

`bmap()` 完整代码如下：

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a, *b;
  struct buf *bp, *bp2;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;

  if(bn < NDINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT + 1]) == 0) {
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT+1] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if ((addr = a[bn / NINDIRECT]) == 0) {
      addr = balloc(ip->dev);
      if (addr == 0) {
        brelse(bp);
        return 0;
      }
      a[bn / NINDIRECT] = addr;
      log_write(bp);
    }
    bp2 = bread(ip->dev, addr);
    b = (uint *)bp2->data;
    if ((addr = b[bn % NINDIRECT]) == 0) {
      addr = balloc(ip->dev);
      if (addr) {
        b[bn % NINDIRECT] = addr;
        log_write(bp2);
      }
    }
    brelse(bp);
    brelse(bp2);
    return addr;
  }

  panic("bmap: out of range");
}
```

在 `itrunc()` 函数中，也可以仿照 `ip->addrs[NDIRECT]` 分支添加对二级索引指向的内容块和索引块的释放。过程如下：

1. 若 `ip->addrs[NDIRECT + 1]` 不为 0：
   1. 读取一级索引内容，依次遍历每一个索引项，若不为 0：
      1. 读取二级索引内容，依次遍历每一个索引项，若不为 0：
         1. 释放二级索引所指的内容块
      2. 释放二级索引块
   2. 释放一级索引块
   3. 设置 `ip->addrs[NDIRECT + 1] = 0`

注意事项同上，`itrunc()` 完整代码如下：

```c
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp, *bp2;
  uint *a, *b;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  if (ip->addrs[NDIRECT + 1]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint *)bp->data;
    for (j = 0; j < NINDIRECT; j++) {
      if (a[j]) {
        bp2 = bread(ip->dev, a[j]);
        b = (uint *)bp2->data;
        for (int k = 0; k < NINDIRECT; k++) {
          if (b[k]) {
            bfree(ip->dev, b[k]);
          }
        }
        brelse(bp2);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

为啥删除文件的操作不用修改呢？可以看看 `iput()` 函数，当一个 inode 的 nlink 项为 0，即文件被删除，inode 可被释放的时候，会调用 `itrunc()` 来清空文件的内容，因此不需要修改删除文件的操作，只需要修改 `itrunc()` 即可。

# Symbolic links ([moderate](https://pdos.csail.mit.edu/6.828/2022/labs/guidance.html))

> In this exercise you will add symbolic links to xv6. Symbolic links (or soft links) refer to a linked file by pathname; when a symbolic link is opened, the kernel follows the link to the referred file. Symbolic links resembles hard links, but hard links are restricted to pointing to file on the same disk, while symbolic links can cross disk devices. Although xv6 doesn't support multiple devices, implementing this system call is a good exercise to understand how pathname lookup works.

## Your job

> > You will implement the `symlink(char *target, char *path)` system call, which creates a new symbolic link at path that refers to file named by target. For further information, see the man page symlink. To test, add symlinktest to the Makefile and run it. Your solution is complete when the tests produce the following output (including usertests succeeding).
>
> ```
> $ symlinktest
> Start: test symlinks
> test symlinks: ok
> Start: test concurrent symlinks
> test concurrent symlinks: ok
> $ usertests -q
> ...
> ALL TESTS PASSED
> $ 
> ```
>
> Hints:
>
> - First, create a new system call number for symlink, add an entry to user/usys.pl, user/user.h, and implement an empty sys_symlink in kernel/sysfile.c.
> - Add a new file type (`T_SYMLINK`) to kernel/stat.h to represent a symbolic link.
> - Add a new flag to kernel/fcntl.h, (`O_NOFOLLOW`), that can be used with the `open` system call. Note that flags passed to `open` are combined using a bitwise OR operator, so your new flag should not overlap with any existing flags. This will let you compile user/symlinktest.c once you add it to the Makefile.
> - Implement the `symlink(target, path)` system call to create a new symbolic link at path that refers to target. Note that target does not need to exist for the system call to succeed. You will need to choose somewhere to store the target path of a symbolic link, for example, in the inode's data blocks. `symlink` should return an integer representing success (0) or failure (-1) similar to `link` and `unlink`.
> - Modify the `open` system call to handle the case where the path refers to a symbolic link. If the file does not exist, `open` must fail. When a process specifies `O_NOFOLLOW` in the flags to `open`, `open` should open the symlink (and not follow the symbolic link).
> - If the linked file is also a symbolic link, you must recursively follow it until a non-link file is reached. If the links form a cycle, you must return an error code. You may approximate this by returning an error code if the depth of links reaches some threshold (e.g., 10).
> - Other system calls (e.g., link and unlink) must not follow symbolic links; these system calls operate on the symbolic link itself.
> - You do not have to handle symbolic links to directories for this lab.

这个实验需要添加一个 `symlink` 系统调用。添加系统调用的准备过程可以参考之前的几个实验，这里不再赘述。此外还需要在 `Makefile` 中添加对用户态程序 `symlinktest` 的编译。

根据提示，添加软链接需要在 `kernel/stat.h` 中添加 `T_SYMLINK` 类型的宏定义 `#define T_SYMLINK 4`，在 `kernel/fcntl.h` 中添加 `O_NOFOLLOW` 标志位的宏定义 `#define O_NOFOLLOW 0x800`。`O_NOFOLLOW` 是软链接特有的打开方式，表示打开时不递归地查询软链接所指向的路径，而是直接打开这个软链接文件。

随后修改 `sys_symlink()` 处理函数。根据提示，参照 `sys_link()` 读取传入的参数，若读入失败则返回 -1。并且需要注意，在开始正式的文件系统相关的处理流程之前需要调用 `begin_op()` 函数，结束后需要调用 `end_op()` 函数。

处理流程如下：

1. 通过 `path` 的父目录获取块设备的 major 与 minor 号。
2. 使用 `create()` 在 `path` 创建 `T_SYMLINK` 类型的文件。
3. 在文件中写入长度为 `MAXPATH` 的 `target` 路径。

注意在读取 inode 之前需要使用 `ilock()` 锁定 inode，读取、写入完成后需要使用 `iunlockput()` 释放内存 inode。

完整代码如下：

```c
uint64 sys_symlink(void) {
  char name[DIRSIZ], target[MAXPATH], path[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;

  begin_op();

  if ((dp = nameiparent(target, name)) == 0)
    return 0;

  ilock(dp);
  int major = dp->major;
  int minor = dp->minor;
  iunlockput(dp);

  ip = create(path, T_SYMLINK, major, minor);
  if (ip == 0) {
    end_op();
    return -1;
  }

  if (writei(ip, 0, (uint64)target, 0, MAXPATH) < MAXPATH) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}
```

随后还需要修改 `sys_open()` 系统调用，添加对 `T_SYMLINK` 类型的文件的支持。如果没有 `O_NOFOLLOW` 标志位，需要一直迭代查找到最后为非软链接的文件为止，或者直到迭代次数达到上限。相关代码可以放在已有的第一次处理文件的类型的分支之前，处理流程如下：

1. 判断文件类型是否为 `T_SYMLINK`，若是：
   1. 判断是否有 `O_NOFOLLOW` 标志位，若有，则当作普通文件看待，结束分支；若没有：
      1. 使用 `readi()` 读取长度为 `MAXPATH` 长的文件内容，即软链接所指向的路径
      2. 使用新的路径获取 inode
      3. 检查是否仍为软链接，若是，则返回 1.1.1 循环，否则退出循环，得到最终指向的文件的 inode
      4. 若 `MAX_SYMLINK_DEPTH` 轮循环结束后，得到的文件仍是软链接，则说明可能出现了循环的引用，以返回码 -1 退出。

注意事项与上述相同。

完整代码如下：

```c
uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_SYMLINK) {
    if (!(omode & O_NOFOLLOW)) {
      for (int i = 0; i < MAX_SYMLINK_DEPTH; i++) {
        if (readi(ip, 0, (uint64)path, 0, MAXPATH) != MAXPATH) {
          iunlockput(ip);
          end_op();
          return -1;
        }
        iunlockput(ip);
        ip = namei(path);
        if (ip == 0) {
          end_op();
          return -1;
        }
        ilock(ip);
        if (ip->type != T_SYMLINK)
          break;
      }
      if (ip->type == T_SYMLINK) {
        iunlockput(ip);
        end_op();
        return -1;
      }
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}
```
