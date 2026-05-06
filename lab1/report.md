# Lab 1: ELF

*Student: Ilya-Linh Nguyen*

*Innopolis main: i.nguen@innopolis.university*

*Group: CBS-01*

# Task

- Learn how LDD utility works.
- Implement the app called `bldd` (backward ldd) – that shows all EXECUTABLE files that use
specified shared library files. See example.
- App must be configurable (for ex. Possibility to set up scan directory)
- App must generate report as the output (txt, pdf, etc.) – can be implemented using any
languages and libraries.
- Report must be sorted by number of executable usages (high -> low). See example.
- App must accept libraries with at least following arches: x86, x86_64, armv7, aarch64. (must be
architecture dependent)
- App must have `help` with usage examples.

# Solution

Here you can see the workflow of my program:
- Asciinema URL: https://asciinema.org/a/ZZNUagjfP3ARxUY0
- GitHub URL: https://github.com/ilyalinhnguyen/adv-linux

I implemented the task app in C (POSIX + direct ELF parsing). All tests I run in docker images with different arch.

The source code you can see at `bldd.c`.

## Source code overview (`lab1/bldd.c`)

The program works like a reverse version of `ldd`:

- **Recursive scan**: it walks the selected directory tree using `nftw()` and checks each regular file.
- **ELF detection**: for each file it reads the first bytes and verifies ELF magic `0x7F 'E' 'L' 'F'`.
- **Architecture detection**: it reads `e_machine` and ELF class (32/64-bit) to classify binaries as:
  - `x86` (EM_386 + ELF32)
  - `x86_64` (EM_X86_64 + ELF64)
  - `armv7` (EM_ARM + ELF32 + EABI >= 5)
  - `aarch64` (EM_AARCH64 + ELF64)
- **Dependency extraction (DT_NEEDED)**:
  - it finds program headers (`PT_LOAD`, `PT_DYNAMIC`, `PT_INTERP`)
  - reads the dynamic section and collects all `DT_NEEDED` entries
  - finds the string table (`DT_STRTAB`) and resolves the `DT_NEEDED` offsets to library names
- **Filtering**: only keeps executables that depend on the libraries specified in CLI arguments.
- **Report building**: stores results grouped by architecture and library; for each library it stores unique executable paths.
- **Sorting**: libraries are sorted by number of executables (high → low) before printing.
- **Output formats**: supports `txt` (human readable) and `json` (machine readable) via `--format`.

## Build with make

```bash
make
./bldd --help
```

## Usage examples

Generate TXT report:

```bash
./bldd libc.so.6 libm.so.6 --scan-dir /usr/bin --arch x86_64 --output report_usrbin.txt
```

Generate JSON report:

```bash
./bldd libc.so.6 --scan-dir /usr/sbin --arch x86_64 --format json --output report_usrsbin.json
```

## Docker demo (different architectures)

Build images (from repository root):

```bash
docker build -t bldd-amd64   -f ./lab1/Dockerfile.amd64   ./lab1
docker build -t bldd-arm64   -f ./lab1/Dockerfile.arm64   ./lab1
docker build -t bldd-aarch64 -f ./lab1/Dockerfile.aarch64 ./lab1
docker build -t bldd-armv7   -f ./lab1/Dockerfile.armv7   ./lab1
```

Run containers:

```bash
docker run --rm -it bldd-amd64
docker run --rm -it bldd-arm64
docker run --rm -it bldd-aarch64
docker run --rm -it bldd-armv7
```

Commands inside container (show full functionality):

```bash
uname -m
./bldd --help

# amd64 container:
./bldd libc.so.6 libm.so.6 --scan-dir /usr/bin --arch x86_64 --output report_usrbin.txt

# arm64 / aarch64 container:
./bldd libc.so.6 libm.so.6 --scan-dir /usr/bin --arch aarch64 --output report_usrbin.txt

# armv7 container:
./bldd libc.so.6 libm.so.6 --scan-dir /usr/bin --arch armv7 --output report_usrbin.txt

# show JSON report:
./bldd libc.so.6 --scan-dir /usr/sbin --format json --output report.json
```

## Screenshots

I will show only part of it, beacuse the output is very big, but the main functionality is working:

### aarch64

![alt](assets/Screenshot%202026-05-06%20at%2012.07.59.png)
![alt](assets/Screenshot%202026-05-06%20at%2012.08.50.png)

### x86_64

![alt](assets/Screenshot%202026-05-06%20at%2012.14.10.png)
![alt](assets/Screenshot%202026-05-06%20at%2012.14.23.png)

### armv7

![alt](assets/Screenshot%202026-05-06%20at%2012.11.00.png)
![alt](assets/Screenshot%202026-05-06%20at%2012.11.12.png)
![alt](assets/Screenshot%202026-05-06%20at%2012.11.24.png)

How it works in practical you can see in Asciinema earlier.