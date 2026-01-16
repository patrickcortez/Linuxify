//!node
const readline = require('readline');

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

console.log("Node.js: Checking isTTY...");
console.log("stdin.isTTY:", process.stdin.isTTY);
console.log("stdout.isTTY:", process.stdout.isTTY);

rl.question('Node.js: Type something: ', (answer) => {
    console.log(`Node.js: You typed: ${answer}`);
    rl.close();
});
