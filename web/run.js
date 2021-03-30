const Module = require("./riscvemu64-wasm.js");

const params =
  [ "http://localhost:8000/root-riscv64.cfg"
  , 128
  , ""
  , null
  , 0
  , 0
  , 0
  , ""
  ]

// console.log(Module);


const start = () => {
  // console.log("start");
  Module.ccall("vm_start", null, ["string", "number", "string", "string", "number", "number", "number", "string"], params);
}

Module.postRun = start

Module.run();

setTimeout(() => {
  console.log("end");
}, 20000000)
