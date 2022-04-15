<script context="module" lang="ts">
  import { getContext, onMount, setContext } from 'svelte';
  import type { RouterPosition, RouterSpec, RouterUrl } from './';
  import { forceRoutersHooks, routeUrlStore } from './';

  const levelKey = {};

  const registry = new Map();

  forceRoutersHooks.push(() => {
    registry.forEach((v) => v());
  });
</script>

<script lang="ts">
  export let routes: Array<RouterSpec> = [];

  // Current router position
  let position: RouterPosition | null = null;

  // Current route level
  const level = (() => {
    const ret = (getContext<number>(levelKey) ?? -1) + 1;
    setContext(levelKey, ret);
    return ret;
  })();

  function onRouterUrlChanged(u: RouterUrl, forced = false) {
    const oldSegment = position?.segments[level];
    const newSegment = u.segments[level];

    // console.log(`oldSegment=${oldSegment} newSegment=${newSegment}`);

    if (newSegment == null) {
      position = null;
      return;
    }
    if (!forced && oldSegment === newSegment) {
      return;
    }
    const spec = routes.find((s) => {
      const m = s[1];
      if (typeof m === 'string') {
        return m === newSegment;
      } else if (typeof m === 'function') {
        return m(newSegment);
      } else {
        return false;
      }
    });
    position = spec === undefined ? null : { ...u, level, spec };
  }

  $: onRouterUrlChanged($routeUrlStore);

  onMount(() => {
    function force() {
      onRouterUrlChanged($routeUrlStore, true);
    }
    registry.set(force, force);
    return () => {
      registry.delete(force);
    };
  });
</script>

<template>
  <svelte:component this={position && position.spec[0]} />
</template>
