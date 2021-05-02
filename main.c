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

#include "deadbeef.h"
#include <gtk/gtk.h>
#include <sys/stat.h>

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


GSList *
get_files(const gchar *dir_path)
{

    GSList *fpaths = NULL;
    GSList *dirs = NULL;
    DIR *cdir = NULL;
    struct dirent *cent = NULL;
    struct stat cent_stat;
    gchar *dir_pdup;

    if (dir_path == NULL)
    {
        return NULL;
    }

    dir_pdup = g_strdup((const gchar *)dir_path);
    dirs = g_slist_append(dirs, (gpointer)dir_pdup);
    while (dirs != NULL)
    {
        cdir = opendir((const gchar *)dirs->data);
        if (cdir == NULL)
        {
            g_slist_free(dirs);
            g_slist_free(fpaths);
            return NULL;
        }
        chdir((const gchar *)dirs->data);
        while ((cent = readdir(cdir)) != NULL)
        {
            lstat(cent->d_name, &cent_stat);
            if (S_ISDIR(cent_stat.st_mode))
            {
                if (g_strcmp0(cent->d_name, ".") == 0 ||
                    g_strcmp0(cent->d_name, "..") == 0)
                {
                    continue;
                }
                dirs = g_slist_append(dirs, g_strconcat((gchar *)dirs->data, "/", cent->d_name, NULL));
            }
            else
            {
                char *dot = strrchr(cent->d_name, '.');
                if (dot && strcmp(dot, ".jpg") != 0 && strcmp(dot, ".jpeg") != 0 && strcmp(dot, ".png") != 0)
                    fpaths = g_slist_append(fpaths, g_strconcat((gchar *)dirs->data, "/", cent->d_name, NULL));
            }
        }
        g_free(dirs->data);
        dirs = g_slist_delete_link(dirs, dirs);
        closedir(cdir);
    }
    return fpaths;
}

static int
Select_Folder(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)(action);
    (void)(ctx);

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt)
    {
        return 0;
    }

    GtkWidget *dlg = gtk_file_chooser_dialog_new("Select folder...", NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);
    gtk_window_set_transient_for(GTK_WINDOW(dlg), NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), FALSE);

    deadbeef->pl_lock();
    const char *oldfolder = deadbeef->plt_find_meta(plt, "Sync_Folder");
    printf("Old Sync folder: %s\n", oldfolder);

    if (oldfolder)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), oldfolder);

    int response = gtk_dialog_run(GTK_DIALOG(dlg));

    if (response == GTK_RESPONSE_OK)
    {
        char *folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dlg));
        if (folder)
        {
            deadbeef->plt_replace_meta(plt, "Sync_Folder", folder);
            deadbeef->plt_modified(plt);

            printf("New Sync folder: %s\n", folder);
            g_free(folder);
        }
    }

    gtk_widget_destroy(dlg);
    deadbeef->plt_unref(plt);
    deadbeef->pl_unlock();

    return 0;
}

static int
Sync_Playlist(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)(action);
    (void)(ctx);

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt)
    {
        return 0;
    }

    if (deadbeef->plt_add_files_begin(plt, 0) < 0)
    {
        deadbeef->plt_unref(plt);
        return 0;
    }

    const char *folder = deadbeef->plt_find_meta(plt, "Sync_Folder");
    if (!folder)
    {
        deadbeef->plt_unref(plt);
        Select_Folder(NULL, 0);
        return 0;
    }

    GSList *files = get_files(folder);
    if (!files)
    {
        deadbeef->plt_unref(plt);
        return 0;
    }

    deadbeef->pl_lock();

    DB_playItem_t *it = deadbeef->plt_get_first(plt, PL_MAIN);
    GSList *iterator = NULL;

    while (it)
    {
        DB_playItem_t *next = deadbeef->pl_get_next(it, PL_MAIN);
        const char *uri = deadbeef->pl_find_meta(it, ":URI");

        gboolean vanished_file = TRUE;
        for (iterator = files; iterator; iterator = iterator->next)
        {
            if (strcmp(iterator->data, uri) == 0)
            {
                files = g_slist_remove_link(files, iterator);
                g_slist_free(iterator);
                vanished_file = FALSE;
                break;
            }
        }

        if (vanished_file)
        {
            deadbeef->plt_remove_item(plt, it);
        }

        deadbeef->pl_item_unref(it);
        it = next;
    }

    if (files)
    {
        deadbeef->plt_add_files_begin(plt, 0);
        for (iterator = files; iterator; iterator = iterator->next)
        {
            // printf("Add file: %s\n", iterator->data);
            deadbeef->plt_add_file2(0, plt, iterator->data, NULL, NULL);
        }

        g_slist_free(files);
    }

    deadbeef->plt_add_files_end(plt, 0);
    deadbeef->pl_unlock();
    deadbeef->plt_modified(plt);
    deadbeef->plt_unref(plt);
    deadbeef->sendmessage(DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_CONTENT, 0);
    deadbeef->pl_save_current ();

    return 0;
}

static int
Remove_Vanished_Items(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)(action);
    (void)(ctx);

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt)
    {
        return 0;
    }

    deadbeef->pl_lock();

    DB_playItem_t *it = deadbeef->plt_get_first(plt, PL_MAIN);
    while (it)
    {
        DB_playItem_t *next = deadbeef->pl_get_next(it, PL_MAIN);
        const char *uri = deadbeef->pl_find_meta(it, ":URI");

        if (deadbeef->is_local_file(uri))
        {
            struct stat buffer;

            if (stat(uri, &buffer) != 0)
            {
                deadbeef->plt_remove_item(plt, it);
            }
        }

        deadbeef->pl_item_unref(it);
        it = next;
    }

    deadbeef->pl_unlock();
    deadbeef->plt_modified(plt);
    deadbeef->plt_unref(plt);
    deadbeef->sendmessage(DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_CONTENT, 0);
    deadbeef->pl_save_current ();

    return 0;
}

static int
Remove_Duplicate_Items(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)(action);
    (void)(ctx);

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt)
    {
        return 0;
    }

    deadbeef->pl_lock();

    DB_playItem_t *next, *it = deadbeef->plt_get_first(plt, PL_MAIN);

    while (it)
    {
        const char *uri = deadbeef->pl_find_meta(it, ":URI");

        if (deadbeef->is_local_file(uri))
        {
            DB_playItem_t *next2, *it2 = deadbeef->pl_get_next(it, PL_MAIN);

            while (it2)
            {
                const char *uri2 = deadbeef->pl_find_meta(it2, ":URI");
                next2 = deadbeef->pl_get_next(it2, PL_MAIN);

                if (strcmp(uri, uri2) == 0)
                {
                    deadbeef->plt_remove_item(plt, it2);
                }

                deadbeef->pl_item_unref(it2);
                it2 = next2;
            }
        }
        next = deadbeef->pl_get_next(it, PL_MAIN);

        deadbeef->pl_item_unref(it);
        it = next;
    }

    deadbeef->pl_unlock();
    deadbeef->plt_modified(plt);
    deadbeef->plt_unref(plt);
    deadbeef->sendmessage(DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_CONTENT, 0);
    deadbeef->pl_save_current ();

    return 0;
}

static int
Remove_Folder_Tag(DB_plugin_action_t *action, ddb_action_context_t ctx)
{
    (void)(action);
    (void)(ctx);

    ddb_playlist_t *plt = deadbeef->plt_get_curr();
    if (!plt)
    {
        return 0;
    }

    deadbeef->pl_lock();

    DB_metaInfo_t *meta = deadbeef->plt_get_metadata_head(plt);
    while (meta)
    {
        if (strcmp(meta->key, "Sync_Folder") == 0)
        {
            deadbeef->plt_delete_metadata(plt, meta);
            break;
        }
        meta = meta->next;
    }

    deadbeef->pl_unlock();
    deadbeef->plt_modified(plt);
    deadbeef->plt_unref(plt);
    deadbeef->sendmessage(DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_CONTENT, 0);
    deadbeef->pl_save_current ();

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
