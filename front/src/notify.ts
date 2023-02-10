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

// @ts-ignore
import { openModal } from './kit/Modal.svelte';
// @ts-ignore
import { sendNotification as _sendNotification } from './kit/Notifications.svelte';
// @ts-ignore
import FatalModal from './kit/FatalModal.svelte';

export const recommendedTimeout = 4000;

export const onFatalSubscribers = new Array<() => void>();

export interface Notification {
  text?: string;
  html?: string;
  component?: any;
  style?: 'error' | 'warning';
  timeout?: number;
  icon?: any;
  closeable?: boolean;
  onAction?: (n: Notification) => void;
  onClick?: (n: Notification) => void;
  onClose?: (n: Notification) => void;
  actionTitle?: string;
  close?: () => void;
}

export function sendNotification(n: Notification): void {
  _sendNotification(n);
}

export function setFatal(title: string, content?: string): () => void {
  onFatalSubscribers.forEach((s) => s());
  return openModal(
    FatalModal,
    {
      closeOnOutsideClick: false,
      closeOnEscape: false,
    },
    {
      title,
      content,
    }
  );
}
