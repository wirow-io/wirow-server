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

import { createEventDispatcher, onMount } from 'svelte';
import type { Readable } from 'svelte/store';
import { get, readable } from 'svelte/store';
import type { NU, UnionFromTuple } from './interfaces';
import type { Logger } from './logger';

export function catchLogResolve<T = any>(log?: Logger, msg?: string, resolve?: T): (e: any) => PromiseLike<T> | T | NU {
  return function (e: any) {
    if (log) {
      log.error(msg != null ? msg : 'catchLogResolve ', e);
    } else {
      console.error(msg != null ? msg : 'catchLogResolve', e);
    }
    return resolve != null ? resolve : null;
  };
}

export function catchLogReject<T = any>(log?: Logger, msg?: string, reject?: T): (e: any) => PromiseLike<T> | T | NU {
  return function (e: any) {
    if (log) {
      log.error(msg != null ? msg : 'catchLogReject ', e);
    } else {
      console.error(msg != null ? msg : 'catchLogReject', e);
    }
    return Promise.reject(reject != null ? reject : e);
  };
}

export function ifClass(spec: string | NU, clazz: string): boolean {
  if (typeof spec !== 'string') {
    return false;
  }
  return spec.split(/\s/).indexOf(clazz) !== -1;
}

export function ifClassThen(spec: string | NU, clazz: string | string[], ret: any | any[], otherwise: any = null): any {
  if (typeof spec !== 'string') {
    return otherwise;
  }
  const sv = spec.split(/\s/);
  if (Array.isArray(clazz)) {
    for (let i = 0; i < clazz.length; ++i) {
      if (sv.indexOf(clazz[i]) !== -1) {
        return Array.isArray(ret) ? ret[i] : ret;
      }
    }
    return otherwise;
  } else {
    return sv.indexOf(clazz) !== -1 ? ret : otherwise;
  }
}

export function isPromise<T = any>(value): value is Promise<T> {
  return Boolean(value && typeof value.then === 'function');
}

export function isTouchEvent(evt: UIEvent): evt is TouchEvent {
  return evt != null && 'touches' in evt;
}

export function createExtEventDispatcher(): (type: string, detail?: any) => void {
  const pending: [string, any?][] = [];
  const dispatch = createEventDispatcher();
  let mounted = false;

  onMount(() => {
    pending.forEach((p) => dispatch(p[0], p[1]));
    mounted = true;
    return () => {
      mounted = false;
    };
  });
  return (type: string, detail?: any) => {
    if (mounted) {
      dispatch(type, detail);
    } else {
      while (pending.length > 1000) {
        pending.shift();
      }
      pending.push([type, detail]);
    }
  };
}

export function storeGet<T>(store: Readable<T>): T {
  return get(store);
}

export const emptyReadableArray = readable<any>([], () => {});

export const emptyReadableNull = readable<any>(null, () => {});

export const emptyReadableString = readable<string>('', () => {});

export const emptyReadableFalse = readable<boolean>(false, () => {});

// eslint-disable-next-line @typescript-eslint/explicit-module-boundary-types
export const Enum = <T extends string[]>(...args: T) => {
  return Object.freeze(
    args.reduce((acc, next) => {
      return {
        ...acc,
        [next]: next,
      };
    }, Object.create(null)) as { [P in UnionFromTuple<typeof args>]: P }
  );
};

export async function copyToClipboard(data: string): Promise<boolean> {
  return navigator.clipboard
    .writeText(data)
    .then(() => true)
    .catch(() => false);
}

export function html2json(html: string): any {
  function child2json(child: ChildNode): any {
    if (child.nodeType == child.TEXT_NODE) {
      return child.nodeValue;
    }
    if (child.nodeType == child.ELEMENT_NODE) {
      const node = child as HTMLElement;
      const attributes = Array.from(node.attributes).map((v) => ({ [v.name]: v.value }));
      const children = Array.from(node.childNodes)
        .map((c) => child2json(c))
        .filter((c) => c !== null);
      const json: any = { t: node.tagName };
      if (attributes.length) {
        json.a = Object.assign({}, ...attributes);
      }
      if (children.length) {
        json.c = children;
      }
      return json;
    }
    return null;
  }

  const doc = new DOMParser().parseFromString(html, 'text/html');
  return Array.from(doc.body.childNodes).map((c) => child2json(c));
}
