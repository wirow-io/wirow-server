<script context="module" lang="ts">
  import type { Writable } from 'svelte/store';
  import { writable } from 'svelte/store';

  const holders: { [key: string]: Writable<any> } = {};

  function getHolder(name: string = 'default'): Writable<any> {
    let holder = holders[name];
    if (holder == null) {
      holder = writable(null);
      holders[name] = holder;
    }
    return holder;
  }
</script>

<script lang="ts">
  export let component: any = null;
  export let holder: string = 'default';

  const h = getHolder(holder);
  h.set(component);

  const props = Object.fromEntries(Object.entries($$props).filter(([k]) => k !== 'component'));
</script>

<template>
  <svelte:component this={$h} {...props} />
</template>
