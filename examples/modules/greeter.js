// greeter.js - Module with module.exports reassignment

class Greeter {
    constructor(name) {
        this.name = name;
    }

    greet() {
        return 'Hello, ' + this.name + '!';
    }

    farewell() {
        return 'Goodbye, ' + this.name + '!';
    }
}

// Export the class by reassigning module.exports
module.exports = Greeter;
