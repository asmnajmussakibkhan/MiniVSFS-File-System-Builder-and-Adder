# MiniVSFS — File System Builder and Adder

A compact C-based virtual file system project featuring two tools, 'mkfs_builder' for creating file system images and 'mkfs_adder' for adding files into them. Designed for demonstrating file system structures, CRC32 integrity and inode management. This project implements a **miniature virtual file system (MiniVSFS)** consisting of two C programs:
- **`mkfs_builder.c`** — creates a blank file system image.
- **`mkfs_adder.c`** — adds a file into an existing file system image.

Both programs are written in C and are intended to be compiled and executed on **Ubuntu (Linux)** using GCC.

---

## Team Members

| Member | Name | Student ID |
|:-------:|:--------------------------|:-----------:|
| 1 | Samin Yeasir | 20301232 |
| 2 | Towhidul Islam Pranto | 20301215 |
| 3 | ASM Najmus Sakib Khan | 21101248 |

---

## Compilation Commands

To compile both programs in Ubuntu, we use the following commands in the terminal:

```bash
gcc mkfs_builder.c -o mkfs_builder
gcc mkfs_adder.c -o mkfs_adder
```

To use the math library (`-lm`), we use these commands instead:

```bash
gcc mkfs_builder.c -o mkfs_builder -lm
gcc mkfs_adder.c -o mkfs_adder -lm
```

---

## Usage Instructions

### 1. Create a File System Image

We use `mkfs_builder` to create a new MiniVSFS image file.

```bash
./mkfs_builder --image out.img --size-kib 512 --inodes 128
```

- `--image` specifies the name of the output image file.  
- `--size-kib` specifies the image size in KiB (e.g., 512).  
- `--inodes` specifies the number of inodes (e.g., 128).

This command generates a new file system image named `out.img`.

---

### 2. Add a File to the Image

We use `mkfs_adder` to insert a new file into an existing file system image.

```bash
./mkfs_adder --input out.img --output out2.img --file hello.txt
```

- `--input` specifies the existing image file.  
- `--output` specifies the new image to save the changes.  
- `--file` specifies the file to add into the image.

After execution, `hello.txt` will be inserted into the new file system image `out2.img`.

---

## Inspecting the File System Image

We can view the raw contents of the image using either of the following commands:

```bash
hexdump -C out.img | less
```

or

```bash
xxd out.img | less
```

These commands display a hexadecimal representation of the file system image for debugging or verification.

---

## Project Overview

### `mkfs_builder.c`
- Builds a new virtual file system image.
- Initializes the superblock, inode table, bitmaps, and root directory.
- Uses CRC32 checksums for data integrity.

### `mkfs_adder.c`
- Loads an existing MiniVSFS image.
- Adds a new file by allocating inodes and data blocks.
- Updates directory entries and metadata with CRC validation.

---

## System Requirements

- **OS:** Ubuntu or any Debian-based Linux distribution 
- **Compiler:** GCC  
- **Libraries:** Standard C Library (stdio, stdlib, string, etc.)

---
