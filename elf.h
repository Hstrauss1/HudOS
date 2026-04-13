//Hudson Strauss
#ifndef ELF_H
#define ELF_H

// Load an AArch64 ELF64 executable from the VFS and spawn it as a user task.
// The binary must be statically linked and position-dependent (not PIE).
// Since the MMU uses an identity map, ELF virtual addresses == physical addresses.
//
// Returns the new task ID, or -1 on error.
int elf_exec(const char *vfs_path, const char *task_name);

// Load an AArch64 ELF64 executable from a pre-loaded memory buffer.
// Use this when the binary was read via fat32_load rather than from the VFS.
// Returns the new task ID, or -1 on error.
int elf_exec_buf(const unsigned char *buf, unsigned int size, const char *task_name);

#endif
