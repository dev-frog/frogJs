declare module 'events' {
    type Listener = (...args: any[]) => void;

    class EventEmitter {
        constructor();

        /** Default limit of listeners per event before a warning (10). */
        static defaultMaxListeners: number;
        /** Listen with this to see 'error' events without handling them. */
        static readonly errorMonitor: unique symbol;
        /** Resolve with the arguments of the next `event` (reject on 'error'). */
        static once(emitter: EventEmitter, event: string | symbol): Promise<any[]>;
        /** Iterate over the arguments of every `event`. */
        static on(emitter: EventEmitter, event: string | symbol): AsyncIterableIterator<any[]>;
        static listenerCount(emitter: EventEmitter, event: string | symbol): number;
        static getEventListeners(emitter: EventEmitter, event: string | symbol): Function[];

        addListener(event: string | symbol, listener: Listener): this;
        on(event: string | symbol, listener: Listener): this;
        once(event: string | symbol, listener: Listener): this;
        prependListener(event: string | symbol, listener: Listener): this;
        prependOnceListener(event: string | symbol, listener: Listener): this;
        removeListener(event: string | symbol, listener: Listener): this;
        off(event: string | symbol, listener: Listener): this;
        removeAllListeners(event?: string | symbol): this;
        /** Call the listeners of `event`; true if there were any. */
        emit(event: string | symbol, ...args: any[]): boolean;
        listeners(event: string | symbol): Function[];
        rawListeners(event: string | symbol): Function[];
        listenerCount(event: string | symbol): number;
        eventNames(): Array<string | symbol>;
        setMaxListeners(n: number): this;
        getMaxListeners(): number;
    }

    namespace EventEmitter {
        export { EventEmitter };
    }

    export = EventEmitter;
}
