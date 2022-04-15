<script lang="ts">
  import { onMount } from 'svelte';

  export let delay = 600;
  export let componentClass = '';
  export let componentStyle = '';

  const dinkSize = 8;
  let isRendered = false;

  let tip: HTMLElement | undefined = undefined;
  let tipClass = '';
  let tipStyle = '';
  let tipTimeoutId: number | any;

  function doTooltip(rect: DOMRect) {
    tip!.style.display = 'inline-block';

    const { offsetWidth, offsetHeight } = tip!;
    const { top, right, left, bottom, width, height } = rect;
    const { innerWidth, innerHeight } = window;
    if (dinkSize + offsetWidth < innerWidth - left - width) {
      tipClass = 'right';
      tipStyle = `top:${Math.ceil(top + height / 2 - offsetHeight / 2)}px;left:${right + dinkSize}px`;
    } else if (
      dinkSize + offsetHeight < top &&
      (offsetWidth - width) / 2 < innerWidth - right &&
      left + width / 2 - offsetWidth >= 0
    ) {
      tipClass = 'top';
      tipStyle = `top:${Math.floor(top - offsetHeight - dinkSize)}px;left:${left + width / 2 - offsetWidth / 2}px`;
    } else if (dinkSize + offsetHeight < innerHeight - top - height && (offsetWidth - width) / 2 < innerWidth - right) {
      tipClass = 'bottom';
      tipStyle = `top:${bottom + dinkSize}px;left:${left + width / 2 - offsetWidth / 2}px`;
    } else {
      tipClass = 'left';
      tipStyle = `top:${top + height / 2 - offsetHeight / 2}px;left:${Math.floor(left - dinkSize - offsetWidth)}px`;
    }
    isRendered = true;
  }

  function onEnter(ev: MouseEvent) {
    const rect = (ev.target as Element).getBoundingClientRect();
    if (tipTimeoutId) {
      clearTimeout(tipTimeoutId);
    }
    tipTimeoutId = setTimeout(() => {
      doTooltip(rect);
    }, delay);
  }

  function onLeave() {
    clearTimeout(tipTimeoutId);
    tipTimeoutId = undefined;
    tipStyle = '';
    isRendered = false;
  }

  onMount(() => {
    const el = tip!;
    document.body.appendChild(el);
    return () => {
      document.body.removeChild(el);
      clearTimeout(tipTimeoutId);
    };
  });
</script>

<template>
  <div class="tooltip" on:pointerenter={onEnter} on:pointerleave={onLeave}>
    <slot />
  </div>
  <div
    class="tip {tipClass} {componentClass}"
    style="{componentStyle} {tipStyle}"
    class:isRendered
    class:isVisible={tipStyle}
    bind:this={tip}
  >
    <slot name="tooltip" />
  </div>
</template>
