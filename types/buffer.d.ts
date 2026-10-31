// Buffer: a global class (there is no 'buffer' module). A FrogJS Buffer is
// not a Uint8Array: read bytes with toString() or by copying it into a typed
// array, not with buf[i].

/** Encoding names are case-insensitive at run time; an unknown name throws ERR_UNKNOWN_ENCODING. */
type BufferEncoding = 'utf8' | 'utf-8' | 'hex' | 'base64' | 'base64url' | 'latin1' | 'binary' | 'ascii';

declare class Buffer {
    /** @deprecated Use Buffer.alloc(size). */
    constructor(size: number);

    /** Size in bytes. */
    readonly length: number;

    /** Decode bytes `start` to `end` (default: all of them) as `encoding` (default `'utf8'`). */
    toString(encoding?: BufferEncoding, start?: number, end?: number): string;
    /** Write `string` as UTF-8 at `offset` (default 0). Returns the number of bytes written. */
    write(string: string, offset?: number): number;
    /** A copy of bytes `start` to `end`. */
    slice(start?: number, end?: number): Buffer;

    /** A zero-filled Buffer of `size` bytes. */
    static alloc(size: number): Buffer;
    /** Encode a string (default `'utf8'`). */
    static from(string: string, encoding?: BufferEncoding): Buffer;
    /** Copy the bytes of a Buffer, typed array, DataView, ArrayBuffer or array of byte values. */
    static from(data: Buffer | ArrayBufferView | ArrayBuffer | SharedArrayBuffer | readonly number[]): Buffer;
    /** Length in bytes of `string` encoded as `encoding`, or of binary data. */
    static byteLength(string: string, encoding?: BufferEncoding): number;
    static byteLength(data: Buffer | ArrayBufferView | ArrayBuffer): number;
    /** Join Buffers into a new one, optionally truncated or zero-padded to `totalLength`. */
    static concat(list: readonly Buffer[], totalLength?: number): Buffer;
    static isBuffer(value: unknown): value is Buffer;
}
