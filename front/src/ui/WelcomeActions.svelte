<script lang="ts">
  import { _ } from 'svelte-intl';
  import type { RoomType } from '../interfaces';
  import Button from '../kit/Button.svelte';
  import toolsIcon from '../kit/icons/tools';
  import userFriendsIcon from '../kit/icons/userFriends';
  import webinarIcon from '../kit/icons/webinar';
  import { openModal, recommendedModalOptions } from '../kit/Modal.svelte';
  import { canCreateMeeting, canCreateWebinar, canUserAdminPanel } from '../permissions';
  import { routeResetPath } from '../router';
  import MeetingPrepareDialog from './meeting/MeetingPrepareDialog.svelte';

  function onAdmninPanel() {
    routeResetPath('/admin.html');
  }

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

  function onCreateMeeting() {
    if (window.location.hash != '') {
      window.location.hash = '';
    }
    createPrepareMeetingModal('meeting');
  }

  function onCreateWebinar() {
    if (window.location.hash != '') {
      window.location.hash = '';
    }
    createPrepareMeetingModal('webinar');
  }
</script>

<template>
  <div class="welcome-actions">
    {#if $canCreateMeeting}
      <Button on:click={onCreateMeeting} icon={userFriendsIcon} componentClass="xx secondary">
        {$_('Welcome.create_meeting')}
      </Button>
    {/if}
    {#if $canCreateWebinar}
      <Button on:click={onCreateWebinar} icon={webinarIcon} componentClass="xx secondary">
        {$_('Welcome.create_webinar')}
      </Button>
    {/if}
    {#if $canUserAdminPanel}
      <Button on:click={onAdmninPanel} icon={toolsIcon} componentClass="xx secondary">
        {$_('Welcome.admin_panel')}
      </Button>
    {/if}
  </div>
</template>
