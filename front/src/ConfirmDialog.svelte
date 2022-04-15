<script lang="ts">
  import { _ } from 'svelte-intl';
  import Box from './kit/Box.svelte';
  import Button from './kit/Button.svelte';
  import { closeModal } from './kit/Modal.svelte';

  export let caption = '';
  export let content = '';
  export let okLabel = 'button.yes';
  export let noLabel = 'button.no';
  export let onYes: (() => void) | undefined = undefined;
  export let onNo: (() => void) | undefined = undefined;

  function onPressNo() {
    if (onNo) {
      onNo();
    } else {
      closeModal();
    }
  }

  function onPressYes() {
    if (onYes) {
      onYes();
    } else {
      closeModal();
    }
  }
</script>

<template>
  <Box closeable on:close={onPressNo} componentClass="modal-small-width">
    <h3>{caption || ''}</h3>
    {content || ''}
    <div class="buttons">
      <Button autofocus componentClass="focus-visible" on:click={onPressNo}>{$_(noLabel)}</Button>
      <Button componentClass="error focus-visible" on:click={onPressYes}>{$_(okLabel)}</Button>
    </div>
  </Box>
</template>
