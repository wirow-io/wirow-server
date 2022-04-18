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

import type { Writable } from 'svelte/store';
import { Logger } from './logger';
import { catchLogResolve } from './utils';

const log = new Logger('db');

/// Database front interface.
export interface KVDB {
  get<T>(key: string): Promise<T>;
  put(key: string, entry: any): Promise<void>;
  delete(key: string): Promise<void>;
  deleteAll(): Promise<void>;
  count(): Promise<number>;
  db: IDBDatabase;
}

export function acquireKVDB(dbName: string): Promise<KVDB> {
  return new Promise<KVDB>((resolve, reject) => {
    const dbr = window.indexedDB.open(dbName, 2);
    const storeName = `${dbName}_store`;
    let db: IDBDatabase;

    type TxType = 'readonly' | 'readwrite';

    const _query = (method: string, txType: TxType, ...params: any[]) =>
      new Promise<any>((resolveQuery, rejectQuery) => {
        if (!db.objectStoreNames.contains(storeName)) {
          rejectQuery(new Error(`Store ${storeName} not found`));
          return;
        }
        const tx = db.transaction(storeName, txType);
        const store = tx.objectStore(storeName);
        const listener = store[method](...params);
        listener.oncomplete = (event) => {
          resolveQuery(event.target.result);
        };
        listener.onsuccess = (event) => {
          resolveQuery(event.target.result);
        };
        listener.onerror = (event) => {
          rejectQuery(event);
        };
      });

    const methods = {
      get: (key) => _query('get', 'readonly', key),
      count: () => _query('count', 'readonly'),
      put: (key: string, entry: any) => _query('put', 'readwrite', entry, key),
      delete: (key) => _query('delete', 'readwrite', key),
      deleteAll: () => _query('clear', 'readwrite'),
    } as KVDB;

    dbr.onupgradeneeded = () => {
      db = dbr.result;
      try {
        db.deleteObjectStore(storeName);
      } catch (_) {
        // ignored
      }
      db.createObjectStore(storeName);
    };

    dbr.onsuccess = () => {
      db = dbr.result;
      resolve(methods);
    };

    dbr.onerror = (e) => {
      reject(e);
    };
  });
}

export const dbPromise = acquireKVDB('greenrooms');

export const dbPromiseOrNull = dbPromise.catch(() => null);

async function makeStoreAsPersistentImpl(key: string, store: Writable<any>): Promise<() => void> {
  const db = await dbPromise;
  const entry = await db.get(key);
  let firstRun = true;
  if (entry != null) {
    store.set(entry);
  }
  return store.subscribe(async (val) => {
    if (firstRun) {
      firstRun = false;
      return;
    }
    if (val != null) {
      await db.put(key, val);
    } else {
      await db.delete(key);
    }
  });
}

export function makeStoreAsPersistent(key: string, store: Writable<any>): Promise<() => void> {
  return makeStoreAsPersistentImpl(key, store).catch((e) => {
    log.warn(`Failed to make store as persistent`, e);
    return () => {};
  });
}
