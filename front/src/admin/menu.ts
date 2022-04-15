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
