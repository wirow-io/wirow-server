<script lang="ts">
  import { _ } from 'svelte-intl';
  import { UUID_REGEX } from '../../constants';
  import Box from '../../kit/Box.svelte';
  import Button from '../../kit/Button.svelte';
  import Input from '../../kit/Input.svelte';
  import { closeModal } from '../../kit/Modal.svelte';
  import { route } from '../../router';
  import { createValidationContext } from '../../validate';

  let value = '';
  let title = '';
  let disabled = false;

  const { validate, valid } = createValidationContext();

  function roomIdentifierValidator(errors: string[], value?: string): any {
    if (value == null || !UUID_REGEX.test(value)) {
      const msg = $_('MeetingJoinDialog.invalid_room_identifier');
      errors.push(msg);
      return msg;
    }
  }

  function onClose() {
    closeModal();
  }

  function onAction() {
    if (validate().length || disabled) {
      return;
    }
    closeModal();
    route(value, true);
  }

  function updateDisabled(..._: any[]) {
    if (!$valid) {
      disabled = true;
      return;
    }
    disabled = title.length === 0;
  }

  async function identifierUpdated(..._: any[]) {
    if (value.length === 0) {
      title = '';
      return;
    }
    const resp = await fetch(`status?ref=${encodeURIComponent(value)}`);
    if (resp.status !== 200) {
      title = '';
      return;
    }
    const name = await resp.text();
    if (typeof name === 'string') {
      title = name;
    } else {
      title = '';
    }
  }

  $: updateDisabled($valid, title);
  $: identifierUpdated(value);
</script>

<template>
  <Box closeable on:close={onClose} componentClass="modal-medium-width">
    <h3>{$_('MeetingJoinDialog.caption')}</h3>
    <h3>
      {#if title.length > 0}<span class="room-name">{title}</span>{/if}
    </h3>
    <Input
      autofocus
      bind:value
      on:action={onAction}
      title={$_('MeetingJoinDialog.room_identifier_title')}
      maxlength={36}
      validator={roomIdentifierValidator} />
    <div class="buttons">
      <Button componentClass="focus-visible x secondary" on:click={onClose}>{$_('button.cancel')}</Button>
      <Button componentClass="focus-visible x" {disabled} on:click={onAction}>{$_('MeetingJoinDialog.join')}</Button>
    </div>
  </Box>
</template>

<style lang="scss">
  .room-name {
    background-color: #344054;
    padding: 0 0.5em;
    font-size: 1.1em;
  }
</style>
