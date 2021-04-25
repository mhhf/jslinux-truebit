## JSLinux on Truebit

This is a small proof of concept project trying to compile a virtual
machine to wasm and letting it run linux on truebit.
Possible use cases could be:

- trusted program execution
- trusted ci/cd pipeline

This repo is a fork of Bellard's [jslinux] project modefied
to be run as a standalone wasm from a single main() function.
It has multiple input files:

- bbl64.bin - the bios
- kernel-riscv64.bin - the kernel
- root-riscv64.bin - the root disk
- stdin.txt - stdin
- stdout.txt - stdout

The root disk is a ext2 file system with a minimal linux installation,
which gets booted. After the boot the content of stdin.txt is piped to
the machines stdin while the stdout is saved to stdout.txt.

Currently its only meant as a poc disregarding performance, which is ~600 times slower
for the truebit-wasm interpreter then that of the nodejs native one.
Overall booting and shutting down the vm took 15min on an avarage
laptop (truebit) vs 1,5 sec with node. Sadly this is not scalable to almost all real world tasks.
However one could explore several performance improvements like truebits-jit,
improving the interpreter or leveraging intel-vm or amd-v for the specific vm emulation task.

## Requiremens

This is build with [nix] which you can get [here](https://nixos.org/download.html).
If you want to run it with the truebit interpreter, make sure
you have [truebit-eth] repo checked out by asking for it [here](https://gitter.im/TruebitProtocol/community).

## Usage

Make sure to edit your correct `TRUEBIT_PATH` in the `default.nix`.

Run it with:
```
nix-shell --run "make clean && make && make run"
```

This will build the wasm (v1.37.36) binary, prepare it with truebits
`emscripten-module-wrapper` and run it with the `ocaml-offchain/interpreter/wasm`.
It boots a minimal linux and pipes the content of stdin.txt into stdin.
The output is saved into stdout.txt and will be available after the execution ends
in the root folder as `stdout.txt.out`.

A stdin.txt file like:

```
pwd
halt -f

```

will produce an output like:

```
[    0.889800] NET: Registered protocol family 17
[    0.890200] 9pnet: Installing 9P2000 support
[    0.898200] EXT4-fs (vda): couldn't mount as ext3 due to feature incompatibilities
[    0.900200] EXT4-fs (vda): mounting ext2 file system using the ext4 subsystem
[    0.915200] EXT4-fs (vda): mounted filesystem without journal. Opts: (null)
[    0.915600] VFS: Mounted root (ext2 filesystem) on device 254:0.
[    0.916800] devtmpfs: mounted
[    0.917200] Freeing unused kernel memory: 84K
[    0.917600] This architecture does not have kernel memory protection.
~ # pwd
/root
~ # halt -f
[    1.005100] reboot: System halted
Power off
```


## Resources

[jslinux]: https://bellard.org/jslinux/
[nix]: https://nixos.org/
[truebit-eth]: https://github.com/TruebitProtocol/truebit-eth
