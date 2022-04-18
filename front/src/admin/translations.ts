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

import { translations } from 'svelte-intl';

translations.update({
  en: {
    'Menu.caption_goback': 'Go back',
    'Menu.caption_dashboard': 'Dashboard',
    'Menu.caption_users': 'Users management',

    'Users.error_exists': 'User is already exists',
    'Users.user_create': 'Create a user ',
    'Users.search_placeholder': 'Search users',
    'Users.error_max_users': 'You already have {limit} users limited by subscription',

    'UserEditDialog.error_username_required': 'Please provide a username',
    'UserEditDialog.error_password_required': 'Please provide user password',
    'UserEditDialog.error_email_invalid': 'Email address is invalid',
    'UserEditDialog.error_role_unknown': 'Role {name} is not known',
    'UserEditDialog.error_save': 'Failed to save user',
    'UserEditDialog.error_create': 'Failed to create user',
    'UserEditDialog.user_create': 'Create new user',
    'UserEditDialog.user_edit': 'Edit user',
    'UserEditDialog.input_name': 'User name',
    'UserEditDialog.input_perms': 'Permissions',
    'UserEditDialog.input_perms_placeholder': 'Comma separated list of user roles',
    'UserEditDialog.input_notes': 'Notes',
    'UserEditDialog.input_email': 'Email',
    'UserEditDialog.input_password': 'Password',
    'UserEditDialog.button_save_edit': 'Save',
    'UserEditDialog.button_save_create': 'Create',
    'UserEditDialog.updated': 'User updated successfully',
    'UserEditDialog.created': 'User created successfully',

    'UserRemoveDialog.error_remove': 'Failed to remove user',
    'UserRemoveDialog.caption': 'Remove user',
    'UserRemoveDialog.caption_confirm': 'Please confirm removal of user {name}',
    'UserRemoveDialog.removed': 'User removed successfully',

    'Dashboard.12h': '12H',
    'Dashboard.24h': '24H',
    'Dashboard.week': 'Week',
    'Dashboard.month': 'Month',
    'Dashboard.chart.title': '{users} online users in {rooms} room(s)',
    'Dashboard.chart.title.noactive': 'No active rooms at now',
    'Dashboard.chart.label.rooms': 'Rooms',
    'Dashboard.chart.label.users': 'Users',
    'Dashboard.chart.label.workers': 'Workers',
    'Dashboard.chart.label.streams': 'Streams',
  },
});
