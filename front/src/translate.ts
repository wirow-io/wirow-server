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
  de: {
    'title.greenrooms': 'Wirow',

    'error.unspecified': 'Unspezifiziert',
    'error.room_not_found': 'Raum nicht gefunden',
    'error.invalid_input': 'Ung??ltige Eingabe',
    'error.not_a_room_member': 'Kein Raummitglied',
    'error.insufficient_permissions': 'Unzureichende Berechtigung',
    'error.cannot_join_inactive_room': 'Anwender kann einem inaktiven Raum nicht betreten',
    'error.screen_sharing_not_allowed_by_user': `Bildschirmteilen durch Anwender verboten.
    Fenster neuladen und erneut probieren.`,

    'license.corrupted.or.clashed':
      'Es scheint, dass die Lizenzdaten besch??digt wurden oder eine andere App-Instanz gestartet wurde',
    'license.inactive': 'Anwendungslizenz abgelaufen oder inaktiv',
    'license.support.required': 'Lizenz??berpr??fung fehlgeschlagen, bitte kontaktieren Sie den Support',
    'license.verification.failed': 'Lizenz??berpr??fung fehlgeschlagen, bitte kontaktieren Sie den Support',
    'license.unknown': 'Lizenzschl??ssel ist nicht bekannt, bitte kontaktieren Sie den Support',

    'time.short': '{time, time, short}',
    'time.medium': '{time, time, medium}',
    'time.long': '{time, time, long}',
    'time.full': '{time, time, full}',

    'date.short': '{date, date, short}',
    'date.medium': '{date, date, medium}',
    'date.long': '{date, date, long}',
    'date.full': '{date, date, full}',

    'button.yes': 'Ja',
    'button.no': 'Nein',
    'button.cancel': 'Abbruch',
    'button.close': 'Schlie??en',

    'notification.room_url_copied': '<a href="{url}">{url}</a> kopiert',

    'tooltip.copy_room_url': 'Raum URL kopieren',

    'env.failed_access_devices': 'Zugriff auf Audio/Video Ger??te gescheitert',
    'env.failed_initialize_app': 'Initialisierung der Applikation gescheitert',

    'ws.ask_reload': 'Bitte Browserfenster erneut laden und erneut versuchen',
    'ws.wait_please': 'Bitte warten ...',
    'ws.lost_connection': 'Verbindung zum Server verloren',

    'meeting.member_broadcast_message': '{member}: {message}',
    'meeting.member_joined': '{member} eingetreten',
    'meeting.member_left': '{member} hat den Raum verlassen',
    'meeting.room_closed': 'Raum aus unbestimmte Gr??nde geschlossen!',
    'meeting.member_changed_name': '{member} hat sein Name zu {name} ge??ndert',
    'meeting.recording_on': 'Raumaufzeichnung aktiviert',
    'meeting.recording_off': 'Raumaufzeichnung beendet',
    'meeting.room_renamed': 'Raum name in {name} ge??ndert',
    'meeting.whiteboard_opened': '{name} hat ein whiteboard ge??ffnet',
    'meeting.open': '??ffnen',

    'meeting.state.': '',
    'meeting.state.new': '',
    'meeting.state.closed': '',
    'meeting.state.connecting': 'verbinde...',
    'meeting.state.connected': 'verbunden',
    'meeting.state.failed': 'gescheitert',
    'meeting.state.disconnected': 'getrennt',

    'Compatibility.caption': 'Wirow is nich mit ihr Browser kompatibel',
    'Compatibility.supported_caption': 'Zurzeit sind nur nachstehenden Browser <strong>desktop</strong> unterst??tzt:',

    'ChatInput.tooltip_send': 'Nachricht senden',
    'ChatInput.tooltip_send_file': 'Datei senden',
    'ChatInput.file_dropzone': 'Um eine Datei zu senden, diese hier ablegen',
    'ChatInput.file_upload_error': 'Fehler beim senden der Datei {name}',
    'ChatInput.file_upload_success': 'Datei??bertragung erfolgt {name}',
    'ChatInput.file_upload_progress': 'Hochladen {count} Dateien: {percent}%',
    'ChatInput.file_upload_forbidden': 'Um Dateien zu senden, m??ssen sie angemeldet  sein',
    'ChatInput.file_size_limit': 'Datei {name} gr????er als erlaubte Gr????e {limit}Mb',

    'Welcome.welcome': 'Willkommen zu Wirow!',
    'Welcome.guest': 'Gast',
    'Welcome.create_meeting': 'Meeting',
    'Welcome.create_webinar': 'Webinar',
    'Welcome.join_live_room': 'Raum betreten',
    'Welcome.admin_panel': 'Admin',
    'Welcome.sign_in': 'Anmelden',
    'Welcome.sign_out': 'Abmelden',
    'Welcome.cannot_join_room_is_not_active': 'Raum nicht aktiv, betreten nicht m??glich.',

    'History.caption': 'Neuere Sitzungen',
    'History.search_placeholder': 'Suchen...',
    'History.remove_caption': 'Soll es wirklich entfernt werden?',
    'History.remove_content': '{name}',

    'HistoryItem.remove': 'Aus der Liste entfernen',
    'HistoryItem.details': 'Situngsdetails',
    'HistoryItem.copy_room_url': 'Raum Link kopieren',

    'MeetingSettings.caption': 'Einstellungen',

    'MeetingMembers.caption': 'Mitglieder',

    'MeetingJoinDialog.caption': 'Live-Raum betreten ',
    'MeetingJoinDialog.room_identifier_title': 'Raumnennung  (UUID)',
    'MeetingJoinDialog.invalid_room_identifier': 'Ung??ltige Raumkennung',
    'MeetingJoinDialog.join': 'Betreten',

    'MeetingLeaveDialog.title': 'Raum verlassen?',
    'MeetingLeaveDialog.goodbye_title': 'Unterhaltung wirklich verlassen?',
    'MeetingLeaveDialog.button_back': 'Zur??ck',
    'MeetingLeaveDialog.button_leave': 'Raum verlassen',

    'Public.acquiring_room_state': 'Raumstatus ermitteln ...',
    'Public.joining_the_room': 'Einttritt in',
    'Public.button_join_no_signin': 'Betreten ohne Anmeldung',
    'Public.button_joining_signin': 'Anmelden',
    'Public.login_failed': 'Ung??ltige Anwendername oder Passwort!',
    'Public.awaiting_start': 'Warte auf Konferenzbeginn ...',

    'LoginDialog.caption': 'Anmelden',
    'LoginDialog.please_provide_username': 'Anwendername angeben',
    'LoginDialog.please_provide_password': 'Passwort angeben',
    'LoginDialog.username': 'Anwendername',
    'LoginDialog.password': 'Passwort',
    'LoginDialog.proceed': 'Anmelden',

    'MediaDeviceSelector.audio_input': 'Audio Eingang',
    'MediaDeviceSelector.audio_input_idx': 'Audio Eingang {idx}',
    'MediaDeviceSelector.audio_output': 'Audio Ausgang',
    'MediaDeviceSelector.audio_output_idx': 'Audio Ausgang {idx}',
    'MediaDeviceSelector.device': 'Ger??t',
    'MediaDeviceSelector.unknown_device': 'Unbekannte Ger??t',
    'MediaDeviceSelector.video_input': 'Video Eingang',
    'MediaDeviceSelector.video_input_idx': 'Video Eingang {idx}',

    'MeetingPrepareDialog.acquiring_local_video': 'Suche nach lokalen Videoger??t ...',
    'MeetingPrepareDialog.create_room': 'Raum anlegen',
    'MeetingPrepareDialog.failed_to_acquire_devices': 'Videoger??t Einbindung gescheitert',
    'MeetingPrepareDialog.failed_to_join_the_room': 'Betreten des Raumes gescheitert',
    'MeetingPrepareDialog.failed_to_create_a_room': 'Raum anlegen gescheitert',
    'MeetingPrepareDialog.initializing': 'Initialisierung...',
    'MeetingPrepareDialog.join_room': 'Raum betreten',
    'MeetingPrepareDialog.joining_the_room': 'Raum betreten ...',
    'MeetingPrepareDialog.meeting': 'Meeting',
    'MeetingPrepareDialog.no_input_devices': 'Anscheinend wurde kein Videoger??t auf ihr Rechner gefunden',
    'MeetingPrepareDialog.please_provide_a_room_title': 'Bitte Raumname eingeben',
    'MeetingPrepareDialog.please_provide_display_name': 'Bitte ihr Name angeben',
    'MeetingPrepareDialog.room_is_not_active{name}': 'Raum {name} ist nicht aktiv und kann erneut benutzt werden',
    'MeetingPrepareDialog.room_missing_name': 'Bitte ihr Name, vor dem Betreten des Raumes, angeben',
    'MeetingPrepareDialog.room_title': 'Raum Name',
    'MeetingPrepareDialog.webinar': 'Webinar',
    'MeetingPrepareDialog.your_display_name': 'Ihr Name',
    'MeetingPrepareDialog.room_display_name': 'Raum Name',
    'MeetingPrepareDialog.open_settings': 'Einstellungspaneel',
    'MeetingPrepareDialog.close_settings': 'Einstellungspaneel schlie??en',

    'Meeting.tooltip_open_chat': 'Chat Fenster ??ffnen',
    'Meeting.tooltip_close_chat': 'Chatfenster schlie??en',
    'Meeting.tooltip_whiteboard': 'Whiteboard ??ffnen',
    'Meeting.tooltip_members': 'Raum Mitglieder',
    'Meeting.tooltip_recording_start': 'Raum Aufnahme beginnen',
    'Meeting.tooltip_recording_stop': 'Raum Aufnahme beenden',
    'Meeting.tooltip_setting': 'Einstellungen',
    'Meeting.tooltip_leave': 'Raum verlassen',
    'Meeting.input_device_connected': 'Media Eingabeger??t verbunden',
    'Meeting.input_device_disconnected': 'Media Eingabeger??t getrennt',

    'ActivityBar.tooltip_videoMute': 'Video abschalten',
    'ActivityBar.tooltip_videoUnmute': 'Video einschalten',
    'ActivityBar.tooltip_audioMute': 'Audio stummschalten',
    'ActivityBar.tooltip_audioUnmute': 'Audio einschalten',
    'ActivityBar.tooltip_shareScreen': 'Bildschirm teilen',
    'ActivityBar.tooltip_shareScreenStop': 'Bildschirmteilen stoppen',

    'MeetingGridUnit.tooltip_enter_fullscreen': 'Vollbildschirm einstellen',
    'MeetingGridUnit.tooltip_exit_fullscreen': 'Vollbildschirm verlassen',

    'RoomInfoDialog.activity': 'Aktivit??t',
    'RoomInfoDialog.chat': 'Chat',
    'RoomInfoDialog.recording': 'Aufzeichnung',
    'RoomInfoDialog.fail': 'Fehler beim Ermitteln der Raumdaten',

    'RoomActivity.time': 'Zeit',
    'RoomActivity.details': 'Details',
    'RoomActivity.created': 'Raum angelegt',
    'RoomActivity.closed': 'Raum geschlossen',
    'RoomActivity.joined': '{member} eingetreten',
    'RoomActivity.left': '{member} verlassen',
    'RoomActivity.recstart': 'Aufzeichnung gestartet',
    'RoomActivity.recstop': 'Aufzeichnung beendet',
    'RoomActivity.renamed': 'Raum von {old} zu {new} umbenannt',
    'RoomActivity.whiteboard': '{member} hat das Whiteboard ge??ffnet',

    'RoomActivityRecording.pending': 'Aufzeichnungsvorbereitung ...',
    'RoomActivityRecording.failed': 'Erzeugung einer Raumaufzeichnung gescheitert',

    // System events
    'event.RoomRecordingPPDone': '{name}: Videoaufzeichnung bereit',
  },

  fr: {
    'title.greenrooms': 'Wirow',

    'error.unspecified': 'Non sp??cifi??',
    'error.room_not_found': 'Salon non trouv??',
    'error.invalid_input': 'Entr??e erron??e',
    'error.not_a_room_member': 'Non membre du salon',
    'error.insufficient_permissions': 'Droits insuffisants',
    'error.cannot_join_inactive_room': "L'utilisateur ne peu joindre un salon inactif",
    'error.screen_sharing_not_allowed_by_user': `Le partage d'??cran a ??t?? interdit.
    Relancer la fen??tre et r??essayez.`,

    'license.corrupted.or.clashed':
      "Il semble que les donn??es de licence aient ??t?? corrompues ou qu'une autre instance d'application ait ??t?? lanc??e",
    'license.inactive': "Licence d'application expir??e ou inactive",
    'license.support.required': "La v??rification de la licence a ??chou??, veuillez contacter l'assistance",
    'license.verification.failed': "La v??rification de la licence a ??chou??, veuillez contacter l'assistance",
    'license.unknown': "La cl?? de licence n'est pas connue, veuillez contacter le support",

    'time.short': '{time, time, short}',
    'time.medium': '{time, time, medium}',
    'time.long': '{time, time, long}',
    'time.full': '{time, time, full}',

    'date.short': '{date, date, short}',
    'date.medium': '{date, date, medium}',
    'date.long': '{date, date, long}',
    'date.full': '{date, date, full}',

    'button.yes': 'Oui',
    'button.no': 'Non',
    'button.cancel': 'Annuller',
    'button.close': 'Fermer',

    'notification.room_url_copied': '<a href="{url}">{url}</a> copi??',

    'tooltip.copy_room_url': "Copier l'URL du salon",

    'env.failed_access_devices': "??chec de l'acc??s aux appareils audio/vid??o",
    'env.failed_initialize_app': "??chec de l'initialisation de l'application",

    'ws.ask_reload': 'Relancer la fen??tre du Browser et essayer de nouveau???!',
    'ws.wait_please': 'Veuillez attendre ...',
    'ws.lost_connection': 'Perte de la connexion au serveur',

    'meeting.member_broadcast_message': '{member}: {message}',
    'meeting.member_joined': '{member} joint le salon',
    'meeting.member_left': '{member} quitte la r??union',
    'meeting.room_closed': 'Le salon a ??t?? fermer',
    'meeting.member_changed_name': '{member} a chang?? son nom en {name}',
    'meeting.recording_on': 'Enregistrement du salon en cours',
    'meeting.recording_off': 'Enregistrement du salon arr??t??',
    'meeting.room_renamed': 'Salon renomm?? {name}',
    'meeting.whiteboard_opened': '{name} a ouvert un tableau num??rique',
    'meeting.open': 'Ouvrir',

    'meeting.state.': '',
    'meeting.state.new': '',
    'meeting.state.closed': '',
    'meeting.state.connecting': 'connexion ...',
    'meeting.state.connected': 'connect??',
    'meeting.state.failed': '??chec',
    'meeting.state.disconnected': 'd??connect??',

    'Compatibility.caption': "Wirow n'est pas compatible avec votre Browser",
    'Compatibility.supported_caption': 'Actuellement seul les Browser support?? sont ceux des <strong>bureau</strong>:',

    'ChatInput.tooltip_send': 'Envoit de message',
    'ChatInput.tooltip_send_file': 'Envoi de fichier',
    'ChatInput.file_dropzone': 'D??pos?? les ou les fichiers ici',
    'ChatInput.file_upload_error': 'Erreur de transmission du fichier {name}',
    'ChatInput.file_upload_success': 'Fichier {name} t??l??charg??',
    'ChatInput.file_upload_progress': 't??l??chargement {count} Fichiers: {percent}%',
    'ChatInput.file_upload_forbidden': 'Vous devez ??tre identifi?? pour t??l??charger des fichiers',
    'ChatInput.file_size_limit': 'La taille du fichier {name} est sup??rieure a la limite impos??e {limit}Mb',

    'Welcome.welcome': 'Bienvenu sur Wirow!',
    'Welcome.guest': 'Guest',
    'Welcome.create_meeting': 'R??union',
    'Welcome.create_webinar': 'Webinar',
    'Welcome.join_live_room': 'Joindre le salon',
    'Welcome.admin_panel': 'Admin',
    'Welcome.sign_in': 'Connectez-vous',
    'Welcome.sign_out': 'D??connectez-vous',
    'Welcome.cannot_join_room_is_not_active': "Il n'est pas possible de joindre un salon inactif.",

    'History.caption': 'Sessions r??centes',
    'History.search_placeholder': 'Cherche ...',
    'History.remove_caption': '??tres vous certain de le supprimer ?',
    'History.remove_content': '{name}',

    'HistoryItem.remove': 'Enlever de la liste',
    'HistoryItem.details': 'D??tails des sessions',
    'HistoryItem.copy_room_url': 'Lien du salon copi??',

    'MeetingSettings.caption': 'R??glages',

    'MeetingMembers.caption': 'Membres',

    'MeetingJoinDialog.caption': 'Joindre le salon',
    'MeetingJoinDialog.room_identifier_title': 'Identifiant salon (UUID)',
    'MeetingJoinDialog.invalid_room_identifier': 'Identifiant salon invalide',
    'MeetingJoinDialog.join': 'Join',

    'MeetingLeaveDialog.title': 'Quitter le salon ?',
    'MeetingLeaveDialog.goodbye_title': '??tres vous sur ?',
    'MeetingLeaveDialog.button_back': 'Retour',
    'MeetingLeaveDialog.button_leave': 'Quitter le salon',

    'Public.acquiring_room_state': 'D??termine le statut du salon ...',
    'Public.joining_the_room': 'Rejoint ',
    'Public.button_join_no_signin': 'Rejoindre sans Rejoindre sans se connecter',
    'Public.button_joining_signin': 'Se connecter',
    'Public.login_failed': "Nom d'utilisateur ou mot de passe invalide !",
    'Public.awaiting_start': 'Attente du d??but de la r??union ...',

    'LoginDialog.caption': 'Se connecter',
    'LoginDialog.please_provide_username': 'Entrer votre nom',
    'LoginDialog.please_provide_password': 'Entrer le mot de passe',
    'LoginDialog.username': "Nom d'utilisateur",
    'LoginDialog.password': 'Mot de passe',
    'LoginDialog.proceed': 'Proc??der',

    'MediaDeviceSelector.audio_input': 'Entr??e audio',
    'MediaDeviceSelector.audio_input_idx': 'Entr??e audio {idx}',
    'MediaDeviceSelector.audio_output': 'Sortie Audio',
    'MediaDeviceSelector.audio_output_idx': 'Sortie audio {idx}',
    'MediaDeviceSelector.device': 'Appareil',
    'MediaDeviceSelector.unknown_device': 'Appareil inconnu',
    'MediaDeviceSelector.video_input': 'Entr??e vid??o',
    'MediaDeviceSelector.video_input_idx': 'Entr??e vid??o {idx}',

    'MeetingPrepareDialog.acquiring_local_video': 'Identifie les appareils vid??o locaux...',
    'MeetingPrepareDialog.create_room': 'Cr??er un salon',
    'MeetingPrepareDialog.failed_to_acquire_devices': ' ??chec de l???acquisition des appareils',
    'MeetingPrepareDialog.failed_to_join_the_room': ' ??chec de connexion au salon',
    'MeetingPrepareDialog.failed_to_create_a_room': "??chec de cr??ation d'un salon",
    'MeetingPrepareDialog.initializing': 'Initialisation ...',
    'MeetingPrepareDialog.join_room': 'Joint le salon',
    'MeetingPrepareDialog.joining_the_room': 'Joint le salon ...',
    'MeetingPrepareDialog.meeting': 'Conf??rence',
    'MeetingPrepareDialog.no_input_devices': 'Il semble qu???aucun appareil vid??o a ??t?? trouv?? sur l???ordinateur',
    'MeetingPrepareDialog.please_provide_a_room_title': 'Donner un nom au salon ',
    'MeetingPrepareDialog.please_provide_display_name': "Donner le nom ?? afficher sut l'??cran",
    'MeetingPrepareDialog.room_is_not_active{name}': "Le salon {name} n'est pas actif et peut ??tre utilis??",
    'MeetingPrepareDialog.room_missing_name': 'S.V.P. Donnez votre nom avant de rejoindre le salon',
    'MeetingPrepareDialog.room_title': 'Nom du salon',
    'MeetingPrepareDialog.webinar': 'Webinar',
    'MeetingPrepareDialog.your_display_name': 'Votre nom',
    'MeetingPrepareDialog.room_display_name': 'Nom du salon',
    'MeetingPrepareDialog.open_settings': 'Ouvrir le panneau de configuration',
    'MeetingPrepareDialog.close_settings': 'Fermer le panneau de configuration',

    'Meeting.tooltip_open_chat': "Ouvrir l'espace de chat",
    'Meeting.tooltip_close_chat': "Fermer l'espace de chat",
    'Meeting.tooltip_whiteboard': 'Ouvrir le tableau num??rique',
    'Meeting.tooltip_members': 'Membres du salon',
    'Meeting.tooltip_recording_start': "D??marrage de l'enregistrement du salon",
    'Meeting.tooltip_recording_stop': "Arr??t de l'enregistrement du salon",
    'Meeting.tooltip_setting': 'R??glages',
    'Meeting.tooltip_leave': 'Quitter le salon',
    'Meeting.input_device_connected': "P??riph??rique d'entr??e multim??dia connect??",
    'Meeting.input_device_disconnected': "P??riph??rique d'entr??e multim??dia d??connect??",

    'ActivityBar.tooltip_videoMute': 'D??activer la vid??o',
    'ActivityBar.tooltip_videoUnmute': 'Activer la vid??o',
    'ActivityBar.tooltip_audioMute': 'D??activer le son',
    'ActivityBar.tooltip_audioUnmute': 'Activer le son',
    'ActivityBar.tooltip_shareScreen': "Partager l'??cran",
    'ActivityBar.tooltip_shareScreenStop': "D??activer le partage de l'??cran",

    'MeetingGridUnit.tooltip_enter_fullscreen': 'Mettre en plein ??cran',
    'MeetingGridUnit.tooltip_exit_fullscreen': 'Quitter le plein ??cran',

    'RoomInfoDialog.activity': 'Arxtivit??es',
    'RoomInfoDialog.chat': 'Chat',
    'RoomInfoDialog.recording': 'Enregistrement',
    'RoomInfoDialog.fail': 'Donn??es du salon non obtenue',

    'RoomActivity.time': 'Temps',
    'RoomActivity.details': 'D??tails',
    'RoomActivity.created': 'Salon cr??e',
    'RoomActivity.closed': 'Salon ferme',
    'RoomActivity.joined': '{member} rejoint',
    'RoomActivity.left': '{member} quitte',
    'RoomActivity.recstart': "L'enregistrement commence",
    'RoomActivity.recstop': 'Enregistrement termin??',
    'RoomActivity.renamed': 'Salon {old} renomm?? {new}',
    'RoomActivity.whiteboard': '{member} ouvre le panneau num??rique',

    'RoomActivityRecording.pending': 'Enregistrement en cours de pr??paration ...',
    'RoomActivityRecording.failed': "la pr??paration de l'enregistrement a ??chou??e",

    // System events
    'event.RoomRecordingPPDone': '{name}: enregistrement vid??o pr??t',
  },
});

locale.set(getBrowserLocale('en'));

// Indirectly initialize formatMessage object
translate.subscribe(() => {});
