#
# TinyEMU emulator
#
# Copyright (c) 2016-2018 Fabrice Bellard
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

# Build the Javascript version of TinyEMU
EMCC=emcc
EMCFLAGS=-O0 -g --llvm-opts 2 -Wall -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -MMD -fno-strict-aliasing
#EMCFLAGS+=-Werror
EMLDFLAGS=-O0 -g --memory-init-file 0 --closure 0 -s FILESYSTEM=1 -s "EXPORTED_FUNCTIONS=['_console_queue_char','_vm_start', '_main']" -s 'EXTRA_EXPORTED_RUNTIME_METHODS=["ccall", "cwrap", "UTF8ToString", "FS", "NODEFS"]' -lnodefs.js
EMLDFLAGS_ASMJS:=$(EMLDFLAGS) -s WASM=0
EMLDFLAGS_WASM:=$(EMLDFLAGS) -s WASM=1 -s TOTAL_MEMORY=67108864 -s ALLOW_MEMORY_GROWTH=1

PROGS=build/riscvemu64-wasm.js build/run.js build/r64.wasm

all: $(PROGS)

JS_OBJS=build/jsemu.js.o build/softfp.js.o build/virtio.js.o build/fs.js.o build/fs_utils.js.o build/pci.js.o build/json.js.o
JS_OBJS+=build/iomem.js.o build/cutils.js.o build/aes.js.o build/sha256.js.o

RISCVEMU64_OBJS=$(JS_OBJS) build/riscv_cpu64.js.o build/riscv_machine.js.o build/machine.js.o
RISCVEMU32_OBJS=$(JS_OBJS) build/riscv_cpu32.js.o build/riscv_machine.js.o build/machine.js.o

build/riscvemu64-wasm.js: $(RISCVEMU64_OBJS) js/lib.js
	mkdir -p build
	$(EMCC) $(EMLDFLAGS_WASM) -o $@ $(RISCVEMU64_OBJS)

build/riscv_cpu64.js.o: src/riscv_cpu.c
	mkdir -p build
	$(EMCC) $(EMCFLAGS) -DMAX_XLEN=64 -DCONFIG_RISCV_MAX_XLEN=64 -c -o $@ $<

build/%.js.o: src/%.c
	mkdir -p build
	$(EMCC) $(EMCFLAGS) -c -o $@ $<

build/run.js: web/run.js
	mkdir -p build
	cp -r web/* build/

build/r64.wasm: build/riscvemu64-wasm.wasm
	wasm2wat build/riscvemu64-wasm.wasm > build/riscvemu64-wasm.wat
	sed -i 's/wasi_snapshot_preview1/env/g' build/riscvemu64-wasm.wat
	wat2wasm build/riscvemu64-wasm.wat --debug-names -o build/r64.wasm

-include $(wildcard *.d)

run:
	echo "" > build/out.txt
	node build/run.js
	cat build/out.txt

clean:
	rm -fdR build
