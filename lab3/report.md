# Lab 3: Booting the kernel

*Student: Ilya-Linh Nguyen*

*Innopolis main: i.nguen@innopolis.university*

*Group: CBS-01*

# Task

## Setup development environment

1. Install git and qemu-system-arm packages
2. Download a mainline linux kernel sources
3. Download a 1st-stage bootloader (any of u-boot, coreboot, etc.) sources
4. Download an embeddable toolset of UNIX programs (busybox, u-root, etc.)
5. Download a GNU cross toolchain

## For a vexpress-a9 qemu target

1. Build bootloader for ARM arch (u-boot, coreboot, etc.)
2. Prepare initrd/initramfs (busybox, u-root, etc.)
3. Prepare rootfs (also can be done using busybox, u-root, etc.)
4. Build the mainline linux
5. Run everything in qemu

# Solution

## Setup development environment

I used a Docker-based workflow so the lab is reproducible on any Linux machine with Docker installed.

### Files added for reproducibility

- `lab3/Dockerfile` - container with toolchain (`qemu-system-arm`, ARM cross-compiler, build tools).
- `lab3/run_lab3.sh` - full automation script: clone sources, build U-Boot, Linux, BusyBox, create initramfs/rootfs, run QEMU.

## Exact commands I ran on host

### 1) Build the lab image

```bash
cd /home/user/Inno/adv-linux/lab3
docker build -t adv-linux-lab3 .
```

Output (excerpt):

```text
#5 [2/3] RUN apt-get update && apt-get install -y --no-install-recommends ... qemu-system-arm ... gcc-arm-linux-gnueabihf ...
#5 32.14 Setting up qemu-system-arm (1:8.2.2+ds-0ubuntu1.16) ...
#5 32.17 Setting up gcc-arm-linux-gnueabihf (4:13.2.0-7ubuntu1) ...
#7 naming to docker.io/library/adv-linux-lab3:latest done
#7 DONE 26.4s
```

### 2) Run the full lab pipeline inside container

```bash
docker run --rm --privileged \
  -v "/home/user/Inno/adv-linux/lab3:/work/lab3" \
  adv-linux-lab3 \
  bash -lc "cd /work/lab3 && ./run_lab3.sh"
```

Output (excerpt):

```text
[1/7] Cloning sources
[2/7] Building U-Boot for vexpress-a9
[3/7] Building Linux kernel for vexpress-a9
[4/7] Building BusyBox (static) for initramfs/rootfs
[5/7] Preparing initramfs (busybox + /init)
[6/7] Preparing rootfs image (ext4 + busybox init)
[7/7] Running QEMU with boot chain
All done. Build artifacts are in /work/lab3/out
```

### 3) Verify generated artifacts

```bash
ls -lh /home/user/Inno/adv-linux/lab3/out
```

Output:

```text
total 29M
drwxr-xr-x  9 root root 4.0K ... initramfs
-rw-r--r--  1 root root 1.1M ... initramfs.cpio.gz
drwxr-xr-x 12 root root 4.0K ... rootfs
-rw-r--r--  1 root root 128M ... rootfs.ext4
-rwxr-xr-x  1 root root 4.2M ... u-boot
-rw-r--r--  1 root root  14K ... vexpress-v2p-ca9.dtb
-rwxr-xr-x  1 root root 5.4M ... zImage
```

## Commands executed inside `run_lab3.sh`

The script runs these key commands to satisfy both task parts:

```bash
# Sources
git clone --depth 1 --branch v2022.01 https://github.com/u-boot/u-boot.git
git clone --depth 1 --branch v6.6 https://github.com/torvalds/linux.git
git clone --depth 1 --branch 1_36_1 https://github.com/mirror/busybox.git

# U-Boot
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
make vexpress_ca9x4_defconfig
make

# Linux kernel
make vexpress_defconfig
./scripts/config --enable DEVTMPFS
./scripts/config --enable DEVTMPFS_MOUNT
./scripts/config --enable BLK_DEV_INITRD
./scripts/config --enable EXT4_FS
./scripts/config --enable VIRTIO
./scripts/config --enable VIRTIO_MMIO
./scripts/config --enable VIRTIO_BLK
make olddefconfig
make zImage dtbs

# BusyBox
make distclean
make defconfig
make oldconfig </dev/null
make
make CONFIG_PREFIX=/work/lab3/out/initramfs install
make CONFIG_PREFIX=/work/lab3/out/rootfs install

# Initramfs and rootfs image
find . -print0 | cpio --null -ov --format=newc > /work/lab3/out/initramfs.cpio
gzip -f /work/lab3/out/initramfs.cpio
truncate -s 128M /work/lab3/out/rootfs.ext4
mkfs.ext4 -F /work/lab3/out/rootfs.ext4

# Boot flow in QEMU
qemu-system-arm -M vexpress-a9 -m 512M -nographic \
  -kernel /work/lab3/out/u-boot \
  -device loader,file=/work/lab3/out/zImage,addr=0x60010000 \
  -device loader,file=/work/lab3/out/initramfs.cpio.gz,addr=0x63000000 \
  -device loader,file=/work/lab3/out/vexpress-v2p-ca9.dtb,addr=0x62f00000 \
  -drive file=/work/lab3/out/rootfs.ext4,if=sd,format=raw
```

## Boot flow proof (required chain)

From QEMU serial log:

```text
U-Boot 2022.01 (May 07 2026 - ...)
=> setenv bootargs console=ttyAMA0 root=/dev/mmcblk0 rw rdinit=/init
=> bootz 0x60010000 0x63000000:0x106964 0x62f00000
Linux version 6.6.0 (...)
[initramfs] early userspace started
[initramfs] waiting for /dev/mmcblk0...
[initramfs] switched to rootfs on /dev/mmcblk0
[rootfs] root filesystem is mounted
[rootfs] boot flow: U-Boot -> Linux kernel -> initramfs -> rootfs
```

This confirms the required sequence:

`bootloader (U-Boot) -> kernel -> initramfs -> rootfs`.

## Screenshot checklist (what to capture)

1. `docker build -t adv-linux-lab3 .` output showing installed `qemu-system-arm` and `gcc-arm-linux-gnueabihf`.
2. `docker run ... ./run_lab3.sh` output showing `[1/7]` ... `[7/7]`.
3. QEMU boot section with:
   - `U-Boot 2022.01`
   - `Linux version 6.6.0`
   - `[initramfs] ...`
   - `[rootfs] boot flow: U-Boot -> Linux kernel -> initramfs -> rootfs`
4. `ls -lh lab3/out` output with built artifacts (`u-boot`, `zImage`, `dtb`, `initramfs.cpio.gz`, `rootfs.ext4`).



