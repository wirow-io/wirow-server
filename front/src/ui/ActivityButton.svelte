<script lang="ts">
  import Button from '../kit/Button.svelte';
  import Tooltip from '../kit/Tooltip.svelte';
  import { emptyReadableFalse } from '../utils';
  import type { ActivityBarSlot } from './ActivityBarLib';

  export let s: ActivityBarSlot;

  const attentionStore = s.attentionStore || emptyReadableFalse;
  const userActiveStore = s.activeStore || emptyReadableFalse;
</script>

<template>
  <Tooltip>
    <Button
      icon={s.icon}
      toggled={s.active || $userActiveStore}
      componentClass={s.active || $userActiveStore ? `active ${s.buttonClass || ''}` : s.buttonClass || ''}
      on:click={s.onAction}
      attention={$attentionStore}
      {...s.props}
    />
    <div slot="tooltip">{s.tooltip()}</div>
  </Tooltip>
</template>
