/*
OBS Looking Glass - Custom Dynamic Multiview Plugin
Copyright (C) 2025

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "plugin.hpp"
#include "core/config-manager.hpp"
#include "ui/tools-menu.hpp"
#include "ui/multiview-window.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("ZombieHDGaming");
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("LookingGlass");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("LookingGlass");
}

// Global singleton instances for configuration and menu management
static ConfigManager *s_configManager = nullptr;
static ToolsMenuManager *s_toolsMenuManager = nullptr;

ConfigManager *GetConfigManager()
{
	return s_configManager;
}

ToolsMenuManager *GetToolsMenuManager()
{
	return s_toolsMenuManager;
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		// Save state and suppress further saves before closing windows,
		// so closeAll() doesn't write old data to the new collection's file
		s_configManager->onSceneCollectionChanging();
		MultiviewWindow::closeAll();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		// Reload configs for the new collection and restore windows
		s_configManager->onSceneCollectionChanged();
		MultiviewWindow::reopenPreviouslyOpen();
		break;

	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		// Initial startup: load configs and build the Tools menu
		s_configManager->loadTemplates();
		s_configManager->loadForCurrentCollection();
		s_toolsMenuManager->initialize();
		MultiviewWindow::reopenPreviouslyOpen();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		// Save open-window state before closing so they reopen on next launch
		s_configManager->onSceneCollectionChanging();
		MultiviewWindow::closeAll();
		break;

	default:
		break;
	}
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	s_configManager = new ConfigManager();
	s_toolsMenuManager = new ToolsMenuManager();

	obs_frontend_add_event_callback(on_frontend_event, nullptr);

	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");

	obs_frontend_remove_event_callback(on_frontend_event, nullptr);

	delete s_toolsMenuManager;
	s_toolsMenuManager = nullptr;

	delete s_configManager;
	s_configManager = nullptr;
}
