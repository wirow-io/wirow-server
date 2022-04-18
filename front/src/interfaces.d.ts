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

import type { SvelteComponent } from 'svelte';

export type NU = null | undefined;

export type EnumLiteralsOf<T extends Record<string, unknown>> = T[keyof T];

export type OptionalBy<T, K extends keyof T> = Pick<Partial<T>, K> & Omit<T, K>;

export type RequiredBy<T, K extends keyof T> = Pick<Required<T>, K> & Omit<T, K>;

/**
 * extracts union type from tuple
 *
 * @example
 *
 * ```ts
 * type Tuple = [number, string, boolean]
 * // $ExpectType number | string | boolean
 * type Test = UnionFromTuple<Tuple>
 * ```
 */
export type UnionFromTuple<T> = T extends (infer U)[] ? U : never;

//
// Svelte
//

export type SComponent = typeof SvelteComponent;

export type SEvent<T = any> = Event & { detail?: T };

export type SBindFn<B> = (me: SvelteComponent, bind: B) => void;

//
// App
//

export type RoomType = 'meeting' | 'webinar';

export interface RoomHistoryEntry {
  uuid: string;
  cid?: string;
  ts: number;
  name: string;
}
