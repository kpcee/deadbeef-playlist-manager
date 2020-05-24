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
#include <string.h>
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


static int
action_Remove_Vanished_Items(DB_plugin_action_t *action, int ctx)
{
    ddb_playlist_t *plt = deadbeef->plt_get_curr ();
    if (!plt) {
        return 0;
    }

    deadbeef->pl_lock();

    DB_playItem_t *it = deadbeef->plt_get_first (plt, PL_MAIN);
    while (it) {
        DB_playItem_t *next = deadbeef->pl_get_next (it, PL_MAIN);
        const char *uri = deadbeef->pl_find_meta (it, ":URI");

        if (deadbeef->is_local_file (uri)) {
           struct stat buffer;

            if (stat (uri, &buffer) != 0) {
               deadbeef->plt_remove_item (plt, it);
            }
        }

        deadbeef->pl_item_unref (it);
        it = next;
    }

    deadbeef->pl_unlock ();
    deadbeef->plt_modified (plt);
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_CONTENT, 0);
    //deadbeef->pl_save_all (); // save all changed playlists

    return 0;
}



static int
action_Remove_Duplicate_Items(DB_plugin_action_t *action, int ctx)
{
    ddb_playlist_t *plt = deadbeef->plt_get_curr ();
    if (!plt) {
        return 0;
    }

    deadbeef->pl_lock();

    DB_playItem_t *next, *it = deadbeef->plt_get_first (plt, PL_MAIN);

    while (it) {
        const char *uri = deadbeef->pl_find_meta (it, ":URI");

        if (deadbeef->is_local_file (uri)) {
            DB_playItem_t *next2, *it2 = deadbeef->pl_get_next (it, PL_MAIN);

            while (it2) {
                const char *uri2 = deadbeef->pl_find_meta (it2, ":URI");
                next2 = deadbeef->pl_get_next (it2, PL_MAIN);

                if (strcmp (uri, uri2) == 0) {
                    deadbeef->plt_remove_item (plt, it2);
                }

                deadbeef->pl_item_unref (it2);
                it2 = next2;
            }
        }
        next = deadbeef->pl_get_next (it, PL_MAIN);

        deadbeef->pl_item_unref (it);
        it = next;
    }

    deadbeef->pl_unlock ();
    deadbeef->plt_modified (plt);
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_CONTENT, 0);
    //deadbeef->pl_save_all (); // save all changed playlists

    return 0;
}


static int
action_duplicate_files(DB_plugin_action_t *action, int ctx)
{
    return action_Remove_Duplicate_Items(action, ctx);
}

static int
action_vanished_files(DB_plugin_action_t *action, int ctx)
{
    return action_Remove_Vanished_Items(action, ctx);
}


static DB_plugin_action_t playlist_manager_vanished_files_action = {
    .title = "Playlist Manager/Remove Vanished Items",
    .name = "playlist_manager_vanished_files",
    .flags = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU,
    .callback2 = action_vanished_files,
    .next = NULL
};

static DB_plugin_action_t playlist_manager_duplicate_files_action = {
    .title = "Playlist Manager/Remove Duplicate Items",
    .name = "playlist_manager_duplicate_files",
    .flags = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU,
    .callback2 = action_duplicate_files,
    .next = &playlist_manager_vanished_files_action
};

static DB_plugin_action_t *
plugin_get_actions(DB_playItem_t *it)
{
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
    .plugin.descr = "Removes duplicate and vanished files from the current playlist",
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
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n"
    ,
    .plugin.website = "http://deadbeef.sf.net",
    .plugin.start = plugin_start,
    .plugin.stop = plugin_stop,
    .plugin.get_actions = plugin_get_actions,
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
