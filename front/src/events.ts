import { EventEmitter } from 'events';
import { Logger } from './logger';

const log = new Logger('events');

interface ExtendedEventEmitterOverride<T extends string | symbol> {
  addListener: (event: T, listener: (...args: any[]) => void) => this;
  removeListener: (event: T, listener: (...args: any[]) => void) => this;
  removeAllListeners: (event?: T) => this;
  // eslint-disable-next-line @typescript-eslint/ban-types
  listeners: (event: T) => Function[];
  // eslint-disable-next-line @typescript-eslint/ban-types
  rawListeners: (event: T) => Function[];
  listenerCount: (type: T) => number;
  prependListener: (event: T, listener: (...args: any[]) => void) => this;
  prependOnceListener: (event: T, listener: (...args: any[]) => void) => this;
  emit: (event: T, ...args: any[]) => boolean;
  on: (event: T, listener: (...args: any[]) => void) => this;
  once: (event: T, listener: (...args: any[]) => void) => this;
  off: (event: T, listener: (...args: any[]) => void) => this;
}

type ExtendedEventEmitterCtor = new <T extends string | symbol>() => {
  [P in keyof ExtendedEventEmitterOverride<T>]: ExtendedEventEmitterOverride<T>[P];
} &
  { [P in keyof Omit<EventEmitter, keyof ExtendedEventEmitterOverride<T>>]: EventEmitter[P] };

const ExtendedEventEmitterCtor: ExtendedEventEmitterCtor = EventEmitter as any;

export class ExtendedEventEmitter<T extends string | symbol = string> extends ExtendedEventEmitterCtor<T> {
  constructor() {
    super();
    this.setMaxListeners(Infinity);
  }
  safeEmit(event: T, ...args: any[]): boolean {
    const numListeners = this.listenerCount(event as any);
    try {
      return this.emit(event as any, ...args);
    } catch (error) {
      log.error('safeEmit() | event listener threw an error [event:%s]:%o', event, error);
      return Boolean(numListeners);
    }
  }
  async safeEmitAsPromise(event: T, ...args: any[]): Promise<any> {
    return new Promise((resolve, reject) => this.safeEmit(event, ...args, resolve, reject));
  }
}
