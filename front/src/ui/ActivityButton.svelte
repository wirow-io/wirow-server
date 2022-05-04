<script lang="ts">
  import Button from '../kit/Button.svelte';
  import { emptyReadableFalse } from '../utils';
  import type { ActivityBarSlot } from './ActivityBarLib';

  export let s: ActivityBarSlot;

  const attentionStore = s.attentionStore || emptyReadableFalse;
  const userActiveStore = s.activeStore || emptyReadableFalse;
</script>

<template>
  <Button
    icon={s.icon}
    toggled={s.active || $userActiveStore}
    componentClass={s.active || $userActiveStore ? `active ${s.buttonClass || ''}` : s.buttonClass || ''}
    on:click={s.onAction}
    attention={$attentionStore}
    title={s.tooltip()}
    {...s.props}
  />
</template>
