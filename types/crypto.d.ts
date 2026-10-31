// Implemented without OpenSSL: hashes, HMAC, PBKDF2 and random values only.

declare module 'crypto' {
    import { Transform, TransformOptions } from 'stream';

    /** md5, sha1, sha224, sha256, sha384 or sha512 (case-insensitive; `SHA-256` and `RSA-SHA256` also work). */
    type HashAlgorithm = 'md5' | 'sha1' | 'sha224' | 'sha256' | 'sha384' | 'sha512' | (string & {});
    type BinaryLike = string | Buffer | ArrayBufferView;
    type BinaryToTextEncoding = 'hex' | 'base64' | 'base64url' | 'latin1' | 'binary';

    /** Also a Transform stream: write data in, read the digest out. */
    class Hash extends Transform {
        private constructor();
        /** Strings are UTF-8 unless `inputEncoding` says otherwise. Throws after digest(). */
        update(data: BinaryLike, inputEncoding?: BufferEncoding): this;
        digest(): Buffer;
        digest(encoding: BinaryToTextEncoding): string;
        /** An independent copy of the current state. */
        copy(options?: TransformOptions): Hash;
    }

    class Hmac extends Transform {
        private constructor();
        update(data: BinaryLike, inputEncoding?: BufferEncoding): this;
        digest(): Buffer;
        digest(encoding: BinaryToTextEncoding): string;
    }

    function createHash(algorithm: HashAlgorithm, options?: TransformOptions): Hash;
    function createHmac(algorithm: HashAlgorithm, key: BinaryLike, options?: TransformOptions): Hmac;
    /** The supported hash names. */
    function getHashes(): string[];

    /** Derive a key on the thread pool. */
    function pbkdf2(password: BinaryLike, salt: BinaryLike, iterations: number, keylen: number, digest: HashAlgorithm,
                    callback: (err: Error | null, derivedKey: Buffer) => void): void;
    function pbkdf2Sync(password: BinaryLike, salt: BinaryLike, iterations: number, keylen: number, digest: HashAlgorithm): Buffer;

    /** Bytes from the OS CSPRNG. */
    function randomBytes(size: number): Buffer;
    function randomBytes(size: number, callback: (err: Error | null, buf: Buffer) => void): void;
    /** A random integer in [min, max) (min defaults to 0). */
    function randomInt(max: number): number;
    function randomInt(min: number, max: number): number;
    function randomInt(max: number, callback: (err: Error | null, value: number) => void): void;
    function randomInt(min: number, max: number, callback: (err: Error | null, value: number) => void): void;
    /** A random RFC 4122 version 4 UUID. */
    function randomUUID(options?: { disableEntropyCache?: boolean }): string;
    /** Fill an integer typed array with random values (at most 65536 bytes) and return it. */
    function getRandomValues<T extends ArrayBufferView>(typedArray: T): T;

    /** Compare two equal-length byte sequences in constant time. */
    function timingSafeEqual(a: ArrayBufferView | ArrayBuffer | Buffer, b: ArrayBufferView | ArrayBuffer | Buffer): boolean;
}
