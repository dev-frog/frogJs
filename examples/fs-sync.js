console.log("Testing synchronous file operations...");

// Write a file synchronously
console.log("Writing test.txt...");
fs.writeFileSync("test.txt", "Hello from FrogJS!\nThis is a test file.");
console.log("File written successfully");

// Read the file synchronously
console.log("Reading test.txt...");
const content = fs.readFileSync("test.txt");
console.log("File content:");
console.log(content);

console.log("Sync file operations complete!");
