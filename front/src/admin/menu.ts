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

import { derived, writable } from 'svelte/store';
import type { SEvent } from '../interfaces';

export interface MenuSlot {
  id: string;
  icon: string;
  onAction: (ev: SEvent) => void;
  tooltip: () => string;
  props?: Record<string, any>;
}

export const menuSlots = writable<Array<MenuSlot[]>>([]);

export const topSlots = derived<typeof menuSlots, MenuSlot[]>(menuSlots, (slots) => {
  return slots[slots.length - 1] || [];
});

export function menuPush(...slots: MenuSlot[]): void {
  menuSlots.update((s) => {
    s.push(slots);
    return s;
  });
}

export function menuPop(): void {
  menuSlots.update((s) => {
    s.pop();
    return s;
  });
}
