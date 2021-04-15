const Module = require("./riscvemu64-wasm.js");

Module.FS.mount(Module.NODEFS, { root: '/home/mhhf/src/jslinux' }, '/home')

Module.postRun = () => {
 Module.ccall("main", null, [], []);
}

Module.run();
