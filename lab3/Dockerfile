FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash ca-certificates curl xz-utils bzip2 gzip tar unzip file cpio \
    git make gcc g++ rsync \
    bc bison flex \
    libssl-dev libelf-dev \
    device-tree-compiler \
    qemu-system-arm qemu-utils \
    gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf \
    e2fsprogs util-linux kmod \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /work
