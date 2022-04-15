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
        const store: any = tx.objectStore(storeName);
        const listener = store[method](...params);
        listener.oncomplete = (event: any) => {
          resolveQuery(event.target.result);
        };
        listener.onsuccess = (event: any) => {
          resolveQuery(event.target.result);
        };
        listener.onerror = (event: any) => {
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