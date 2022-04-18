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

import { derived, readable } from 'svelte/store';
import { UUID_REGEX } from '../constants';
import type { SComponent } from '../interfaces';

export type RouterSpec = [SComponent, string | ((segment: string) => boolean), { [key: string]: any }?];

export const forceRoutersHooks = new Array<() => void>();

export interface RouterUrl {
  url: URL;
  segments: string[];
}

export interface RouterPosition extends RouterUrl {
  level: number;
  spec: RouterSpec;
}

function parseLocation(location: Location): RouterUrl {
  let hash = location.hash;
  while (hash.charAt(0) == '#' || hash.charAt(0) == '/') {
    hash = hash.substring(1);
  }
  while (hash.endsWith('/')) {
    hash = hash.substring(0, hash.length - 1);
  }
  const segments = hash.split('/');
  return {
    url: new URL(location.toString()),
    segments,
  };
}

export const routeUrlStore = readable<RouterUrl>(parseLocation(window.location), (set) => {
  const update = () => {
    set(parseLocation(window.location));
  };
  update();
  window.addEventListener('hashchange', update, false);
  return function () {
    window.removeEventListener('hashchange', update, false);
  };
});

export const roomBrowserUUID = derived<typeof routeUrlStore, string | undefined>(routeUrlStore, (d) => {
  const firstSegment = d.segments[0];
  if (firstSegment == null || !UUID_REGEX.test(firstSegment)) {
    return undefined;
  }
  return firstSegment;
});

export function route(name: string, force = false): boolean {
  if (name.charAt(0) != '#') {
    name = `#${name}`;
  }
  if (window.location.hash == name) {
    if (force) {
      forceRoutersHooks.forEach((f) => f());
    }
    return force;
  } else {
    window.location.hash = name;
    return true;
  }
}

export function routeUrl(name: string): string {
  if (name.charAt(0) != '#') {
    name = `#${name}`;
  }
  let loc = `${window.location}`;
  const idx = loc.lastIndexOf('#');
  if (idx !== -1) {
    loc = loc.substring(0, idx);
  }
  return `${loc}${name}`;
}

export function routeResetPath(newPart: string) {
  let loc = `${window.location}`;
  const idx = loc.indexOf('#');
  if (idx !== -1) {
    loc = loc.substring(0, idx);
  }
  while (loc.endsWith('/')) {
    loc = loc.substring(0, loc.length - 1);
  }
  if (newPart.charAt(0) !== '/') {
    newPart = '/' + newPart;
  }
  if (loc.indexOf('.html') !== -1) {
    loc = loc.replace(/\/[a-zA-Z0-9_]+\.html/, newPart);
  } else {
    loc += newPart;
  }
  window.location.assign(loc);
}
