<script lang="ts">
  import { _ } from 'svelte-intl';
  import Box from '../../kit/Box.svelte';
  import Button from '../../kit/Button.svelte';
  import { closeModal } from '../../kit/Modal.svelte';
  import { sendNotification } from '../../kit/Notifications.svelte';
  import { Logger } from '../../logger';
  import { recommendedTimeout } from '../../notify';
  import { t } from '../../translate';
  import { fetchWrapper } from '../../fetch';
  import type { AdminUser } from '../admin';

  export let user: AdminUser;
  export let onSuccess: (() => void) | undefined = undefined;
  export let onCancel: (() => void) | undefined = undefined;

  const log = new Logger('UserRemoveDialog');

  async function onPressRemove() {
    const body = new FormData();
    body.append('uuid', user.uuid);

    const res = await fetchWrapper(
      fetch('admin/user/remove', {
        method: 'POST',
        body,
      }),
      {
        errorKey: 'UserRemoveDialog.error_remove',
        log,
      }
    );
    if (res) {
      onSuccess && onSuccess();
      sendNotification({
        text: t('UserRemoveDialog.removed'),
        closeable: true,
        timeout: recommendedTimeout,
      });
    }
  }

  function onPressCancel() {
    if (onCancel) {
      onCancel();
    } else {
      closeModal();
    }
  }
</script>

<template>
  <Box closeable on:close={() => closeModal()} componentClass="modal-small-width">
    <h3>{$_('UserRemoveDialog.caption')}</h3>
    {$_('UserRemoveDialog.caption_confirm', { name: user.name })}
    <div class="buttons">
      <Button autofocus componentClass="focus-visible" on:click={onPressCancel}>{$_('button.no')}</Button>
      <Button componentClass="error focus-visible" on:click={onPressRemove}>{$_('button.yes')}</Button>
    </div>
  </Box>
</template>
