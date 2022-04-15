<script lang="ts">
  import { onMount } from 'svelte';
  import { _ } from 'svelte-intl';
  import type { RoomType } from '../interfaces';
  import { openModal, recommendedModalOptions } from '../kit/Modal.svelte';
  import { roomBrowserUUID } from '../router';
  import { t } from '../translate';
  import { user } from '../user';
  import WelcomeHistory from './WelcomeHistory.svelte';
  import MeetingPrepareDialog from './meeting/MeetingPrepareDialog.svelte';
  import WelcomeActions from './WelcomeActions.svelte';
  import WelcomeHeader from './WelcomeHeader.svelte';

  function createPrepareMeetingModal(type: RoomType = 'meeting') {
    openModal(
      MeetingPrepareDialog,
      {
        ...recommendedModalOptions,
      },
      {
        type,
      }
    );
  }

  onMount(() => {
    window.document.title = t('title.greenrooms');
  });

  function checkBrowserUUID(..._: any[]) {
    if ($roomBrowserUUID != null) {
      createPrepareMeetingModal();
    }
  }

  $: checkBrowserUUID($roomBrowserUUID);
</script>

<template>
  <div class="welcome">
    {#if $roomBrowserUUID == null && $user != null}
      <WelcomeHeader />
      <h2>{$_('Welcome.welcome')}</h2>
      <div class="welcome-holder">
        <WelcomeActions />
        <WelcomeHistory />
      </div>
    {/if}
  </div>
</template>
