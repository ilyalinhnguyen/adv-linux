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

Here you can see the workflow of the lab and files (The `out/` dir will be in moodle, it is too large):
- Asciinema URL: https://asciinema.org/a/tvQ3fEUTCdYWpkSN
- GitHub URL: https://github.com/ilyalinhnguyen/adv-linux

## Setup development environment

I used a Docker image for this lab because:

- I want to isolate this lab from my system
- It will be easy reproducable in other machines

I put in Docker image all needed packages so let's build it:

```bash
cd lab3/
docker build -t adv-linux-lab3 .
```

![alt](assets/Screenshot%20from%202026-05-07%2016-06-59.png)

And run it

```bash
docker run --rm -it --privileged -v "/home/user/Inno/adv-linux/lab3:/work/lab3" adv-linux-lab3 bash
```

We are inside our container. 

Let's prepare folders and environment variables:

```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export MAKEFLAGS="-j$(nproc)"

mkdir -p /work/lab3/src /work/lab3/out
```

Perfect, now let`s clone the sources:

```bash
cd src/
git clone --depth 1 --branch v2022.01 https://github.com/u-boot/u-boot.git
git clone --depth 1 --branch v6.6 https://github.com/torvalds/linux.git
git clone --depth 1 --branch 1_36_1 https://github.com/mirror/busybox.git
```

After a long time everything is cloned.

Now let's build `U-boot`:

```bash
cd u-boot/
make vexpress_ca9x4_defconfig
make
cp -f u-boot /work/lab3/out/u-boot
```

![alt](assets/Screenshot%20from%202026-05-07%2016-08-19.png)

Build Linux kernel:

```bash
cd /work/lab3/src/linux
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
cp -f arch/arm/boot/zImage /work/lab3/out/zImage
cp -f arch/arm/boot/dts/arm/vexpress-v2p-ca9.dtb /work/lab3/out/vexpress-v2p-ca9.dtb
```

![alt](assets/Screenshot%20from%202026-05-07%2016-11-55.png)

And build busybox (I used `sed` for more comfortable editing):

```bash
cd /work/lab3/src/busybox
make distclean
make defconfig
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
sed -i 's/# CONFIG_FEATURE_SH_STANDALONE is not set/CONFIG_FEATURE_SH_STANDALONE=y/' .config
sed -i 's/# CONFIG_FEATURE_SH_NOFORK is not set/CONFIG_FEATURE_SH_NOFORK=y/' .config
sed -i 's/^CONFIG_TC=.*/# CONFIG_TC is not set/' .config
make oldconfig </dev/null
make
```

![alt](assets/Screenshot%20from%202026-05-07%2016-12-47.png)


Now let's create initramfs.

```bash
make CONFIG_PREFIX=/work/lab3/out/initramfs install
mkdir -p /work/lab3/out/initramfs/{proc,sys,dev,newroot}
```

And write the following `init` script:

```bash
#!/bin/sh
set -e
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
echo "[initramfs] early userspace started"
echo "[initramfs] waiting for /dev/mmcblk0..."
for _ in 1 2 3 4 5 6 7 8 9 10; do
  [ -b /dev/mmcblk0 ] && break
  sleep 1
done
mount -t ext4 /dev/mmcblk0 /newroot
echo "[initramfs] switched to rootfs on /dev/mmcblk0"
exec switch_root /newroot /sbin/init
```

Giving rights for run it:

```bash
chmod +x /work/lab3/out/initramfs/init
```

And package our `initramfs/` directory into a compressed initramfs image so the Linux kernel can unpack it at boot:

```bash
find . -print0 | cpio --null -ov --format=newc > /work/lab3/out/initramfs.cpio
gzip -f /work/lab3/out/initramfs.cpio
```

![alt](assets/Screenshot%20from%202026-05-07%2016-20-07.png)

Now let's create rootfs and ext4 image:

```bash
mkdir -p /work/lab3/out/rootfs
make CONFIG_PREFIX=/work/lab3/out/rootfs install
mkdir -p /work/lab3/out/rootfs/{proc,sys,dev,etc,root,tmp,mnt}
```

And write the following `init` script:

```bash
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
echo "[rootfs] root filesystem is mounted"
echo "[rootfs] boot flow: U-Boot -> Linux kernel -> initramfs -> rootfs"
exec /bin/sh
```

Giving rights to it:

```bash
chmod +x /work/lab3/out/rootfs/sbin/init
```

Create a file that will be virtual SD card/disk with 128 MB:

```bash
truncate -s 128M /work/lab3/out/rootfs.ext4
```

Formats that file with an ext4 filesystem:

```bash
mkfs.ext4 -F /work/lab3/out/rootfs.ext4
```

Create mountpoint dir and mount the ext4 filesystem using a loop device, so the kernel treats the file like a disk partition:

```bash
mkdir -p /tmp/rootfs-mnt
mount -o loop /work/lab3/out/rootfs.ext4 /tmp/rootfs-mnt
```

Copy my prepared rootfs directory tree into the mounted filesystem and unmount the image cleanly and remove the temporary mount directory:

```bash
cp -a /work/lab3/out/rootfs/. /tmp/rootfs-mnt/
umount /tmp/rootfs-mnt
rmdir /tmp/rootfs-mnt
```

So let's boot in QEMU now:

```bash
INITRD_SIZE_HEX=$(printf "0x%x" "$(stat -c %s /work/lab3/out/initramfs.cpio.gz)")
( sleep 3; \
  echo "setenv bootargs console=ttyAMA0 root=/dev/mmcblk0 rw rdinit=/init"; \
  echo "bootz 0x60010000 0x63000000:${INITRD_SIZE_HEX} 0x62f00000"; \
) | timeout 120 qemu-system-arm \
      -M vexpress-a9 \
      -m 512M \
      -nographic \
      -kernel /work/lab3/out/u-boot \
      -device loader,file=/work/lab3/out/zImage,addr=0x60010000 \
      -device loader,file=/work/lab3/out/initramfs.cpio.gz,addr=0x63000000 \
      -device loader,file=/work/lab3/out/vexpress-v2p-ca9.dtb,addr=0x62f00000 \
      -drive file=/work/lab3/out/rootfs.ext4,if=sd,format=raw
```

QEMU starts by executing the U-Boot binary as if it were the firmware/bootloader for the `vexpress-a9` machine.
I use `-nographic` so all output goes to the terminal via the emulated serial (`ttyAMA0`).

`-device loader` pre-loads `zImage`, `initramfs.cpio.gz`, and the `dtb` into RAM at the given addresses; then we “type” U-Boot commands:
`setenv bootargs ... rdinit=/init` and `bootz KERNEL INITRD:SIZE DTB`.
Rootfs is attached as `if=sd`, so Linux sees it as `/dev/mmcblk0` (mounted by initramfs, then `switch_root`).
`timeout 120` just stops QEMU after 2 minutes.

![alt](assets/Screenshot%20from%202026-05-07%2016-43-07.png)

That's it for the task, `out/` directory will be on moodle, beacuse its too large for github.