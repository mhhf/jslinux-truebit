## JSLinux on Truebit

This project tries to create a trustless ci/cd setup.
With trustless ci/cd setup I mean in particular building and
(cros-)compiling binaries in a trusless way.
The general Idea is to compile a [instruction set architecture](https://en.wikipedia.org/wiki/Instruction_set_architecture) to wasm which runs
linux on truebit and run all build tasks within that VM on truebit.

This repo is a fork of Bellard's [jslinux] project modefied
to be run as a standalone wasm from a single main() function.
It assumes a fs with multiple input files:

- bbl64.bin - the bios
- kernel-riscv64.bin - the kernel
- root-riscv64.bin - the root disk
- stdin.txt - stdin
- stdout.txt - stdout

The root disk is a ext2 file system with a minimal linux installation,
which gets booted. After the boot the content of stdin.txt is piped to
the machines stdin while the stdout is saved to stdout.txt.

## Requiremens

This is build with [nix] which you can get [here](https://nixos.org/download.html).
If you want to run it with the truebit interpreter, make sure
you have [truebit-eth] repo checked out by asking for it [here](https://gitter.im/TruebitProtocol/community).

## Usage

Right now two slightly different implementations for different
environments are maintained. The nodejs environment (on the node-fs branch)
is a working reference environment. The ocam-offchain/interpreter
(on the master branch) is a WIP implementation meant to be run on truebit-os.

### nodejs

The `node-fs` git branch contains a working
environment run with nodejs. Run it with:

```
git checkout node-fs
nix-shell
make clean && make && make run
```
This will build the wasm-vm, run it with node and display the output.
With a stdin containing `pwd\nhalt -f\n` you should see the output
looking like the following:
```
[    1.024542] NET: Registered protocol family 17
[    1.026031] 9pnet: Installing 9P2000 support
[    1.037634] EXT4-fs (vda): couldn't mount as ext3 due to feature incompatibilities
[    1.039993] EXT4-fs (vda): mounting ext2 file system using the ext4 subsystem
[    1.061024] EXT4-fs (vda): mounted filesystem without journal. Opts: (null)
[    1.061748] VFS: Mounted root (ext2 filesystem) on device 254:0.
[    1.063807] devtmpfs: mounted
[    1.064979] Freeing unused kernel memory: 84K
[    1.065396] This architecture does not have kernel memory protection.
~ # pwd
/root
~ # halt -f
[    1.460055] reboot: System halted
Power off
```

### ocaml-offchain/interpreter

This environment is meant to be run on truebit.
Right now its not working (see TODOs).

Make sure to edit your correct `TRUEBIT_PATH` in the `default.nix`.

Run it with:
```
git checkout master
nix-shell
make clean && make && make run
```

This will build the wasm (v1.37.36) binary, prepare it with truebits
`emscripten-module-wrapper` and run it with the `ocaml-offchain/interpreter/wasm`.
Right now it will get stuck at some point. #sad

## TODO

Regardless of being build with a large `TOTAL_MEMORY` of 1GB and
`ALLOW_MEMORY_GROWTH` being disabled by default, when loading
larger files (e.g. `kernel-riscv64.bin`) the `env . enlargeMemory`
is called which isn't implemented in the ocaml-offchain/interpreter.
I don't understand why its called to begin with, yet.



## Resources

[jslinux]: https://bellard.org/jslinux/
[nix]: https://nixos.org/
[truebit-eth]: https://github.com/TruebitProtocol/truebit-eth
