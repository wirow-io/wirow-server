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

  de: {
    'Menu.caption_goback': 'Zurück',
    'Menu.caption_dashboard': 'Dashboard',
    'Menu.caption_users': 'Anwenderverwaltung',

    'Users.error_exists': 'Anwender bereits vorhanden',
    'Users.user_create': 'Anwender anlegen ',
    'Users.search_placeholder': 'Anwender suchen',
    'Users.error_max_users': 'Der Anzahl an Anwender ({limit}), entsprechend Abonnement, ist erreicht',

    'UserEditDialog.error_username_required': 'Bitte ein Anwendername eintragen',
    'UserEditDialog.error_password_required': 'Bitte geben die das Anwenderpasswort ein',
    'UserEditDialog.error_email_invalid': 'Ungültige Email Adresse',
    'UserEditDialog.error_role_unknown': 'Role {name} unbekannt',
    'UserEditDialog.error_save': 'Spreichern des Anwenders gescheitert',
    'UserEditDialog.error_create': 'Anlegen des Anwenders gescheitert',
    'UserEditDialog.user_create': 'Neue Anwender anlegen',
    'UserEditDialog.user_edit': 'Anwender editieren',
    'UserEditDialog.input_name': 'Anwendername',
    'UserEditDialog.input_perms': 'Berechtigungen',
    'UserEditDialog.input_perms_placeholder': 'Komma getrennte Liste von Anwenderrolle',
    'UserEditDialog.input_notes': 'Notizen',
    'UserEditDialog.input_email': 'Email',
    'UserEditDialog.input_password': 'Passwort',
    'UserEditDialog.button_save_edit': 'Sichern',
    'UserEditDialog.button_save_create': 'Erzeugen',
    'UserEditDialog.updated': 'Anwender erfolgreich geändert',
    'UserEditDialog.created': 'Anwender erfolgreich angelegt',

    'UserRemoveDialog.error_remove': 'Entfernen des Anwenders gescheitert',
    'UserRemoveDialog.caption': 'Anwender entfernen',
    'UserRemoveDialog.caption_confirm': 'Bitte betätigen das Entfernen des Anwenders {name}',
    'UserRemoveDialog.removed': 'Anwender erfolgreich entfernt',

    'Dashboard.12h': '12H',
    'Dashboard.24h': '24H',
    'Dashboard.week': 'Woche',
    'Dashboard.month': 'Monat',
    'Dashboard.chart.title': '{users} online Anwender im {rooms} Räume',
    'Dashboard.chart.title.noactive': 'Momentan, kein  Raum aktiv',
    'Dashboard.chart.label.rooms': 'Räume',
    'Dashboard.chart.label.users': 'Anwenders',
    'Dashboard.chart.label.workers': 'Workers',
    'Dashboard.chart.label.streams': 'Streams',
  },

  fr: {
    'Menu.caption_goback': 'Retour',
    'Menu.caption_dashboard': 'tableau de bord',
    'Menu.caption_users': 'Gestion des utilisateurs',

    'Users.error_exists': "L'utilisateur existe!",
    'Users.user_create': 'Ajouter un utilisateur',
    'Users.search_placeholder': 'Recherche des utilisateurs',
    'Users.error_max_users': 'Vous avez déjà les {limit} utilisateurs limités par abonnement',

    'UserEditDialog.error_username_required': "Donnez un nom d'utilisateur",
    'UserEditDialog.error_password_required': "Donnez un mot de passe pour l'utilisateur",
    'UserEditDialog.error_email_invalid': "L'adresse courriel est invalide",
    'UserEditDialog.error_role_unknown': 'Rolle {name} inconnu',
    'UserEditDialog.error_save': "Échec de l'enregistrement de l’utilisateur",
    'UserEditDialog.error_create': "Échec de l'ajout d’un utilisateur",
    'UserEditDialog.user_create': 'Ajouter un nouvel utilisateur',
    'UserEditDialog.user_edit': 'Modification de l’utilisateur',
    'UserEditDialog.input_name': "No de l'utilisateur",
    'UserEditDialog.input_perms': 'Permissions',
    'UserEditDialog.input_perms_placeholder': 'Liste de rôles séparés par virgule',
    'UserEditDialog.input_notes': 'Notes',
    'UserEditDialog.input_email': 'Courriel',
    'UserEditDialog.input_password': 'Mot de passe',
    'UserEditDialog.button_save_edit': 'Enregistrer',
    'UserEditDialog.button_save_create': 'Ajouter',
    'UserEditDialog.updated': 'Utilsateur modifié avec succès',
    'UserEditDialog.created': 'Utilsateur ajouté avec succès',

    'UserRemoveDialog.error_remove': "Échec pour enlever l'utilisateur",
    'UserRemoveDialog.caption': 'Enlever un utilisateur',
    'UserRemoveDialog.caption_confirm': "confirmer la suppression de l'utilisateur {name}",
    'UserRemoveDialog.removed': 'Utilisateur supprimé avec succès',

    'Dashboard.12h': '12H',
    'Dashboard.24h': '24H',
    'Dashboard.week': 'Semaine',
    'Dashboard.month': 'Mois',
    'Dashboard.chart.title': '{users} utilisateurs en ligne dans {rooms} salons',
    'Dashboard.chart.title.noactive': 'Actuellement pas de salon actif',
    'Dashboard.chart.label.rooms': 'Salon',
    'Dashboard.chart.label.users': 'Utilisateur',
    'Dashboard.chart.label.workers': 'Workers',
    'Dashboard.chart.label.streams': 'Streams',
  },
});
