# Lab 2: GDB

*Student: Ilya-Linh Nguyen*

*Innopolis main: i.nguen@innopolis.university*

*Group: CBS-01*

# Task

- Learn these utilities: GDB, strace, GHIDRA, ldd.
- `Hack` the application provided by the instructor!
- Application requires the license that is generated based on your hardware id.
- License will be stored somewhere on your PC (filesystem).
- Implement 2 artifacts:
    1. Create keygen for this app.
    2. Create binary patch to disable licensing completely.

# Solution

Here you can see the files of my solution:
- GitHub URL: https://github.com/ilyalinhnguyen/adv-linux

For this lab I switched from my macOS to the Linux laptop for more comfortable lab completion.

---

## Initial Triage 

### File exploring and dependency check

Firstly, let's inspect our app with some common commands:

```bash
file lab2/hack_app/hack_app
```

Result: 

![alt](assets/Screenshot%20from%202026-05-06%2014-17-20.png)

```bash
ldd lab2/hack_app/hack_app
```
![alt](assets/Screenshot%20from%202026-05-06%2014-22-06.png)


Key findings:

- Binary is 64-bit ELF PIE, dynamically linked.
- Missing dependency:
  - `libcrypto.so.1.1 => not found`

Where this comes from: the binary was compiled against OpenSSL 1.1 and imports MD5 symbols from `libcrypto.so.1.1`. I verified the required imported symbols from the dynamic symbol table:

```bash
objdump -T lab2/hack_app/hack_app | grep -E "MD5_(Init|Update|Final)"
```

![alt](assets/Screenshot%20from%202026-05-06%2014-27-30.png)

Let's fix this now, to prevent errors in future:

I created a local compatibility shared library that exports only the needed MD5 functions with the correct `OPENSSL_1_1_0` symbol version (so the loader can resolve them):

- Source: `lab2/deps/md5_compat.c`
- Version script: `lab2/deps/openssl_1_1.map`
- Output: `lab2/deps/libcrypto.so.1.1`

Build command (chosen by me to compile the shim and apply symbol versioning via the version script):

```bash
gcc -shared -fPIC lab2/deps/md5_compat.c \
  -Wl,--version-script=lab2/deps/openssl_1_1.map \
  -Wl,-soname,libcrypto.so.1.1 \
  -o lab2/deps/libcrypto.so.1.1
```

Then I will execute the target app with:

```bash
LD_LIBRARY_PATH=/home/user/Inno/adv-linux/lab2/deps ...
```

Let's check with `string` command now:

```bash
strings lab2/hack_app/hack_app
```

![alt](assets/Screenshot%20from%202026-05-06%2014-32-23.png)

Super interesting strings found:

- `/proc/self/exe`
- `user.license`
- `Your HWID is %08X%08X.`
- `Enter the license key:`
- `Your app is licensed to this PC!`
- `Now you app is activated! Thanks for purchasing!`
- `Provided key is wrong! App is closing!`
- `MD5_Init`, `MD5_Update`, `MD5_Final`

These strings already hint that:

- App computes a hardware-based ID (`HWID`).
- Key is MD5-related.
- License is stored in extended attribute (`user.license`) on the executable file.

---

## Static Reverse Engineering (`gdb` disassembly)

I disassembled `main` in batch mode:

```bash
gdb -q -batch -ex "file lab2/hack_app/hack_app" -ex "disassemble main"
```

The most valuable info that I could find from `main`

Calls `__get_cpuid(1, &eax, &ebx, &ecx, &edx)`.

![alt](assets/Screenshot%20from%202026-05-06%2014-35-23.png)

Performs byte swap on `eax` and `edx`.

From screenshot earlier:

```text
0x000000000000145b <+119>: mov    -0x2c(%rbp),%eax
0x000000000000145e <+122>: shr    $0x18,%eax
```

And

```
0x0000000000001485 <+161>: mov    %eax,-0x10(%rbp)
...
0x00000000000014b2 <+206>: mov    %eax,-0xc(%rbp)
```

from

![alt](assets/Screenshot%20from%202026-05-06%2014-38-51.png)

Builds HWID string with:
  - `snprintf(PSN, 0x11, "%08X%08X", swapped_eax, swapped_edx)`.

![alt](assets/Screenshot%20from%202026-05-06%2014-39-55.png)

Computes MD5 of this 16-char ASCII string.

![alt](assets/Screenshot%20from%202026-05-06%2014-41-35.png)

Builds expected key as lowercase hex of **reversed MD5 digest bytes**.

The index `0xf - i` used to read bytes from `md5digest`:

![alt](assets/Screenshot%20from%202026-05-06%2014-42-43.png)

Then from:

![alt](assets/Screenshot%20from%202026-05-06%2014-44-54.png)

We got: 

Reads current executable path via:
  - `readlink("/proc/self/exe", ...)`.


```text
0x0000000000001554 <+368>: call   0x11b0 <readlink@plt>
```

Reads xattr:
  - `getxattr(binaryPath, "user.license", ...)`.


```text
0x0000000000001573 <+399>: call   0x11d0 <getxattr@plt>
```

Compares expected key and xattr with:
  - `strncmp(expected, xattrValue, 0x21)`.


```text
0x0000000000001597 <+435>: call   0x1150 <strncmp@plt>
```

If not licensed, asks user input (`scanf("%32s")`), compares, and if correct:
  - `setxattr(binaryPath, "user.license", expected, 0x21, 0)`.


```text
0x00000000000015dd <+505>: call   0x11f0 <__isoc99_scanf@plt>
...
0x000000000000161e <+570>: call   0x11a0 <setxattr@plt>
```


License gate branch in `main`:

- Original instruction at virtual/file offset `0x15AB`:
  - `0f 85 8e 00 00 00` (`jne 0x163f`)

```text
0x00000000000015ab <+455>: jne    0x163f <main+603>
```

Patch strategy:

- Replace conditional jump with unconditional jump to licensed branch:
  - New bytes: `e9 8f 00 00 00 90` (`jmp 0x163f; nop`)

---

## Keygen Implementation

Implementhing the python `lab2/keygen.py` script for making key.
The app constructs the expected license key fully inside `main`:

- It queries CPU identification data via CPUID leaf 1:
  - `call __get_cpuid` with function id `1`
- It then uses only **two 32-bit values** from that leaf:
  - `eax` and `edx`
- Both values are converted to big-endian (byte-swapped).
- The program formats HWID as a 16-character uppercase hex string:
  - `HWID = "%08X%08X" % (bswap32(eax), bswap32(edx))`
- It computes MD5 of that **ASCII HWID string**.
- Finally, it creates the license string by taking the **MD5 digest bytes in reverse order** and printing each byte as lowercase hex:
  - `key = hex(md5(HWID))[::-1 by bytes]`

That matches the disassembly results:

```text
0x0000000000001456 <+114>: call   0x131e <__get_cpuid>
...
0x00000000000014d8 <+244>: call   0x1130 <snprintf@plt>     ; "%08X%08X"
0x00000000000014e9 <+261>: call   0x1397 <calc_md5>
...
0x00000000000014f7 <+275>: mov    $0xf,%eax
0x00000000000014fc <+280>: sub    -0x18(%rbp),%eax         ; index = 0xF - i
0x0000000000001508 <+292>: movzbl (%rax,%rdx,1),%eax       ; md5digest[15-i]
0x0000000000001532 <+334>: call   0x1190 <sprintf@plt>      ; "%02x"
```

How `keygen.py` reproduces it

Step-by-step mapping to the recovered logic:

1. **Read CPUID leaf 1**
  In `keygen.py`, I execute the `cpuid` instruction (leaf = 1) via a small in-memory code stub using `mmap` (RWX page) and `ctypes`. This returns the same 4 registers the binary uses: `eax, ebx, ecx, edx`.
2. **Compute the HWID string exactly like the binary**
  Only `eax` and `edx` are used.
  - `hwid_hi = bswap32(eax)`
  - `hwid_lo = bswap32(edx)`
  - `HWID = f"{hwid_hi:08X}{hwid_lo:08X}"`
   This matches the binary’s `snprintf("%08X%08X", ...)`.
3. **Compute MD5 over the ASCII HWID**
  `digest = hashlib.md5(HWID.encode("ascii")).digest()`
4. **Reverse digest bytes and hex-encode**
  The binary reads bytes in the order `md5digest[15], md5digest[14], ..., md5digest[0]`.  
  So the keygen does:
  - `key = "".join(f"{b:02x}" for b in digest[::-1])`

This produces a **32-character** lowercase hex key, which is exactly what the program reads with `scanf("%32s")`.

Run:

```bash
python3 lab2/keygen.py
```

Observed output on my Linux laptop:

![alt](assets/Screenshot%20from%202026-05-06%2015-00-15.png)

---

## Binary Patch Implementation


The app has two high-level states:

- **Licensed path**: prints `Your app is licensed to this PC!` and exits (no prompt).
- **Unlicensed path**: prints HWID and asks for a key; if correct, it writes `user.license` xattr.

The “gate” that selects which path to execute is a conditional jump:

```text
0x00000000000015ab <+455>: jne    0x163f <main+603>
```

The report’s earlier finding shows that `-0x1c(%rbp)` is set to 1 when the xattr matches the expected key, then the program branches to the licensed message.

At `0x15AB`, the original instruction bytes are:

- `0f 85 8e 00 00 00` = `jne rel32`

So if the condition is true (licensed), execution jumps to `0x163f` (licensed branch).

To disable licensing completely, we force execution to always go to that branch, regardless of the condition:

- Replace `jne` with `jmp rel32` to the same target.

Patched bytes:

- `e9 8f 00 00 00 90`
  - `e9 8f 00 00 00` = `jmp rel32` (to `0x163f`)
  - `90` = `nop` (fills remaining 1 byte because `jmp rel32` is 5 bytes, while `jne rel32` is 6 bytes)

So the patched control flow always behaves as “licensed”.

What `patch.py` script does

Behavior:

- Checks bytes at offset `0x15AB`.
- Verifies expected original bytes (`0f858e000000`) to avoid accidental corruption.
- Replaces with patched bytes (`e98f00000090`).
- Supports idempotence (if already patched, prints message and exits).

Usage:

```bash
cp lab2/hack_app/hack_app lab2/hack_app/hack_app.patched
python3 lab2/patch.py lab2/hack_app/hack_app.patched
```

And let's disassembly this and check:

```bash
objdump -d -M intel lab2/hack_app/hack_app.patched | sed -n '418,434p'
```

![alt](assets/Screenshot%20from%202026-05-06%2014-58-52.png)

At `0x15ab`: `jmp 0x163f`

This forces control flow into licensed branch and bypasses normal license check path.

---

## A little of testing

Let's try wrong key for the app

```bash
cp lab2/hack_app/hack_app lab2/hack_app/hack_app.clean
printf "advlinuxadvlinuxadvlinuxadvlinux\n\n" | \
  LD_LIBRARY_PATH=/home/user/Inno/adv-linux/lab2/deps \
  lab2/hack_app/hack_app.clean
```

Output:

![alt](assets/Screenshot%20from%202026-05-06%2015-05-06.png)

And now put correct key


```bash
printf "37e9ef58cd7826a3379678653268ed54\n\n" | \
  LD_LIBRARY_PATH=/home/user/Inno/adv-linux/lab2/deps \
  lab2/hack_app/hack_app.clean
```

Output:

![alt](assets/Screenshot%20from%202026-05-06%2015-06-01.png)

Now let's just run after activation

```bash
printf "\n" | LD_LIBRARY_PATH=/home/user/Inno/adv-linux/lab2/deps \
  lab2/hack_app/hack_app.clean
```

Output:

![alt](assets/Screenshot%20from%202026-05-06%2015-07-06.png)
That's it for this task
