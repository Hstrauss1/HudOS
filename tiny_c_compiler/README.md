# Tiny C Compiler

This folder holds the real Tiny C compiler used by the project.

It started life as an imported "Simple C" compiler, and it is now being shaped
into the compiler path HudOS can actually grow around. The goal is not "full C"
for its own sake. The goal is a small, understandable compiler that can produce
useful AArch64 programs for HudOS.

## What lives here

Most of the front end is already solid and reusable:
- `parser.cpp`, `checker.cpp`
- `Tree.*`, `Type.*`, `Scope.*`, `Symbol.*`
- the lexer and token definitions
- allocator and AST helper code

The architecture-specific work mostly lives in:
- `generator.cpp`
- `machine.h`
- parts of `Register.*`

The original x86 backend is still kept around in `generator_x86.cpp` as a
reference while the AArch64 path matures.

## Current state

Right now this compiler is a real host-side tool with a still-growing AArch64
backend.

The backend already handles a meaningful Tiny C subset, including:
- integer and pointer scalars
- locals and parameters
- arithmetic and comparisons
- assignment
- `if`, `while`, `for`, `break`, `return`
- direct function calls with up to 8 arguments
- string literals

It is much more capable than the in-kernel `toycc` path, but it is still a
host-side compiler. It is not a fully ported "compile inside HudOS" tool yet.

## Why this compiler exists

There are really two different compiler stories in this repo:
- `./tcc` at the repo root is the real Tiny C compiler workflow
- `toycc` inside HudOS is the lightweight in-kernel subset compiler

If you want the serious compiler path, this directory is the one that matters.

## Quick start

Build the compiler directly:

```sh
cd /Users/hudsons/Code/rPiOS/tiny_c_compiler
make
make user-virt
```

That gives you:
- `scc` as the host compiler
- `out/hello.s` as generated AArch64 assembly
- `out/hello.elf` as a `virt`-compatible user ELF

## Recommended way to use it

From the repo root, use the wrapper script:

```sh
cd /Users/hudsons/Code/rPiOS
./tcc tiny_c_compiler/samples/hello.c -o build/hello.elf
```

This is the main `tcc` entrypoint for the project now.

## Running a Tiny C app in HudOS

To embed a Tiny C app into the `virt` build:

```sh
cd /Users/hudsons/Code/rPiOS
make PLATFORM=virt TINY_APP=tiny_c_compiler/samples/hello.c
```

When HudOS boots, the ELF is installed into `/bin/hello`, and you can run it
from the shell with:

```sh
exec /bin/hello
```

## Direction

The plan is straightforward:
1. Keep the parser, type checker, and AST stable.
2. Keep expanding the AArch64 backend until it covers the Tiny C programs we care about.
3. Use the old x86 generator as a reference while that backend grows up.
4. Decide on deeper in-OS compiler integration only after the host-side path feels dependable.

In other words: make the real compiler path good first, then get fancy later.
