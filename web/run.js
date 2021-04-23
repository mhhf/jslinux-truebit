const Module = require("./riscvemu64-wasm.js");

Module.FS.mount(Module.NODEFS, { root: __dirname + '/..' }, '/home')

Module.postRun = () => {
 Module.ccall("main", null, [], []);
}

Module.run();
