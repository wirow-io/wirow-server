import type { Readable } from 'svelte/store';
import { derived } from 'svelte/store';
import type { RoomType } from './interfaces';
import { getUserSnapshot, User, user } from './user';

export const canCreateMeeting = derived<Readable<User | undefined>, boolean>(
  user,
  (u) => u?.hasAnyPerms('user') ?? false
);

export const canUserAdminPanel = derived<Readable<User | undefined>, boolean>(
  user,
  (u) => u?.hasAnyPerms('admin') ?? false
);

export const canCreateWebinar = canCreateMeeting;

export const canJoinEvent = derived<Readable<User | undefined>, boolean>(
  user,
  (u) => u?.hasAnyPerms('user', 'guest') ?? false
);

export const isGuest = derived<Readable<User | undefined>, boolean>(user, (u) => u?.hasAnyPerms('guest') ?? true);

export function hasAnyPerms(...perms: string[]): boolean {
  return getUserSnapshot()?.hasAnyPerms(...perms) ?? false;
}

// eslint-disable-next-line @typescript-eslint/no-unused-vars
export function canCreateRoomOfType(type: RoomType): boolean {
  return hasAnyPerms('user');
}
