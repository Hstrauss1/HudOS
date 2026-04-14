## Tiny C Compiler

This directory contains the imported "Simple C" compiler from `/Users/hudsons/Desktop/Old/phase6`.

What is reusable:
- `parser.cpp`, `checker.cpp`, `Tree.*`, `Type.*`, `Scope.*`, `Symbol.*`
- the lexer and token definitions
- allocator and AST writer helpers

What is target-specific:
- `generator.cpp`
- `machine.h`
- parts of `Register.*`

Current status:
- the original x86 backend is preserved in `generator_x86.cpp`
- `generator.cpp` is now a minimal AArch64 backend
- the current backend targets a small but real subset:
  - integer/pointer scalars
  - locals and parameters
  - arithmetic and comparisons
  - assignment
  - `if`, `while`, `for`, `break`, `return`
  - direct function calls with up to 8 args
  - string literals
- this is still a host-side compiler, not an in-kernel shell command yet

Planned direction:
1. Preserve the parser, type checker, and AST.
2. Grow the AArch64 backend until it can handle useful user programs.
3. Keep the old x86 generator as a reference until the new backend is stable.
4. Only then decide whether to port the compiler into HudOS itself.

Quick start:
```sh
cd /Users/hudsons/Code/rPiOS/tiny_c_compiler
make
make user-virt
```

Repo-level wrapper:
```sh
cd /Users/hudsons/Code/rPiOS
./tcc tiny_c_compiler/samples/hello.c -o build/hello.elf
```

This is the preferred `tcc` entrypoint now. The in-kernel subset compiler is
separately exposed as `toycc` inside HudOS.

This builds:
- `scc` as the host compiler
- `out/hello.s` as generated AArch64 assembly
- `out/hello.elf` as a `virt`-compatible user ELF

To embed a tiny-compiler app into HudOS:
```sh
cd /Users/hudsons/Code/rPiOS
make PLATFORM=virt TINY_APP=tiny_c_compiler/samples/hello.c
```

At boot, the kernel installs the embedded ELF as `/bin/hello`, and you can run it from the HudOS shell with:
```sh
exec /bin/hello
```
