<script lang="ts" context="module">
  const registry: (() => void)[] = [];
</script>

<script lang="ts">
  import { onMount, setContext } from 'svelte';

  let isOpen: boolean = false;
  let popupRef: HTMLElement | undefined;

  function unregisterListeners() {
    document.removeEventListener('click', close);
    document.removeEventListener('keydown', onKeyDown);
    window.removeEventListener('resize', close);
  }

  function registerListeners() {
    document.addEventListener('click', close);
    document.addEventListener('keydown', onKeyDown);
    window.addEventListener('resize', close);
  }

  function close() {
    if (isOpen === true) {
      isOpen = false;
      unregisterListeners();
      if (popupRef) {
        popupRef.style.display = 'none';
      }
    }
  }

  function onOpen(ev: Event | MouseEvent) {
    const ref = popupRef;
    if (ref == null) {
      return;
    }
    const target: EventTarget | Element | null = ev.target;
    let x: number = 0;
    let y: number = 0;
    if ('clientX' in ev) {
      x = ev.clientX;
      y = ev.clientY;
    }
    if (x === 0 && y === 0 && target != null && 'getBoundingClientRect' in target) {
      const rect = (target as Element).getBoundingClientRect();
      x = rect.left + rect.width / 2;
      y = rect.top + rect.height / 2;
    }

    isOpen = true;
    registerListeners();
    registry.forEach((c) => {
      if (c !== close) {
        c();
      }
    });

    ref.style.display = 'inline-block';
    const { height, width } = ref.getBoundingClientRect();
    const { innerWidth, innerHeight } = window;
    if (x + width > innerWidth) {
      x = innerWidth - width > 0 ? innerWidth - width : 0;
    }
    if (y + height > innerHeight) {
      y = innerHeight - height > 0 ? innerHeight - height : 0;
    }
    ref.style.top = `${y}px`;
    ref.style.left = `${x}px`;
    ref.focus();
  }

  function onAction(ev: Event | MouseEvent) {
    if (isOpen) {
      close();
      return;
    }
    window.setTimeout(() => {
      onOpen(ev);
    }, 0);
  }

  function onKeyDown(ev: KeyboardEvent) {
    if (ev.key === 'Escape') {
      close();
    }
  }

  onMount(() => {
    registry.push(close);
    return () => {
      unregisterListeners();
      const idx = registry.findIndex((c) => c === close);
      if (idx !== -1) {
        registry.splice(idx, 1);
        close();
      }
    };
  });

  setContext('actionAcceptor', onAction);
</script>

<template>
  <div tabindex="0" class="popup" bind:this={popupRef}>
    <slot name="popup" />
  </div>
  <slot />
</template>
