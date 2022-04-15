<script lang="ts" context="module">
  const registry: (() => void)[] = [];
</script>

<script lang="ts">
  import { createEventDispatcher, onMount } from 'svelte';
  import { get_current_component } from 'svelte/internal';
  import { fade } from 'svelte/transition';
  import type { SBindFn } from '../interfaces';
  import type { DropdownBind } from './dropdown';
  import { hasModals } from './Modal.svelte';

  export let closeOnClick: boolean = false;
  export let disabled: boolean = false;
  export let componentClass = '';
  export let componentStyle = '';
  export let bind: SBindFn<DropdownBind> | undefined = undefined;

  let isOpen: boolean = false;
  let ddRef: HTMLElement | undefined;
  let hostRef: HTMLElement | undefined;

  let maxHeight = 0;
  let ddTop = '100%';
  let ddLeft = '0';
  let outclickRoot: Node = document;

  const dispatch = createEventDispatcher();

  function registerListeners() {
    if (hasModals()) {
      outclickRoot = document.getElementById('modal-area') || document;
    }
    outclickRoot.addEventListener('click', close);
    window.addEventListener('resize', close);
  }

  function unregisterListeners() {
    outclickRoot.removeEventListener('click', close);
    window.removeEventListener('resize', close);
    outclickRoot = document;
  }

  function toggle(state: boolean = !isOpen) {
    isOpen = disabled ? false : state;
    if (isOpen === true) {
      registerListeners();
      registry.forEach((c) => {
        if (c !== close) {
          c();
        }
      });
    } else {
      unregisterListeners();
    }
    dispatch(isOpen ? 'open' : 'close');
  }

  function close() {
    if (isOpen) toggle(false);
  }

  bind?.(get_current_component(), {
    get toggled(): boolean {
      return isOpen;
    },
    set toggled(v: boolean) {
      if (v !== isOpen) {
        toggle(v);
      }
    },
  });

  onMount(() => {
    registry.push(close);
    syncPosition();
    return () => {
      unregisterListeners();
      const idx = registry.findIndex((c) => c === close);
      if (idx !== -1) {
        registry.splice(idx, 1);
        close();
      }
    };
  });

  function syncPosition() {
    if (ddRef && hostRef) {
      const { height, width } = ddRef.getBoundingClientRect();
      const { top, left, height: hostHeight } = hostRef.getBoundingClientRect();

      maxHeight = height > window.innerHeight ? window.innerHeight : height;
      if (top + hostHeight + maxHeight > window.innerHeight) {
        ddTop = `${window.innerHeight - maxHeight - top}px`;
      } else if (top + hostHeight + height <= window.innerHeight) {
        ddTop = '100%';
      }
      if (left + width > innerWidth) {
        ddLeft = `${innerWidth - left - width}px`;
      } else if (left + width <= innerWidth) {
        ddLeft = '0';
      }
    }
  }

  function onClick(e: UIEvent) {
    if (!isOpen) {
      e.stopPropagation();
      toggle();
    }
  }
</script>

<template>
  <div on:click|capture={onClick} class="dd-host {componentClass}" style={componentStyle} bind:this={hostRef}>
    <slot />
    <div class="dd-cont">
      {#if isOpen}
        <div class="dd" in:fade|local={{ duration: 100 }} style="top:{ddTop};left:{ddLeft}">
          <div bind:this={ddRef} style={maxHeight ? `max-height:${maxHeight}px` : ''}>
            <slot name="dropdown" />
          </div>
        </div>
      {/if}
    </div>
  </div>
</template>
