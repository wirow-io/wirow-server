<script lang="ts">
  import { createEventDispatcher, getContext } from 'svelte';

  export let value: any = null;
  export let title: string = '';

  const dispatch = createEventDispatcher();
  const acceptorFn = getContext('actionAcceptor');

  function onKeyDown(ev: KeyboardEvent) {
    if (ev.key === 'Enter' || ev.key === 'Escape') {
      if (ev.key === 'Enter') {
        dispatch('action', value ?? title);
      }
      if (typeof acceptorFn === 'function') {
        acceptorFn(ev);
      }
    }
  }

  function onClick(ev: any) {
    dispatch('action', value ?? title);
    if (typeof acceptorFn === 'function') {
      acceptorFn(ev);
    }
  }
</script>

<template>
  <li tabindex="0" on:keydown|stopPropagation={onKeyDown} on:click={onClick}>{title}</li>
</template>
