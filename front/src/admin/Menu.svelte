<script lang="ts">
  import { _ } from 'svelte-intl';
  import Button from '../kit/Button.svelte';
  import exitIcon from '../kit/icons/signOut';
  import usersIcon from '../kit/icons/userFriends';
  import logoIcon from '../kit/icons/greenroomsLogo';
  import { routeResetPath } from '../router';
  import type { RouterUrl } from '../router';
  import { route, routeUrlStore } from '../router';
  import { t } from '../translate';
  import { topSlots } from './menu';
  import Icon from '../kit/Icon.svelte';

  let side = '';
  let caption = '';

  function onRouteUrl(url: RouterUrl) {
    let segment = url.segments[0];
    switch (segment) {
      case 'users':
        side = segment;
        caption = t(`Menu.caption_${side}`);
        break;
      default:
        side = '';
        caption = t('Menu.caption_dashboard');
    }
  }

  function onExit() {
    routeResetPath('/index.html');
  }

  function onRoot() {
    route('');
  }

  function onBack() {
    if (side === '') {
      onExit();
    } else {
      onRoot();
    }
  }

  $: onRouteUrl($routeUrlStore);
</script>

<template>
  <div class="menu">
    <ul>
      <li class="logo">
        <Icon icon={logoIcon} />
      </li>
      <li class="left text">{caption || ''}</li>
      <li class="right" />
      {#each $topSlots as s (s.id)}
        <li class="slot">
          <Button icon={s.icon} on:click={s.onAction} title={s.tooltip()} {...s.props} />
        </li>
      {/each}
      {#if side === 'users' || side === ''}
        <li>
          <Button
            componentClass="x"
            toggled={side === 'users'}
            autotoggle={false}
            icon={usersIcon}
            toggleIcon={usersIcon}
            toggleIconClass="toggled-onmedia"
            title={$_('Menu.caption_users')}
            on:click={() => route('users') || onRoot()}
          />
        </li>
      {/if}
      <li>
        <Button componentClass="x" on:click={onBack} icon={exitIcon} title={$_('Menu.caption_goback')} />
      </li>
    </ul>
  </div>
</template>
