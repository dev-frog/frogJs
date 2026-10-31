declare module 'os' {
    interface CpuInfo {
        model: string;
        /** MHz */
        speed: number;
        /** Milliseconds spent in each mode. */
        times: {
            user: number;
            nice: number;
            sys: number;
            idle: number;
            irq: number;
        };
    }

    /** `'\n'` */
    const EOL: string;

    /** `'arm64'`, `'x64'`, ... */
    function arch(): string;
    function cpus(): CpuInfo[];
    /** Free memory in bytes. */
    function freemem(): number;
    /** Total memory in bytes. */
    function totalmem(): number;
    function homedir(): string;
    function hostname(): string;
    /** `'darwin'`, `'linux'`, ... */
    function platform(): string;
    /** Kernel release, e.g. `'25.5.0'`. */
    function release(): string;
    function tmpdir(): string;
    /** `'Darwin'`, `'Linux'`, ... */
    function type(): string;
}
