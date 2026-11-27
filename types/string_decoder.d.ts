declare module 'string_decoder' {
    /** Decode Buffers into strings without splitting a multibyte character between two chunks. */
    class StringDecoder {
        constructor(encoding?: BufferEncoding);
        readonly encoding: BufferEncoding;
        /** The decoded text; bytes of an unfinished character are kept for the next call. */
        write(buffer: string | Buffer | ArrayBufferView): string;
        /** Decode `buffer` (if any) and whatever is left; incomplete characters become U+FFFD. */
        end(buffer?: string | Buffer | ArrayBufferView): string;
        text(buffer: Buffer, offset: number): string;
        fillLast(buffer: Buffer): string | undefined;
    }
}
