// logger.js - Nested module example

function log(message) {
    console.log('[LOG]', message);
}

function error(message) {
    console.error('[ERROR]', message);
}

function info(message) {
    console.log('[INFO]', message);
}

module.exports = {
    log: log,
    error: error,
    info: info
};
