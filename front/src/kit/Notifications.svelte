<script lang="ts" context="module">
  export interface Notification {
    text?: string;
    html?: string;
    component?: any;
    style?: 'error' | 'warning';
    timeout?: number;
    icon?: any;
    closeable?: boolean;
    closeOnClick?: boolean;
    onAction?: (n: Notification) => void;
    onClick?: (n: Notification) => void;
    onClose?: (n: Notification) => void;
    actionTitle?: string;
    close?: () => void;
  }

  interface InternalNotification extends Notification {
    timeoutHandle?: number | any;
  }

  let addNotificationHook: ((n: Notification) => void) | undefined = undefined;

  let pendingNotifications: Notification[] = [];

  export function sendNotification(n: Notification) {
    if (!addNotificationHook) {
      console.warn('<Notifications /> is not mounted');
      pendingNotifications.push(n);
      return;
    }
    addNotificationHook({ ...n });
  }
</script>

<script lang="ts">
  import { onMount } from 'svelte';
  import { fade } from 'svelte/transition';
  import Box from './Box.svelte';
  import Button from './Button.svelte';

  export let notifications: InternalNotification[] = [];
  export let timeout: number = 0;
  export let closeOnClick: boolean = false;
  export let maxOpenNotifications: number = 15;

  let containerRef: HTMLElement | undefined;

  onMount(() => {
    const ref = containerRef!;
    document.body.appendChild(ref);
    addNotificationHook = (n) => {
      notifications.unshift(n);
      while (notifications.length > maxOpenNotifications) {
        notifications.pop();
      }
      notifications = notifications;
      n.close = () => close(n);
    };

    [...pendingNotifications].forEach((n) => addNotificationHook?.(n));
    pendingNotifications = [];

    return () => {
      notifications.forEach((n) => n.timeoutHandle && clearTimeout(n.timeoutHandle));
      document.body.removeChild(ref);
    };
  });

  function getBoxClass(n: InternalNotification): string {
    if (n.style != null) {
      return n.style;
    } else {
      return 'primary';
    }
  }

  function close(n: InternalNotification) {
    notifications = notifications.filter((nn) => nn != n);
    n.onClose && n.onClose(n);
  }

  function clicked(n: InternalNotification) {
    n.onClick && n.onClick(n);
    if (n.closeOnClick === true || closeOnClick) {
      close(n);
    }
  }

  function closeDelayed(n: InternalNotification): InternalNotification {
    const d = n.timeout || timeout;
    if (!d) {
      return n;
    }
    n.timeoutHandle = setTimeout(() => {
      n.timeoutHandle = 0;
      close(n);
    }, d);
    return n;
  }
</script>

<template>
  <div id="notification-holder" bind:this={containerRef}>
    {#each notifications.map(closeDelayed) as n, _ (n)}
      <Box
        hoverable
        closeable={n.closeable != null ? n.closeable : true}
        icon={n.icon}
        transitionFn={fade}
        componentClass="notification {getBoxClass(n)}"
        on:close={() => close(n)}
        on:click={() => clicked(n)}>
        {#if n.html !== undefined}
          {@html n.html}
        {:else if n.component !== undefined}
          <svelte:component this={n.component} />
        {:else}{n.text || ''}{/if}

        {#if n.actionTitle !== undefined}
          <div>
            <Button componentClass="outline s" on:click={() => n.onAction && n.onAction(n)}>{n.actionTitle}</Button>
          </div>
        {/if}
      </Box>
    {/each}
  </div>
</template>
