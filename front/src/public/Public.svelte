<script lang="ts">
  import { onMount } from 'svelte';
  import { _ } from 'svelte-intl';
  import { slide } from 'svelte/transition';
  import Button from '../kit/Button.svelte';
  import Modal from '../kit/Modal.svelte';
  import Notifications, { sendNotification } from '../kit/Notifications.svelte';
  import { roomBrowserUUID } from '../router';
  import AppCaption from './AppCaption.svelte';
  import LoginDialog from './LoginDialog.svelte';

  interface RoomStatusResponse {
    name?: string;
    active: boolean;
  }

  let showLogin = false;
  let roomUuid: string | undefined = undefined;
  let status: RoomStatusResponse | undefined = undefined;
  let timeoutHandle: any = undefined;

  async function _acquireRoomState(uuid: string | undefined): Promise<RoomStatusResponse | undefined> {
    if (uuid === undefined) {
      return undefined;
    }
    const resp = await fetch(`status?ref=${encodeURIComponent(uuid)}`);
    if (resp.status !== 200) {
      return undefined;
    }
    return await resp.json();
  }

  async function acquireRoomState(uuid: string | undefined) {
    roomUuid = undefined;
    return _acquireRoomState(uuid)
      .then((s) => {
        if (s == null) {
          status = undefined;
        } else {
          status = s;
          if (status.active === false) {
            clearTimeout(timeoutHandle);
            timeoutHandle = setTimeout(() => {
              acquireRoomState(uuid);
            }, 5000);
          } else {
            roomUuid = uuid;
          }
        }
      })
      .catch(() => {
        status = undefined;
      });
  }

  function onGuestJoin() {
    if (roomUuid === undefined) {
      return;
    }
    const form = document.getElementById('guestForm') as HTMLFormElement;
    const ref = document.getElementById('guestRef') as HTMLInputElement;
    ref.value = roomUuid;
    form.submit();
  }

  onMount(() => {
    const html = window.document.documentElement;
    const status = html.getAttribute('data-status');
    html.removeAttribute('data-status');
    if (status === 'failed') {
      showLogin = true;
      sendNotification({
        text: $_('Public.login_failed'),
        style: 'warning',
        closeOnClick: true,
      });
    }
    return () => {
      clearTimeout(timeoutHandle);
    };
  });

  $: acquireRoomState($roomBrowserUUID);
</script>

<template>
  <Modal />
  <Notifications />
  <div id="root">
    <div class="holder modal-medium-width">
      <AppCaption />
      {#if status}
        <div class="options">
          <form id="guestForm" method="POST">
            <input type="hidden" id="guestRef" name="__ref__" value="" />
            <h3>{$_('Public.joining_the_room')} <span class="room-name">{status.name || ''}</span></h3>
          </form>

          {#if status.active}
            <Button on:click={onGuestJoin} componentClass="xx green">
              {$_('Public.button_join_no_signin')}
            </Button>
          {:else}
            <Button disabled loading componentClass="xx">{$_('Public.awaiting_start')}</Button>
          {/if}

          {#if !showLogin}
            <Button componentClass="xx" on:click={() => (showLogin = true)}>{$_('Public.button_joining_signin')}</Button
            >
          {:else}
            <div class="login" in:slide={{ duration: 100 }}>
              <LoginDialog />
            </div>
          {/if}
        </div>
      {:else}
        <div class="login">
          <LoginDialog />
        </div>
      {/if}
    </div>
  </div>
</template>

<style lang="scss">
  @import '../../css/kit/variables';
  #root {
    height: 100vh;
    width: 100vw;
    overflow: auto;
    display: grid;
    grid-template-rows: 0fr;
  }
  .room-name {
    background-color: #344054;
    padding: 0 0.5em;
    font-size: 1.1em;
  }
  .options {
    display: flex;
    flex-direction: column;
  }
  .holder {
    align-self: center;
    justify-self: center;
  }
</style>
