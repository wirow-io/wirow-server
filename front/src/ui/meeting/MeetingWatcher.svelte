<script lang="ts">
  import type { MeetingRoom } from './meeting';

  export let room: MeetingRoom;

  const roomName = room.roomName;

  function onRoomName(name: string) {
    window.document.title = name;
  }

  $: onRoomName($roomName);
</script>
