<script lang="ts" context="module">
  import { onMount, setContext } from 'svelte';
  import { Config } from '../../config';
  import { UUID_REGEX } from '../../constants';
  import type { NU, SEvent } from '../../interfaces';
  import HSplitPane from '../../kit/HSplitPane.svelte';
  import whiteboardIcon from '../../kit/icons/chalkboard';
  import cogIcon from '../../kit/icons/cog';
  import commentsIcon from '../../kit/icons/comments';
  import phoneSlashIcon from '../../kit/icons/phoneSlash';
  import recordingIcon from '../../kit/icons/recording';
  import usersIcon from '../../kit/icons/users';
  import { openModal, recommendedModalOptions } from '../../kit/Modal.svelte';
  import { Logger } from '../../logger';
  import { allInputDevices, closeAllMediaTracks } from '../../media';
  import { recommendedTimeout, sendNotification } from '../../notify';
  import type { Room } from '../../room';
  import { route } from '../../router';
  import { t } from '../../translate';
  import { catchLogResolve, storeGet } from '../../utils';
  import ActivityBar from '../ActivityBar.svelte';
  import type { ActivityBarSlot } from '../ActivityBarLib';
  import Chat from '../chat/Chat.svelte';
  import SideBar from '../SideBar.svelte';
  import { Key, MeetingContext, MeetingRoom, meetingRoomStore } from './meeting';
  import { meetingChatAttach } from './meeting-chat';
  import { meetingMembersAttach } from './meeting-members';
  import { toggleRecording } from './meeting-recording';
  import MeetingFooter from './MeetingFooter.svelte';
  import MeetingGrid from './MeetingGrid.svelte';
  import MeetingLeaveDialog from './MeetingLeaveDialog.svelte';
  import MeetingSettings from './MeetingSettings.svelte';
  import MeetingWatcher from './MeetingWatcher.svelte';
  import MeetingMembers from './members/MeetingMembers.svelte';

  export function routerMatcher(segment: string): boolean {
    const ret = UUID_REGEX.test(segment);
    if (!ret) {
      return false;
    }
    const room = storeGet<MeetingRoom | undefined>(meetingRoomStore);
    if (!room || room.uuid != segment) {
      return false;
    }
    return true;
  }
</script>

<script lang="ts">
  const log = new Logger('Meeting.svelte');
  const ctx = new MeetingContext(findActivityBarSlot);
  const { activityBarActiveSlot, activityBarSlots, hasUnreadMessages, hasStartedRecording } = ctx;
  setContext(Key, ctx);

  let sideBarState: any;
  let inputDevices = $allInputDevices;

  const slots: ActivityBarSlot[] = [
    {
      id: 'chat',
      side: 'top',
      icon: commentsIcon,
      onAction: onActivityBarAction('chat'),
      sideComponent: Chat,
      sideProps: meetingChatAttach(ctx),
      attentionStore: hasUnreadMessages,
      tooltip: () => {
        return $activityBarActiveSlot?.id === 'chat' ? t('Meeting.tooltip_close_chat') : t('Meeting.tooltip_open_chat');
      },
    },
    {
      id: 'whiteboard',
      side: 'top',
      icon: whiteboardIcon,
      onAction: () => $meetingRoomStore?.room.openWhiteboard(),
      tooltip: () => t('Meeting.tooltip_whiteboard'),
      visible: () => Config.enableWhiteboard && $meetingRoomStore?.room.whiteboard !== undefined,
    },
    {
      id: 'members',
      side: 'top',
      icon: usersIcon,
      onAction: onActivityBarAction('members'),
      sideComponent: MeetingMembers,
      sideProps: meetingMembersAttach(ctx),
      tooltip: () => {
        return t('Meeting.tooltip_members');
      },
      visible: () => {
        const room = $meetingRoomStore;
        return room !== undefined && (room.owner === true || room.type === 'meeting');
      },
    },
    {
      id: 'settings',
      side: 'bottom',
      icon: cogIcon,
      sideComponent: MeetingSettings,
      onAction: onActivityBarAction('settings'),
      tooltip: () => t('Meeting.tooltip_setting'),
    },
    {
      id: 'recording',
      side: 'bottom',
      icon: recordingIcon,
      onAction: () => {
        const room = $meetingRoomStore;
        if (room !== undefined && room.owner === true) {
          return toggleRecording(ctx);
        }
      },
      attentionStore: hasStartedRecording,
      activeStore: hasStartedRecording,
      tooltip: () => {
        const room = $meetingRoomStore;
        if (room !== undefined && room.owner === true) {
          return $hasStartedRecording ? t('Meeting.tooltip_recording_stop') : t('Meeting.tooltip_recording_start');
        } else {
          return $hasStartedRecording ? t('meeting.recording_on') : t('meeting.recording_off');
        }
      },
      visible: () => {
        const room = $meetingRoomStore;
        return Config.enableRecording && !!(room && (room.owner === true || $hasStartedRecording));
      },
      props: {
        toggleIcon: recordingIcon,
        toggleIconColor: '#b1091d',
        attentionIconColor: '#b1091d',
        clickable: (function () {
          return $meetingRoomStore?.owner === true;
        })(),
      },
    },
    {
      id: 'exit',
      side: 'bottom',
      icon: phoneSlashIcon,
      props: { iconColor: '#b1091d' },
      onAction: () => onLeaveTheRoomAction(),
      tooltip: () => t('Meeting.tooltip_leave'),
    },
  ];

  activityBarSlots.set(slots);

  function findActivityBarSlot(id: string): ActivityBarSlot | undefined {
    return $activityBarSlots.find((s) => s.id === id);
  }

  function onLeaveTheRoomAction() {
    openModal(MeetingLeaveDialog, recommendedModalOptions, (close) => ({
      room: $meetingRoomStore,
      onSuccess: async (message: string) => {
        await $meetingRoomStore?.leave(message)?.catch(catchLogResolve(log, 'onLeaveTheRoomAction()'));
        close();
      },
      onCancel: () => close(),
    }));
  }

  function onActivityBarAction(id: string): (ev: SEvent) => void {
    return (ev: SEvent) => {
      const toggled = !!ev.detail;
      const slot = findActivityBarSlot(id)!;
      $activityBarSlots = $activityBarSlots.map((s) => {
        s.active = s == slot && toggled;
        return s;
      });
    };
  }

  function onActivityBarSlotChanged(..._: any[]) {
    $activityBarSlots = $activityBarSlots;
  }

  function onSideBarStateChanged(..._: any[]) {
    if (sideBarState === 'collapsed' && $activityBarActiveSlot != null) {
      $activityBarSlots = $activityBarSlots.map((s) => {
        if (s.sideComponent && !s.mainComponent) {
          s.active = false;
        }
        return s;
      });
    }
  }

  function onRoomMessage() {
    const slot = $activityBarActiveSlot;
    if (slot?.id !== 'chat') {
      hasUnreadMessages.set(true);
    }
  }

  function onRoomRecordingStatusChanged(_: Room, started: boolean) {
    hasStartedRecording.set(started);
  }

  function onRoomChanged(mr: MeetingRoom | NU) {
    if (mr == null) {
      route('');
      return;
    }
    mr.room.addListener('roomMessage', onRoomMessage);
    mr.room.addListener('recordingStatus', onRoomRecordingStatusChanged);
    hasStartedRecording.set(mr.room.recording);
  }

  function sideComponent(slot: ActivityBarSlot | NU) {
    return slot?.sideComponent;
  }

  function sideProps(slot: ActivityBarSlot | NU) {
    return slot?.sideProps;
  }

  function mainComponent(slot: ActivityBarSlot | NU) {
    return slot?.mainComponent;
  }

  function mainProps(slot: ActivityBarSlot | NU) {
    return slot?.mainProps;
  }

  function onOpenSettings(devices: MediaDeviceInfo[]) {
    const slot = findActivityBarSlot('settings');
    if (slot && inputDevices != devices) {
      sendNotification({
        text: t(
          inputDevices.length > devices.length ? 'Meeting.input_device_disconnected' : 'Meeting.input_device_connected'
        ),
        style: 'warning',
        timeout: recommendedTimeout,
      });
      $activityBarSlots = $activityBarSlots.map((s) => {
        s.active = s == slot;
        return s;
      });
      inputDevices = devices;
    }
  }

  onMount(() => {
    return () => {
      const mr = $meetingRoomStore;
      if (mr) {
        mr.room.removeListener('roomMessage', onRoomMessage);
        mr.room.removeListener('recordingStatus', onRoomRecordingStatusChanged);
        try {
          mr.leave();
        } catch (e) {
          log.error(e);
        }
      }
      closeAllMediaTracks();
    };
  });

  $: onSideBarStateChanged(sideBarState);
  $: onRoomChanged($meetingRoomStore);
  $: onActivityBarSlotChanged($hasStartedRecording);
  $: onOpenSettings($allInputDevices);
</script>

<template>
  {#if $meetingRoomStore}
    <MeetingWatcher room={$meetingRoomStore} />
    <div class="meeting">
      <div class="content-row">
        <ActivityBar slots={$activityBarSlots.filter((s) => s.visible === undefined || s.visible())} />
        <div class="content-main">
          <HSplitPane
            bind:state={sideBarState}
            collapsed={sideComponent($activityBarActiveSlot) == null}
            resizable={sideComponent($activityBarActiveSlot) != null}
            collapseWidth={150}
            persistKey="meeting.split.main"
          >
            <div slot="first" class="side-area">
              <SideBar>
                <svelte:component this={sideComponent($activityBarActiveSlot)} {...sideProps($activityBarActiveSlot)} />
              </SideBar>
            </div>
            <div slot="second" class="main-area">
              {#if mainComponent($activityBarActiveSlot)}
                <svelte:component this={mainComponent($activityBarActiveSlot)} {...mainProps($activityBarActiveSlot)} />
              {:else}
                <MeetingGrid room={$meetingRoomStore} />
              {/if}
            </div>
          </HSplitPane>
        </div>
      </div>
      <MeetingFooter />
    </div>
  {/if}
</template>
