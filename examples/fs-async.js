console.log("Testing asynchronous file operations...");

// Write a file asynchronously
console.log("Writing async-test.txt...");
fs.writeFile("async-test.txt", "Async file content from FrogJS", (err) => {
    if (err) {
        console.error("Error writing file:", err);
        return;
    }
    console.log("File written successfully!");

    // Read the file asynchronously after writing
    console.log("Reading async-test.txt...");
    fs.readFile("async-test.txt", (err, data) => {
        if (err) {
            console.error("Error reading file:", err);
            return;
        }
        console.log("File content:");
        console.log(data);
        console.log("Async file operations complete!");
    });
});

console.log("Async operations initiated, waiting for callbacks...");
