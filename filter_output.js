const fs = require("fs");

const raw = fs.readFileSync("./master.out", "utf8");

const out = raw
  .split("\n")
  .filter(l => (/^DEBU5/).test(l))
  .map(l => l.slice(7))
  .filter(l => l.length > 0)
  .join("\n");

console.log(out);
