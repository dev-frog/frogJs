// Buffer: a Uint8Array subclass, as in Node.js. Also `require('buffer').Buffer`.

/** Encoding names are case-insensitive at run time; an unknown name throws ERR_UNKNOWN_ENCODING. */
type BufferEncoding = 'utf8' | 'utf-8' | 'hex' | 'base64' | 'base64url' | 'latin1' | 'binary' | 'ascii';

// The static Uint8Array.from() and of() are replaced, so the base is declared
// without them
declare namespace FrogJS {
    const BufferBase: { new (...args: any[]): Uint8Array<ArrayBuffer>; readonly prototype: Uint8Array<ArrayBuffer> };
}

declare class Buffer extends FrogJS.BufferBase {
    /** @deprecated Use Buffer.alloc(size) or Buffer.from(). */
    constructor(size: number);
    /** @deprecated Use Buffer.from(). */
    constructor(data: string | ArrayBuffer | readonly number[], encodingOrOffset?: BufferEncoding | number, length?: number);

    /** Decode bytes `start` to `end` (default: all of them) as `encoding` (default `'utf8'`). */
    toString(encoding?: BufferEncoding, start?: number, end?: number): string;
    toLocaleString(): string;
    toJSON(): { type: 'Buffer'; data: number[] };
    /** Write `string` at `offset` (default 0). Returns the bytes written; a character that doesn't fit isn't written. */
    write(string: string, encoding?: BufferEncoding): number;
    write(string: string, offset: number, encoding?: BufferEncoding): number;
    write(string: string, offset: number, length: number, encoding?: BufferEncoding): number;
    /** A view of the same memory (unlike Uint8Array's slice, as in Node.js). */
    slice(start?: number, end?: number): Buffer;
    subarray(start?: number, end?: number): Buffer;
    equals(other: Uint8Array): boolean;
    /** -1, 0 or 1. */
    compare(target: Uint8Array, targetStart?: number, targetEnd?: number, sourceStart?: number, sourceEnd?: number): number;
    /** Copy into `target`; returns the bytes copied. */
    copy(target: Uint8Array, targetStart?: number, sourceStart?: number, sourceEnd?: number): number;
    fill(value: string | number | boolean | Uint8Array, offset?: number, end?: number, encoding?: BufferEncoding): this;
    fill(value: string | number | boolean | Uint8Array, offset: number, encoding: BufferEncoding): this;
    fill(value: string | number | boolean | Uint8Array, encoding: BufferEncoding): this;
    indexOf(value: string | number | Uint8Array, byteOffset?: number | BufferEncoding, encoding?: BufferEncoding): number;
    lastIndexOf(value: string | number | Uint8Array, byteOffset?: number | BufferEncoding, encoding?: BufferEncoding): number;
    includes(value: string | number | Uint8Array, byteOffset?: number | BufferEncoding, encoding?: BufferEncoding): boolean;

    readUInt8(offset?: number): number;
    readUint8(offset?: number): number;
    readInt8(offset?: number): number;
    readUInt16LE(offset?: number): number;
    readUint16LE(offset?: number): number;
    readUInt16BE(offset?: number): number;
    readUint16BE(offset?: number): number;
    readInt16LE(offset?: number): number;
    readInt16BE(offset?: number): number;
    readUInt32LE(offset?: number): number;
    readUint32LE(offset?: number): number;
    readUInt32BE(offset?: number): number;
    readUint32BE(offset?: number): number;
    readInt32LE(offset?: number): number;
    readInt32BE(offset?: number): number;
    readBigUInt64LE(offset?: number): bigint;
    readBigUint64LE(offset?: number): bigint;
    readBigUInt64BE(offset?: number): bigint;
    readBigUint64BE(offset?: number): bigint;
    readBigInt64LE(offset?: number): bigint;
    readBigInt64BE(offset?: number): bigint;
    readFloatLE(offset?: number): number;
    readFloatBE(offset?: number): number;
    readDoubleLE(offset?: number): number;
    readDoubleBE(offset?: number): number;

    /** The write methods return the offset after the bytes written. */
    writeUInt8(value: number, offset?: number): number;
    writeUint8(value: number, offset?: number): number;
    writeInt8(value: number, offset?: number): number;
    writeUInt16LE(value: number, offset?: number): number;
    writeUint16LE(value: number, offset?: number): number;
    writeUInt16BE(value: number, offset?: number): number;
    writeUint16BE(value: number, offset?: number): number;
    writeInt16LE(value: number, offset?: number): number;
    writeInt16BE(value: number, offset?: number): number;
    writeUInt32LE(value: number, offset?: number): number;
    writeUint32LE(value: number, offset?: number): number;
    writeUInt32BE(value: number, offset?: number): number;
    writeUint32BE(value: number, offset?: number): number;
    writeInt32LE(value: number, offset?: number): number;
    writeInt32BE(value: number, offset?: number): number;
    writeBigUInt64LE(value: bigint, offset?: number): number;
    writeBigUint64LE(value: bigint, offset?: number): number;
    writeBigUInt64BE(value: bigint, offset?: number): number;
    writeBigUint64BE(value: bigint, offset?: number): number;
    writeBigInt64LE(value: bigint, offset?: number): number;
    writeBigInt64BE(value: bigint, offset?: number): number;
    writeFloatLE(value: number, offset?: number): number;
    writeFloatBE(value: number, offset?: number): number;
    writeDoubleLE(value: number, offset?: number): number;
    writeDoubleBE(value: number, offset?: number): number;

    /** A zero-filled Buffer, or one filled with `fill`. */
    static alloc(size: number, fill?: string | number | Uint8Array, encoding?: BufferEncoding): Buffer;
    /** Zero-filled too: FrogJS never hands out uninitialized memory. */
    static allocUnsafe(size: number): Buffer;
    static allocUnsafeSlow(size: number): Buffer;
    /** Encode a string (default `'utf8'`). */
    static from(string: string, encoding?: BufferEncoding): Buffer;
    /** A view of the ArrayBuffer's memory. */
    static from(buffer: ArrayBuffer, byteOffset?: number, length?: number): Buffer;
    /** Copy a Buffer, typed array (element values modulo 256), DataView, SharedArrayBuffer or array of byte values. */
    static from(data: Uint8Array | ArrayBufferView | SharedArrayBuffer | readonly number[] | { type: 'Buffer'; data: number[] } | ArrayLike<number>): Buffer;
    /** Length in bytes of `string` encoded as `encoding`, or of binary data. */
    static byteLength(string: string, encoding?: BufferEncoding): number;
    static byteLength(data: ArrayBufferView | ArrayBuffer | SharedArrayBuffer): number;
    /** Join into a new Buffer, optionally truncated or zero-padded to `totalLength`. */
    static concat(list: readonly Uint8Array[], totalLength?: number): Buffer;
    static compare(a: Uint8Array, b: Uint8Array): number;
    static isBuffer(value: unknown): value is Buffer;
    static isEncoding(encoding: unknown): boolean;
    /** Always 0: FrogJS doesn't pool allocations. */
    static poolSize: number;
}

type FrogJSBufferClass = typeof Buffer;

declare module 'buffer' {
    const Buffer: FrogJSBufferClass;
    export { Buffer };
}
