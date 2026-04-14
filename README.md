# HudOS

A bare-metal operating system for the Raspberry Pi 4, written in C and AArch64 assembly.
Built from scratch to develop real systems-programming implementation skills.

---

## Architecture Overview

HudOS runs at **EL2** (hypervisor exception level) on the BCM2711 SoC.
It boots directly from `kernel8.img` with no bootloader other than the GPU firmware.

### Boot sequence

```
GPU firmware (start.elf)
  └─ loads kernel8.img at 0x80000
       └─ boot.S: _start
            ├─ loads _stack_top into SP
            └─ calls kernel_main()
                  ├─ cpu_install_vectors()  — VBAR_EL2 = exception table
                  ├─ uart_init()            — PL011 UART0
                  ├─ alloc_init()           — heap free-list
                  ├─ mmu_init()             — identity map, caches on
                  ├─ fb_init()              — VideoCore mailbox
                  ├─ irq_init()             — GIC-400 + legacy controller
                  ├─ timer_init(10ms)       — ARM generic timer at EL2
                  ├─ uart_irq_enable()      — UART RX interrupt
                  ├─ cpu_enable_irqs()      — unmask DAIF
                  ├─ task_init()            — kernel task (task 0)
                  ├─ ramfs_init()           — in-memory filesystem
                  └─ query_terminal()       — interactive shell
```

### Source layout

| File | Purpose |
|------|---------|
| `boot.s` | Entry point, stack setup |
| `vectors.S` | AArch64 exception vector table, context save/restore |
| `switch.S` | `cpu_switch_to()` — callee-saved register context switch |
| `kernel.c` | `kernel_main()`, interactive shell |
| `uart.c/h` | PL011 UART driver, `kprintf`, IRQ ring buffer |
| `gpio.c/h` | GPIO function select, read, write |
| `timer.c/h` | System timer (1 MHz) + ARM generic timer (IRQ) |
| `cpu.c/h` | EL detection, vector install, exception handler, register/stack dump |
| `irq.c/h` | GIC-400 init, IRQ dispatch |
| `alloc.c/h` | Free-list heap allocator (`kmalloc`, `kfree`, `kmalloc_aligned`) |
| `mmu.c/h` | MMU identity map, cache enable |
| `task.c/h` | Task control blocks, create/exit/sleep |
| `sched.c/h` | Round-robin preemptive scheduler |
| `spinlock.h` | AArch64 exclusive load/store spinlock |
| `semaphore.c/h` | Counting semaphore with task wait queue |
| `mutex.h` | Binary semaphore wrapper |
| `msgqueue.c/h` | Circular message queue with blocking send/recv |
| `syscall.c/h` | EL1→EL2 syscall interface (SVC #0) |
| `user.c/h` | Drop to EL1, user task creation |
| `fb.c/h` | Framebuffer via VideoCore mailbox, console, font |
| `mailbox.c/h` | VideoCore property tag interface |
| `ramfs.c/h` | In-memory filesystem (32 files × 4 KB) |
| `panic.c/h` | `panic()`, `panic_at()`, `ASSERT()`, `BUG()` |
| `string.c/h` | `strlen`, `strcmp`, `memset`, `memcpy`, `itoa`, `atoi`, `hextoul` |
| `test.c/h` | Self-test commands: UART, GPIO, timer, allocator |
| `version.h` | Version string and build metadata |
| `mmio.h` | BCM2711 peripheral base addresses and register offsets |
| `linker.ld` | Memory layout script |

---

## Memory Map

```
Address             Size        Contents
─────────────────────────────────────────────────────────────
0x00000000          512 MB      (unused / device memory below GPU)
0x00080000          ~2 MB       Kernel image (.text, .rodata, .data, .bss)
                                  _stack_top = BSS end + 16 KB
0x00090000 approx   ~948 MB     Heap  (__heap_start → __heap_end = 0x3B400000)
0x3B400000          —           Heap ceiling (set in linker.ld)
0xFC000000          256 MB      BCM2711 peripheral window
  0xFE000000                      Legacy peripherals
    0xFE003000                      System Timer
    0xFE00B000                      Legacy IRQ controller
    0xFE00B880                      Mailbox
    0xFE200000                      GPIO
    0xFE201000                      UART0 (PL011)
  0xFF800000                      ARM local peripherals
  0xFF840000                      GIC-400 distributor
  0xFF842000                      GIC-400 CPU interface
```

Linker-exported symbols used at runtime:

| Symbol | Meaning |
|--------|---------|
| `__heap_start` | First byte of the heap |
| `__heap_end` | One past the last heap byte (0x3B400000) |
| `_stack_top` | Initial stack pointer (16-byte aligned above BSS) |

---

## IPC and Synchronization

### Spinlock (`spinlock.h`)

AArch64 `ldaxr`/`stxr` exclusive-access spinlock. Acquire-release semantics.

```c
spinlock_t lock = SPINLOCK_INIT;
spin_lock(&lock);
// critical section
spin_unlock(&lock);

// with IRQ save/restore
unsigned long flags = spin_lock_irqsave(&lock);
spin_unlock_irqrestore(&lock, flags);
```

### Semaphore (`semaphore.h`)

Counting semaphore with a per-semaphore task wait queue (up to 16 waiters).

```c
semaphore_t sem;
sem_init(&sem, 0);    // 0 = event, 1 = mutex, N = resource pool
sem_wait(&sem);       // blocks if count == 0
sem_signal(&sem);     // wakes one waiter or increments count
sem_trywait(&sem);    // non-blocking, returns 1 if acquired
```

### Mutex (`mutex.h`)

Binary semaphore wrapper.

```c
mutex_t m;
mutex_init(&m);
mutex_lock(&m);
// exclusive section
mutex_unlock(&m);
```

### Message Queue (`msgqueue.h`)

Circular buffer (16 messages × 64 bytes) with blocking send and receive.

```c
msgqueue_t q;
mq_init(&q);
mq_send(&q, type, data, len);   // blocks if full
msg_t msg;
mq_recv(&q, &msg);              // blocks if empty
```

---

## Error Handling and Diagnostics

### Kernel panic

```c
panic("something went badly wrong");         // any site
panic_at("msg", __FILE__, __LINE__);         // with location
ASSERT(ptr != NULL);                         // expression check
BUG("should never reach here");              // unconditional
```

All panics: disable IRQs → print message + task + EL → spin forever.

### Exception handler (`cpu.c`)

On any unhandled exception (sync/FIQ/SError) the vector table saves full
context (x0–x30, ELR, ESR, FAR) and calls `exception_handler()`, which prints:

- Exception type and class (EC field decoded)
- ELR (faulting PC), ESR, FAR
- Data/instruction abort reason (DFSC/IFSC decoded)
- Current task name and exception level
- Full register dump (x0–x30)
- Frame-pointer stack trace + raw stack words

---

## Build and Run

### Requirements

- `aarch64-elf` cross-compiler (e.g. from `brew install aarch64-elf-gcc`)
- `qemu-system-aarch64` with `raspi4b` machine support

### Build

```sh
make
```

### Real Tiny C

Use the repo-level `tcc` wrapper for the real Tiny C compiler:

```sh
./tcc tiny_c_compiler/samples/hello.c -o build/hello.elf
```

This runs the host-side compiler in [tiny_c_compiler/](/Users/hudsons/Code/rPiOS/tiny_c_compiler).

To boot HudOS with a Tiny C app embedded:

```sh
make PLATFORM=virt TINY_APP=tiny_c_compiler/samples/hello.c run
```

Inside HudOS, the old in-kernel subset compiler is `toycc`, not `tcc`.

### Run in QEMU (with display)

```sh
make run
```

`make run` attaches serial input/output to the launching terminal, which is the
currently reliable input path in QEMU.
The QEMU keyboard is attached directly to the DWC2 root port (`usb-bus.0,port=1`)
to avoid QEMU auto-inserting a USB hub in front of it.

### Experimental QEMU serial-console window

```sh
make run-vc
```

This uses QEMU's own serial console window (`-serial vc`). Depending on the
host/QEMU build, keyboard input there may be inconsistent.

### Run headless (serial only)

```sh
make run-serial
```

### Run the `virt` platform skeleton

```sh
make run-virt
```

This boots a serial-only `virt` target with Pi-specific peripherals disabled.
It is the starting point for bringing up simpler QEMU-native input devices
without depending on the Raspberry Pi 4 USB host path.

### GDB debug session

```sh
make debug   # QEMU halts at reset, listens on :1234
# in another terminal:
aarch64-elf-gdb kernel.elf -ex "target remote :1234"
```

### Clean

```sh
make clean
```

---

## Shell Command Reference

Type `help` at the `>` prompt for the full grouped list. Quick reference:

### System

| Command | Description |
|---------|-------------|
| `help` | Show grouped command list |
| `info` | Version, arch, board, build date |
| `why` | Project motivation |
| `clear` | ANSI clear screen |
| `echo <text>` | Print text |
| `el` | Current exception level |
| `uptime` | Seconds since boot |
| `panic` | Trigger kernel panic |
| `crashtest <type>` | Fault injection: `null` `assert` `brk` `undef` `align` |

### GPIO

| Command | Description |
|---------|-------------|
| `led <pin> <on\|off>` | Set GPIO pin high or low |
| `blink <pin>` | Toggle pin 5 times with 500 ms period |
| `readpin <pin>` | Read and print pin state |

### Timer / IRQ

| Command | Description |
|---------|-------------|
| `ticks` | Print timer IRQ count since boot |
| `irqtest` | Count IRQs over 3 seconds |
| `timerdbg` | Dump GIC distributor and CPU interface registers |

### Memory

| Command | Description |
|---------|-------------|
| `peek <addr>` | Read 32-bit word at hex address |
| `poke <addr> <val>` | Write 32-bit hex value to address |
| `dump <addr> [n]` | Dump n 32-bit words (default 8, max 64) |
| `heapinfo` | Used bytes, free bytes, free block count |
| `malloc <size>` | Allocate bytes (decimal or hex), print pointer |
| `free <addr>` | Free pointer at hex address |

### Tasks

| Command | Description |
|---------|-------------|
| `tasks` | List all active tasks with state |
| `spawn` | Spawn background counter task |
| `kill <id>` | Kill task by ID |
| `sleep <ms>` | Sleep shell task for ms milliseconds |
| `yield` | Yield to next ready task |
| `uspawn` | Spawn EL1 user-mode demo task |

### IPC / Sync demos

| Command | Description |
|---------|-------------|
| `locktest` | Spinlock acquire/trylock/release test |
| `semtest` | Producer→consumer semaphore demo (5 items) |
| `mqtest` | Ping-pong message queue demo (5 rounds) |
| `mutextest` | Two tasks share a mutex-protected counter |

### Filesystem (ramfs)

| Command | Description |
|---------|-------------|
| `mkfile <name>` | Create empty file |
| `write <name> <data>` | Write text data to file |
| `cat <name>` | Print file contents |
| `ls` | List all files with sizes |
| `tcc <src> -o <out>` | Real host-side Tiny C compiler in the repo |
| `toycc <src> -o <out>` | In-kernel toy Tiny C subset compiler |

### Framebuffer

| Command | Description |
|---------|-------------|
| `fbtest` | Draw colored rectangles and text on framebuffer |

### Self-tests

| Command | Description |
|---------|-------------|
| `test_uart` | String library and kprintf format tests |
| `test_gpio` | GPIO set/read/write tests |
| `test_timer` | Timer advancing, delay accuracy, IRQ firing |
| `test_alloc` | kmalloc, kfree, alignment, accounting |
| `test_all` | Run all four suites, report total failures |

---

## Architecture Notes

### Exception levels

The kernel runs at EL2 (hypervisor). EL1 is available for user tasks via
`user_task_create()`. Syscalls use `SVC #0` and trap to EL2 via the lower-EL
sync vector. EL0 is not used.

### Preemption

The ARM generic hypervisor physical timer (PPI 26) fires every 10 ms.
`sched_tick()` sets a preemption flag; `irq_handler()` calls `schedule()`
after any IRQ. The scheduler picks the next READY task in round-robin order
and calls `cpu_switch_to()` (in `switch.S`) to swap callee-saved registers
and stack pointer.

### QEMU notes

- ARM local peripheral MMIO (0xFF800000) causes SError in QEMU raspi4b.
  Use `CNTHP_TVAL_EL2` and `CNTHP_CTL_EL2` system registers instead of
  memory-mapped local timer registers.
- GPIO read-back after write returns 0 in QEMU (not fully emulated).
  All GPIO tests note this and do not fail on it.
- Framebuffer initializes via VideoCore mailbox and works in QEMU with
  a display window (`make run`).

### Heap allocator

Free-list first-fit with bump allocation for new blocks. Freed blocks are
inserted at the head of the free list (no coalescing yet). Thread-safe via
`spin_lock_irqsave`. The heap spans from `__heap_start` (~128 KB after
the kernel image) to 0x3B400000 (~948 MB).

### Task stacks

Each task gets a 4 KB stack allocated from the kernel heap at `task_create()`
time. The task control block holds the saved SP and callee-saved registers
(x19–x30). Context switch saves/restores only these 13 registers; x0–x18 are
caller-saved and live on the task's own stack.
