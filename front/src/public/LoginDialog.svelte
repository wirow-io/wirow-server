<script lang="ts">
  import { _ } from 'svelte-intl';
  import Box from '../kit/Box.svelte';
  import Button from '../kit/Button.svelte';
  import keyIcon from '../kit/icons/key';
  import userIcon from '../kit/icons/user';
  import Input from '../kit/Input.svelte';
  import { createValidationContext } from '../validate';

  let username = '';
  let password = '';

  const { validate, valid } = createValidationContext();

  function userNameValidator(errors: string[], value?: string): any {
    if (value == null || value.trim() === '') {
      const msg = $_('LoginDialog.please_provide_username');
      errors.push(msg);
      return msg;
    }
  }

  function passwordValidator(errors: string[], value?: string): any {
    if (value == null || value.trim() === '') {
      const msg = $_('LoginDialog.please_provide_password');
      errors.push(msg);
      return msg;
    }
  }

  async function onLogin() {
    if (validate().length) {
      return;
    }
    const form = document.getElementById('authForm') as HTMLFormElement;
    form.submit();
  }
</script>

<template>
  <Box componentClass="login-dialog modal-medium-width">
    <form id="authForm" method="POST">
      <ul class="form">
        <li class="caption"><span class="text">{$_('LoginDialog.caption')}</span></li>
        <li>
          <Input
            autofocus
            name="__username__"
            bind:value={username}
            icon={userIcon}
            on:action={onLogin}
            title={$_('LoginDialog.username')}
            validator={userNameValidator} />
        </li>
        <li>
          <Input
            type="password"
            name="__password__"
            autocomplete="off"
            bind:value={password}
            icon={keyIcon}
            on:action={onLogin}
            title={$_('LoginDialog.password')}
            validator={passwordValidator} />
        </li>
        <li class="buttons">
          <Button on:click={onLogin} disabled={username === '' || password === '' || !$valid} componentClass="x">
            {$_('LoginDialog.proceed')}
          </Button>
        </li>
      </ul>
    </form>
  </Box>
</template>

<style lang="scss">
  @import '../../css/kit/variables';
  :global {
    #root {
      background-image: url(images/bg4.svg);
      background-size: cover;
    }
    .login-dialog {
      margin-top: 0.6em;
      h1 {
        font-size: 1.8em;
        margin: 0.5em 0;
      }
      background-color: $color_modal_background;
    }
  }
  .caption {
    font-family: 'button', 'main', sans-serif;
    font-size: 1.75rem;
  }
</style>
