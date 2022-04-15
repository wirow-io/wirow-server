<script lang="ts">
  import { _ } from 'svelte-intl';
  import Box from '../../kit/Box.svelte';
  import Button from '../../kit/Button.svelte';
  import atIcon from '../../kit/icons/at';
  import infoIcon from '../../kit/icons/info';
  import keyIcon from '../../kit/icons/key';
  import theaterMaskIcon from '../../kit/icons/theaterMasks';
  import userIcon from '../../kit/icons/user';
  import Input from '../../kit/Input.svelte';
  import { closeModal } from '../../kit/Modal.svelte';
  import { sendNotification } from '../../kit/Notifications.svelte';
  import { Logger } from '../../logger';
  import { recommendedTimeout } from '../../notify';
  import { t } from '../../translate';
  import { fetchWrapper } from '../../fetch';
  import { createValidationContext } from '../../validate';
  import { validateEmail } from '../../validators';
  import type { AdminUser } from '../admin';
  import { knownUserRoles } from '../constant';

  const log = new Logger('UserEditDialog');

  export let user: AdminUser | undefined = undefined;
  export let onSuccess: (() => void) | undefined = undefined;

  const titleKey = user ? 'UserEditDialog.user_edit' : 'UserEditDialog.user_create';
  const saveKey = user ? 'UserEditDialog.button_save_edit' : 'UserEditDialog.button_save_create';

  let name = user?.name ?? '';
  let notes = user?.notes ?? '';
  let email = user?.email ?? '';
  let perms = user?.perms ?? '';
  let password = '';

  const { validate, valid } = createValidationContext(...(user ? [] : [userNameValidator, passwordValidator]));

  function userNameValidator(errors: string[], value?: string): any {
    value = value ? value.trim() : '';
    if (value.length < 2) {
      const msg = t('UserEditDialog.error_username_required');
      errors.push(msg);
      return msg;
    }
  }

  function passwordValidator(errors: string[], value?: string): any {
    if (user == null && (value == null || value.trim() === '')) {
      const msg = t('UserEditDialog.error_password_required');
      errors.push(msg);
      return msg;
    }
  }

  function emailValidator(errors: string[], value?: string): any {
    if (value && !validateEmail(value)) {
      const msg = t('UserEditDialog.error_email_invalid');
      errors.push(msg);
      return msg;
    }
  }

  function permissionsValidator(errors: string[], value?: string): any {
    if (value == null || value.trim() == '') {
      return;
    }
    value.split(',').some((r) => {
      if (knownUserRoles.indexOf(r.trim()) === -1) {
        const msg = t('UserEditDialog.error_role_unknown', { name: r });
        errors.push(msg);
        return true;
      }
      return false;
    });
    return errors[0];
  }

  function normalizePerms() {
    const pm = {};
    perms = perms.trim();
    if (user == null && perms.length === 0) {
      perms = 'user';
    }
    perms.split(',').forEach((p) => {
      p = p.trim();
      if (p.length) {
        pm[p] = true;
      }
    });
    if (pm['admin']) {
      pm['user'] = true;
    }
    perms = Object.keys(pm).sort().join(',');
  }

  async function onSave() {
    if (!$valid || validate().length) {
      return;
    }
    normalizePerms();
    const body = new FormData();
    if (user) {
      body.append('uuid', user.uuid);
    }
    body.append('name', name);
    body.append('perms', perms);
    body.append('email', email);
    body.append('password', password);
    body.append('notes', notes);

    const res = await fetchWrapper(
      fetch('admin/user/update', {
        body,
        method: 'POST',
      }),
      {
        errorKey: 'UserEditDialog.error_create',
        log,
      }
    );
    if (res) {
      sendNotification({
        text: user ? t('UserEditDialog.updated') : t('UserEditDialog.created'),
        closeable: true,
        timeout: recommendedTimeout,
      });
      onSuccess && onSuccess();
    }
  }
</script>

<template>
  <Box closeable on:close={() => closeModal()} componentClass="modal-medium-width">
    <ul class="form">
      <li>
        <h2>{$_(titleKey)}</h2>
      </li>
      <li>
        <Input
          autofocus
          autocomplete="off"
          bind:value={name}
          readonly={user != null}
          icon={userIcon}
          on:action={onSave}
          required={user == null}
          title={$_('UserEditDialog.input_name')}
          validator={userNameValidator}
          noValidateOnInput={true} />
      </li>
      <li>
        <Input
          autocomplete="new-password"
          bind:value={password}
          icon={keyIcon}
          type="password"
          on:action={onSave}
          required={user == null}
          title={$_('UserEditDialog.input_password')}
          validator={passwordValidator} />
      </li>
      <li>
        <Input
          autocomplete="off"
          bind:value={perms}
          icon={theaterMaskIcon}
          title={$_('UserEditDialog.input_perms')}
          placeholder={$_('UserEditDialog.input_perms_placeholder')}
          validator={permissionsValidator}
          noValidateOnInput={true} />
      </li>
      <li>
        <Input
          autocomplete="off"
          bind:value={email}
          icon={atIcon}
          title={$_('UserEditDialog.input_email')}
          validator={emailValidator}
          noValidateOnInput={true} />
      </li>
      <li>
        <Input autocomplete="off" bind:value={notes} icon={infoIcon} title={$_('UserEditDialog.input_notes')} />
      </li>
      <li class="buttons">
        <Button componentClass="x full-width" disabled={!$valid} on:click={onSave}>{$_(saveKey)}</Button>
      </li>
    </ul>
  </Box>
</template>

<style lang="scss">
</style>
