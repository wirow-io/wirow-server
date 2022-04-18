/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

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
