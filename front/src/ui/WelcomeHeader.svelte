<script lang="ts">
  import Icon from '../kit/Icon.svelte';
  import logoIcon from '../kit/icons/greenroomsLogo';
  import signOutIcon from '../kit/icons/signOut';
  import signInIcon from '../kit/icons/fingerprint';
  import { isGuest } from '../permissions';
  import { routeResetPath } from '../router';
  import { user } from '../user';
  import { _ } from 'svelte-intl';

  function onSignOut() {
    routeResetPath('/login.html');
  }
</script>

<template>
  <div class="welcome-header">
    <div class="logo">
      <Icon icon={logoIcon} size="3rem" />
      <a href="https://wirow.io" target="_blank">Wirow</a>
    </div>
    <div />
    <div />
    <div class="user">
      {#if $user}
        <span>{$isGuest ? $_('Welcome.guest') : $user.name}</span>
        <Icon
          clickable
          title={$isGuest ? $_('Welcome.sign_in') : $_('Welcome.sign_out')}
          on:click={onSignOut}
          icon={$isGuest ? signInIcon : signOutIcon}
          size={32}
        />
      {/if}
    </div>
  </div>
</template>
