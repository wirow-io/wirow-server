/*
 * Copyright (C) 2022 Greenrooms, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

import formatMessage from 'format-message';
import { getBrowserLocale, locale, translations, translate } from 'svelte-intl';

/**
 * Translate a given key to current locale.
 *
 * @param key Translation key.
 * @param args Optional replacement arguments.
 */
export function t(msg: formatMessage.Message, args?: Record<string, any>, locales?: formatMessage.Locales): string {
  return formatMessage.apply(formatMessage, [msg, args, locales]);
}

translations.update({
  en: {
    'title.greenrooms': 'Wirow',

    'error.unspecified': 'Unspecified',
    'error.room_not_found': 'Room not found',
    'error.invalid_input': 'Invalid input',
    'error.not_a_room_member': 'No a room member',
    'error.insufficient_permissions': 'Insufficient permissions',
    'error.cannot_join_inactive_room': "User can't join inactive room",
    'error.screen_sharing_not_allowed_by_user': `Screen sharing is disallowed by user.
    Reload window to enable it again.`,

    'license.corrupted.or.clashed': 'It seems License data has been corrupted or another app instance launched',
    'license.inactive': 'Application License expired ot inactive',
    'license.support.required': 'License verification failed, please contact support',
    'license.verification.failed': 'License verification failed, please contact support',
    'license.unknown': 'License key is not known, please contact support',

    'time.short': '{time, time, short}',
    'time.medium': '{time, time, medium}',
    'time.long': '{time, time, long}',
    'time.full': '{time, time, full}',

    'date.short': '{date, date, short}',
    'date.medium': '{date, date, medium}',
    'date.long': '{date, date, long}',
    'date.full': '{date, date, full}',

    'button.yes': 'Yes',
    'button.no': 'No',
    'button.cancel': 'Cancel',
    'button.close': 'Close',

    'notification.room_url_copied': 'Copied <a href="{url}">{url}</a>',

    'tooltip.copy_room_url': 'Copy room url',

    'env.failed_access_devices': 'Failed to access audio/video devices',
    'env.failed_initialize_app': 'Failed to initialize application',

    'ws.ask_reload': 'Please reload the browser window then try again!',
    'ws.wait_please': 'Wait please...',
    'ws.lost_connection': 'Lost connection to the server',

    'meeting.member_broadcast_message': '{member}: {message}',
    'meeting.member_joined': '{member} joined',
    'meeting.member_left': '{member} left the room',
    'meeting.room_closed': 'Meeting room unexpectedly closed for some reasons',
    'meeting.member_changed_name': '{member} changed his name to {name}',
    'meeting.recording_on': 'Room recording active',
    'meeting.recording_off': 'Room recording stopped',
    'meeting.room_renamed': 'Room renamed to {name}',
    'meeting.whiteboard_opened': '{name} opened a whiteboard',
    'meeting.open': 'Open',

    'meeting.state.': '',
    'meeting.state.new': '',
    'meeting.state.closed': '',
    'meeting.state.connecting': 'connecting...',
    'meeting.state.connected': 'connected',
    'meeting.state.failed': 'failed',
    'meeting.state.disconnected': 'disconnected',

    'Compatibility.caption': 'Wirow is not compatible with your browser',
    'Compatibility.supported_caption': 'Currently only the following <strong>desktop</strong> browsers are supported:',

    'ChatInput.tooltip_send': 'Send message',
    'ChatInput.tooltip_send_file': 'Send file',
    'ChatInput.file_dropzone': 'Drop files here to send',
    'ChatInput.file_upload_error': 'Error uploading file {name}',
    'ChatInput.file_upload_success': 'File uploaded {name}',
    'ChatInput.file_upload_progress': 'Uploading {count} files: {percent}%',
    'ChatInput.file_upload_forbidden': 'You should be logged in to send files',
    'ChatInput.file_size_limit': 'File {name} is bigger than allowed {limit}Mb',

    'Welcome.welcome': 'Welcome to Wirow!',
    'Welcome.guest': 'Guest',
    'Welcome.create_meeting': 'Meeting',
    'Welcome.create_webinar': 'Webinar',
    'Welcome.join_live_room': 'Join live room',
    'Welcome.admin_panel': 'Admin',
    'Welcome.sign_in': 'Sign-in',
    'Welcome.sign_out': 'Sign-out',
    'Welcome.cannot_join_room_is_not_active': 'Cannot join to room since it is not active.',

    'History.caption': 'Recent sessions',
    'History.search_placeholder': 'Search...',
    'History.remove_caption': 'Are you sure to remove it?',
    'History.remove_content': '{name}',

    'HistoryItem.remove': 'Remove from list',
    'HistoryItem.details': 'Session details',
    'HistoryItem.copy_room_url': 'Copy room link',

    'MeetingSettings.caption': 'Settings',

    'MeetingMembers.caption': 'Members',

    'MeetingJoinDialog.caption': 'Join live room',
    'MeetingJoinDialog.room_identifier_title': 'Room identifier (UUID)',
    'MeetingJoinDialog.invalid_room_identifier': 'Invalid room identifier',
    'MeetingJoinDialog.join': 'Join',

    'MeetingLeaveDialog.title': 'Leaving the room?',
    'MeetingLeaveDialog.goodbye_title': 'Wanna say goodbye to buddies?',
    'MeetingLeaveDialog.button_back': 'Go back',
    'MeetingLeaveDialog.button_leave': 'Leave the room',

    'Public.acquiring_room_state': 'Acquiring room state...',
    'Public.joining_the_room': 'Joining to',
    'Public.button_join_no_signin': 'Join without signing-in',
    'Public.button_joining_signin': 'Sign-in',
    'Public.login_failed': 'Invalid username or password!',
    'Public.awaiting_start': 'Awaiting start of conference...',

    'LoginDialog.caption': 'Sign-in',
    'LoginDialog.please_provide_username': 'Please provide username',
    'LoginDialog.please_provide_password': 'Please provide password',
    'LoginDialog.username': 'Username',
    'LoginDialog.password': 'Password',
    'LoginDialog.proceed': 'Proceed',

    'MediaDeviceSelector.audio_input': 'Audio input',
    'MediaDeviceSelector.audio_input_idx': 'Audio input {idx}',
    'MediaDeviceSelector.audio_output': 'Audio output',
    'MediaDeviceSelector.audio_output_idx': 'Audio output {idx}',
    'MediaDeviceSelector.device': 'Device',
    'MediaDeviceSelector.unknown_device': 'Unknow device',
    'MediaDeviceSelector.video_input': 'Video input',
    'MediaDeviceSelector.video_input_idx': 'Video input {idx}',

    'MeetingPrepareDialog.acquiring_local_video': 'Acquiring local video...',
    'MeetingPrepareDialog.create_room': 'Create a room',
    'MeetingPrepareDialog.failed_to_acquire_devices': 'Failed to acquire media devices',
    'MeetingPrepareDialog.failed_to_join_the_room': 'Failed to join the room',
    'MeetingPrepareDialog.failed_to_create_a_room': 'Failed to create a room',
    'MeetingPrepareDialog.initializing': 'Initialising...',
    'MeetingPrepareDialog.join_room': 'Join the room',
    'MeetingPrepareDialog.joining_the_room': 'Joining to the room ...',
    'MeetingPrepareDialog.meeting': 'Meeting',
    'MeetingPrepareDialog.no_input_devices': 'It seems no video input devices found on your computer',
    'MeetingPrepareDialog.please_provide_a_room_title': 'Please provide a room title',
    'MeetingPrepareDialog.please_provide_display_name': 'Please provide your display name',
    'MeetingPrepareDialog.room_is_not_active{name}': 'Room {name} is not active and can be used again',
    'MeetingPrepareDialog.room_missing_name': 'Please specify your name before joining the room',
    'MeetingPrepareDialog.room_title': 'Room title',
    'MeetingPrepareDialog.webinar': 'Webinar',
    'MeetingPrepareDialog.your_display_name': 'Your name',
    'MeetingPrepareDialog.room_display_name': 'Room name',
    'MeetingPrepareDialog.open_settings': 'Open settings pane',
    'MeetingPrepareDialog.close_settings': 'Close settings pane',

    'Meeting.tooltip_open_chat': 'Open chat window',
    'Meeting.tooltip_close_chat': 'Close chat window',
    'Meeting.tooltip_whiteboard': 'Open whiteboard',
    'Meeting.tooltip_members': 'Room members',
    'Meeting.tooltip_recording_start': 'Start room recording',
    'Meeting.tooltip_recording_stop': 'Stop room recording',
    'Meeting.tooltip_setting': 'Settings',
    'Meeting.tooltip_leave': 'Leave the room',
    'Meeting.input_device_connected': 'Media input device connected',
    'Meeting.input_device_disconnected': 'Media input device disconnected',

    'ActivityBar.tooltip_videoMute': 'Mute video',
    'ActivityBar.tooltip_videoUnmute': 'Unmute video',
    'ActivityBar.tooltip_audioMute': 'Mute audio',
    'ActivityBar.tooltip_audioUnmute': 'Unmute audio',
    'ActivityBar.tooltip_shareScreen': 'Share screen',
    'ActivityBar.tooltip_shareScreenStop': 'Stop screen sharing',

    'MeetingGridUnit.tooltip_enter_fullscreen': 'Enter fullscreen',
    'MeetingGridUnit.tooltip_exit_fullscreen': 'Exit fullscreen',

    'RoomInfoDialog.activity': 'Activity',
    'RoomInfoDialog.chat': 'Chat',
    'RoomInfoDialog.recording': 'Recording',
    'RoomInfoDialog.fail': 'Failed to get room data',

    'RoomActivity.time': 'Time',
    'RoomActivity.details': 'Details',
    'RoomActivity.created': 'Room created',
    'RoomActivity.closed': 'Room closed',
    'RoomActivity.joined': '{member} joined',
    'RoomActivity.left': '{member} left',
    'RoomActivity.recstart': 'Recording started',
    'RoomActivity.recstop': 'Recording stopped',
    'RoomActivity.renamed': 'Room renamed from {old} to {new}',
    'RoomActivity.whiteboard': '{member} opened whiteboard',

    'RoomActivityRecording.pending': 'Recording generation in-progress...',
    'RoomActivityRecording.failed': 'Failed to generate room recording',

    // System events
    'event.RoomRecordingPPDone': '{name}: Video recording ready',

  },
});

locale.set(getBrowserLocale('en'));

// Indirectly initialize formatMessage object
translate.subscribe(() => {});
