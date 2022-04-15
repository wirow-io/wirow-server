<script lang="ts">
  import { onMount } from 'svelte';
  import { dbPromiseOrNull } from '../db';
  import { isTouchEvent } from '../utils';

  export let size: number = 0;
  export let collapseWidth: number = 0;
  export let collapsed: boolean | undefined;
  export let persistKey: string | undefined;
  export let resizable: boolean = true;
  export let state: 'collapsed' | 'open';

  const dividerSize = 4;

  let paneElement: HTMLElement | undefined;
  let firstElement: HTMLElement | undefined;

  let firstWidthInitial = 0;
  let mouseStartX = 0;
  let mouseCurrentX = 0;

  let allowPersist = false;
  let sizeBeforeMove = 0;

  $: sync(collapsed);
  $: persistCollapsed(collapsed);
  $: state = collapsed ? 'collapsed' : 'open';

  function updateFirst(width: number) {
    if (firstElement) {
      if (collapsed || width == 0) {
        firstElement.classList.add('collapsed');
      } else {
        firstElement.classList.remove('collapsed');
        firstElement.style.width = `${width}px`;
      }
    }
  }

  function onWindwowResize() {
    if (firstElement) {
      const box = firstElement.getBoundingClientRect();
      if (box.width + box.x >= window.innerWidth - dividerSize) {
        size = window.innerWidth - box.x - dividerSize;
        if (size < 0) {
          size = 0;
        }
        updateFirst(size);
      }
    }
  }

  onMount(() => {
    sync();
    window.addEventListener('resize', onWindwowResize);
    return () => {
      window.removeEventListener('resize', onWindwowResize);
    };
  });

  if (persistKey) {
    (async () => {
      const db = await dbPromiseOrNull;
      const cv = await db?.get<boolean | undefined>(`${persistKey}.collapsed`);
      const sv = await db?.get<number | undefined>(`${persistKey}.size`);
      if (sv != null) {
        size = sv;
      }
      if (cv != null && collapsed == null) {
        collapsed = cv;
      }
      sync();
      allowPersist = true;
    })();
  }

  function sync(..._: any[]) {
    if (paneElement && firstElement) {
      if (collapsed || collapsed == null) {
        onStopResize(null, 0);
      } else if (firstElement) {
        updateFirst(size || collapseWidth * 2);
        window.setTimeout(onWindwowResize, 0);
      }
    }
  }

  function onStartResize(ev: MouseEvent | TouchEvent) {
    ev.preventDefault();
    ev.stopPropagation();

    if (!resizable) {
      return;
    }

    if (isTouchEvent(ev)) {
      const tch = ev.touches.item(0);
      if (tch) {
        mouseStartX = tch.clientX;
      } else {
        return; // Abort
      }
    } else {
      mouseStartX = ev.clientX;
    }

    firstElement!.classList.remove('width-smooth');
    mouseCurrentX = mouseStartX;
    sizeBeforeMove = size;

    firstWidthInitial = parseInt(firstElement!.style.width);
    if (isNaN(firstWidthInitial)) {
      firstWidthInitial = size || collapseWidth * 2 || 0;
    }

    if (isTouchEvent(ev)) {
      window.addEventListener('touchmove', onMouseMove, {
        passive: false,
      });
      window.addEventListener('touchend', onStopResize, {
        passive: false,
      });
      window.addEventListener('touchcancel', onStopResize, {
        passive: false,
      });
    } else {
      window.addEventListener('mousemove', onMouseMove);
      window.addEventListener('mouseup', onStopResize);
    }

    paneElement!.style.cursor = 'ew-resize';
  }

  function onMouseMove(ev: MouseEvent | TouchEvent) {
    ev.preventDefault();
    ev.stopPropagation();

    if (isTouchEvent(ev)) {
      const tch = ev.touches.item(0);
      if (tch) {
        mouseCurrentX = tch.clientX;
      } else {
        return;
      }
    } else {
      mouseCurrentX = ev.clientX;
    }

    if (mouseCurrentX >= window.innerWidth) {
      mouseCurrentX = window.innerWidth - dividerSize;
      if (mouseCurrentX < 0) {
        mouseCurrentX = 0;
      }
    }

    if (firstWidthInitial + mouseCurrentX - mouseStartX < collapseWidth) {
      if (mouseCurrentX < mouseStartX) {
        collapsed = true;
        size = sizeBeforeMove;
        return;
      }
    }

    size = firstWidthInitial + mouseCurrentX - mouseStartX;
    collapsed = false;
    updateFirst(size);
  }

  function onStopResize(ev: UIEvent | null, newWidth?: number) {
    if (ev) {
      ev.preventDefault();
      ev.stopPropagation();
    }

    window.removeEventListener('mousemove', onMouseMove);
    window.removeEventListener('touchmove', onMouseMove);
    window.removeEventListener('mouseup', onStopResize);
    window.removeEventListener('touchend', onStopResize);
    window.removeEventListener('touchcancel', onStopResize);

    if (firstElement) {
      firstElement.classList.add('width-smooth');
    }
    if (paneElement) {
      paneElement.style.cursor = 'auto';
    }

    const width = newWidth != null ? newWidth : firstWidthInitial + mouseCurrentX - mouseStartX;
    if (width === 0) {
      updateFirst(0);
    } else {
      size = width;
      updateFirst(size);
      persistSize(size);
    }

    mouseStartX = 0;
    mouseCurrentX = 0;
  }

  async function persistSize(v: number): Promise<void> {
    if (allowPersist) {
      return (await dbPromiseOrNull)?.put(`${persistKey}.size`, v);
    }
  }

  async function persistCollapsed(..._: any[]): Promise<void> {
    if (allowPersist) {
      return (await dbPromiseOrNull)?.put(`${persistKey}.collapsed`, collapsed);
    }
  }
</script>

<template>
  <div class="h-split-pane" bind:this={paneElement}>
    <div class="hsplit-first" class:collapsed bind:this={firstElement}>
      <slot name="first" />
    </div>
    <div class="divider" on:mousedown={onStartResize} on:touchstart={onStartResize} class:noresize={!resizable} />
    <div class="hsplit-second">
      <slot name="second" class="second" />
    </div>
  </div>
</template>
