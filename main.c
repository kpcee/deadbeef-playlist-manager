/*
    Playlist Manager, a plugin for the DeaDBeeF audio player

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#define _XOPEN_SOURCE 700

#include <gtk/gtk.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libintl.h>
#include "deadbeef.h"

#define _(String) gettext(String)

static DB_misc_t plugin;
static DB_functions_t *deadbeef;

DB_plugin_t *
playlist_manager_load(DB_functions_t *api)
{
    deadbeef = api;
    return DB_PLUGIN(&plugin);
}

static int
plugin_start(void)
{
    return 0;
}

static int
plugin_stop(void)
{
    return 0;
}

static GHashTable *
get_file_hash_table(const gchar *dir_path)
{
    if (!dir_path) {
        return NULL;
    }

    GHashTable *file_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GSList *dirs = g_slist_append(NULL, g_strdup(dir_path));

    while (dirs) {
        gchar *current_dir = dirs->data;
        DIR *dir = opendir(current_dir);

        if (!dir) {
            g_free(current_dir);
            dirs = g_slist_delete_link(dirs, dirs);
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            gchar *path = g_build_filename(current_dir, entry->d_name, NULL);
            struct stat st;
            if (stat(path, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    if (g_strcmp0(entry->d_name, ".") != 0 && g_strcmp0(entry->d_name, "..") != 0) {
                        dirs = g_slist_append(dirs, path);
                        path = NULL;
                    }
                } else {
                    const gchar *ext = strrchr(entry->d_name, '.');
                    if (!ext || (g_strcmp0(ext, ".jpg") != 0 && g_strcmp0(ext, ".jpeg") != 0 && g_strcmp0(ext, ".png") != 0)) {
                        g_hash_table_add(file_table, path);
                        path = NULL;
                    }
                }
            }
            g_free(path);
        }

        closedir(dir);
        g_free(current_dir);
        dirs = g_slist_delete_link(dirs, dirs);
    }

    return file_table;
}

static int
Select_Folder(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)action;
    (void)ctx;

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt) {
        return 0;
    }

    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select folder...",
        NULL,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Open"), GTK_RESPONSE_OK,
        NULL);

    const char *oldfolder = deadbeef->plt_find_meta(plt, "Sync_Folder");
    if (oldfolder) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), oldfolder);
    }

    int response = gtk_dialog_run(GTK_DIALOG(dlg));
    if (response == GTK_RESPONSE_OK) {
        char *folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dlg));
        if (folder) {
            deadbeef->plt_replace_meta(plt, "Sync_Folder", folder);
            deadbeef->plt_modified(plt);
            g_free(folder);
        }
    }

    gtk_widget_destroy(dlg);
    deadbeef->plt_unref(plt);
    return 0;
}

static int
Sync_Playlist(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)action;
    (void)ctx;

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt) {
        return 0;
    }

    const char *folder = deadbeef->plt_find_meta(plt, "Sync_Folder");
    if (!folder) {
        deadbeef->plt_unref(plt);
        return Select_Folder(NULL, 0);
    }

    GHashTable *file_table = get_file_hash_table(folder);
    if (!file_table) {
        deadbeef->plt_unref(plt);
        return 0;
    }

    deadbeef->pl_lock();

    DB_playItem_t *it = deadbeef->plt_get_first(plt, PL_MAIN);
    while (it) {
        DB_playItem_t *next = deadbeef->pl_get_next(it, PL_MAIN);
        const char *uri = deadbeef->pl_find_meta(it, ":URI");

        if (g_hash_table_contains(file_table, uri)) {
            g_hash_table_remove(file_table, uri);
        } else {
            deadbeef->plt_remove_item(plt, it);
        }

        deadbeef->pl_item_unref(it);
        it = next;
    }

    GHashTableIter iter;
    gpointer key;
    
    deadbeef->plt_add_files_begin(plt, 0);
    g_hash_table_iter_init(&iter, file_table);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        deadbeef->plt_add_file2(0, plt, key, NULL, NULL);
    }

    g_hash_table_destroy(file_table);
    deadbeef->plt_add_files_end(plt, 0);
    deadbeef->pl_unlock();
    deadbeef->plt_modified(plt);
    deadbeef->plt_unref(plt);

    return 0;
}


static int Remove_Vanished_Items(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)action;
    (void)ctx;

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt) {
        return 0;
    }

    deadbeef->pl_lock();

    GArray *items_to_remove = g_array_new(FALSE, FALSE, sizeof(DB_playItem_t *));
    DB_playItem_t *it = deadbeef->plt_get_first(plt, PL_MAIN);
    GHashTable *checked_uris = g_hash_table_new(g_str_hash, g_str_equal);

    while (it) {
        const char *uri = deadbeef->pl_find_meta(it, ":URI");

        if (deadbeef->is_local_file(uri)) {
            if (!g_hash_table_contains(checked_uris, uri)) {
                struct stat buffer;
                if (stat(uri, &buffer) != 0) {
                    g_array_append_val(items_to_remove, it);
                }
                g_hash_table_add(checked_uris, (gpointer)uri);
            }
        }

        deadbeef->pl_item_unref(it);
        it = deadbeef->pl_get_next(it, PL_MAIN);
    }

   for (guint i = 0; i < items_to_remove->len; i++) {
        deadbeef->plt_remove_item(plt, g_array_index(items_to_remove, DB_playItem_t *, i));
    }

    g_array_free(items_to_remove, TRUE);
    g_hash_table_destroy(checked_uris);
    deadbeef->pl_unlock();
    deadbeef->plt_modified(plt);
    deadbeef->plt_unref(plt);
    return 0;
}



static int Remove_Duplicate_Items(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)action;
    (void)ctx;

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt) {
        return 0;
    }

    deadbeef->pl_lock();

    GHashTable *seen_uris = g_hash_table_new(g_str_hash, g_str_equal);
    DB_playItem_t *it = deadbeef->plt_get_first(plt, PL_MAIN);

    while (it) {
        const char *uri = deadbeef->pl_find_meta(it, ":URI");

        if (g_hash_table_contains(seen_uris, uri)) {
            DB_playItem_t *to_remove = it; 
            it = deadbeef->pl_get_next(it, PL_MAIN); 
            deadbeef->plt_remove_item(plt, to_remove);
        } else {
            g_hash_table_insert(seen_uris, (gpointer)uri, NULL);
            it = deadbeef->pl_get_next(it, PL_MAIN); 
        }
    }

    g_hash_table_destroy(seen_uris);
    deadbeef->pl_unlock();
    deadbeef->plt_modified(plt);
    deadbeef->plt_unref(plt);
    return 0;
}



static int
Remove_Folder_Tag(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)action;
    (void)ctx;

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt) {
        return 0;
    }

    deadbeef->pl_lock();

    DB_metaInfo_t *meta = deadbeef->plt_get_metadata_head(plt);
    while (meta) {
        if (g_strcmp0(meta->key, "Sync_Folder") == 0) {
            deadbeef->plt_delete_metadata(plt, meta);
            break;
        }
        meta = meta->next;
    }

    deadbeef->pl_unlock();
    deadbeef->plt_modified(plt);
    deadbeef->plt_unref(plt);
    return 0;
}

static DB_plugin_action_t remove_folder_tag_action = {
    .title = "Playlist Manager/› Reset Folder Tag",
    .name = "Remove_Folder_Tag",
    .flags = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU,
    .callback2 = Remove_Folder_Tag,
    .next = NULL};

static DB_plugin_action_t select_folder_action = {
    .title = "Playlist Manager/› Select Folder",
    .name = "Select_Folder",
    .flags = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU,
    .callback2 = Select_Folder,
    .next = &remove_folder_tag_action};

static DB_plugin_action_t playlist_sync_playlist_action = {
    .title = "Playlist Manager/Sync Playlist",
    .name = "Sync_Playlist",
    .flags = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU,
    .callback2 = Sync_Playlist,
    .next = &select_folder_action};

static DB_plugin_action_t playlist_manager_vanished_files_action = {
    .title = "Playlist Manager/Remove Vanished Items",
    .name = "Remove_Vanished_Items",
    .flags = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU,
    .callback2 = Remove_Vanished_Items,
    .next = &playlist_sync_playlist_action};

static DB_plugin_action_t playlist_manager_duplicate_files_action = {
    .title = "Playlist Manager/Remove Duplicate Items",
    .name = "Remove_Duplicate_Items",
    .flags = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU,
    .callback2 = Remove_Duplicate_Items,
    .next = &playlist_manager_vanished_files_action};

static DB_plugin_action_t *get_actions(DB_playItem_t *it)
{
    (void)it;
    return &playlist_manager_duplicate_files_action;
}

static DB_misc_t plugin = {
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 5,
    .plugin.version_major = 1,
    .plugin.version_minor = 0,
    .plugin.type = DB_PLUGIN_MISC,
#if GTK_CHECK_VERSION(3, 0, 0)
    .plugin.id = "playlist_manager_gtk3",
#else
    .plugin.id = "playlist_manager_gtk2",
#endif
    .plugin.name = "Playlist Manager",
    .plugin.descr = "Sync the current playlist with a selected folder or removes duplicate and vanished files within the current playlist",
    .plugin.copyright =
        "Playlist Manager plugin for DeaDBeeF Player\n"
        "Author: kpcee\n"
        "\n"
        "This program is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU General Public License\n"
        "as published by the Free Software Foundation; either version 2\n"
        "of the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program; if not, write to the Free Software\n"
        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n",
    .plugin.website = "https://github.com/kpcee/deadbeef-playlist-manager",
    .plugin.start = plugin_start,
    .plugin.stop = plugin_stop,
    .plugin.get_actions = get_actions,
};

#if GTK_CHECK_VERSION(3, 0, 0)
DB_plugin_t *playlist_manager_gtk3_load(DB_functions_t *api)
#else
DB_plugin_t *playlist_manager_gtk2_load(DB_functions_t *api)
#endif
{
    deadbeef = api;
    return DB_PLUGIN(&plugin);
}
