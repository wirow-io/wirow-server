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
