// @ts-ignore
import { openModal } from './kit/Modal.svelte';
// @ts-ignore
import { sendNotification as _sendNotification } from './kit/Notifications.svelte';
// @ts-ignore
import FatalModal from './ui/FatalModal.svelte';

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
