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

#include "config-manager.hpp"
#include "../plugin.hpp"
#include "../ui/multiview-window.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QDir>
#include <QFile>
#include <QFont>
#include <QRegularExpression>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {}

ConfigManager::~ConfigManager() {}

// --- Per-collection multiview CRUD ---

QStringList ConfigManager::multiviewNames() const
{
	return multiviews_.keys();
}

bool ConfigManager::hasMultiview(const QString &name) const
{
	return multiviews_.contains(name);
}

MultiviewConfig ConfigManager::getMultiview(const QString &name) const
{
	return multiviews_.value(name);
}

void ConfigManager::addMultiview(const MultiviewConfig &mv)
{
	multiviews_[mv.name] = mv;
	saveCurrentCollection();
	emit multiviewAdded(mv.name);
}

void ConfigManager::updateMultiview(const MultiviewConfig &mv)
{
	multiviews_[mv.name] = mv;
	saveCurrentCollection();
	emit multiviewUpdated(mv.name);
}

void ConfigManager::removeMultiview(const QString &name)
{
	if (multiviews_.remove(name)) {
		saveCurrentCollection();
		emit multiviewRemoved(name);
	}
}

void ConfigManager::renameMultiview(const QString &oldName, const QString &newName)
{
	if (!multiviews_.contains(oldName) || multiviews_.contains(newName))
		return;
	MultiviewConfig mv = multiviews_.take(oldName);
	mv.name = newName;
	multiviews_[newName] = mv;
	saveCurrentCollection();
	emit multiviewRenamed(oldName, newName);
}

void ConfigManager::duplicateMultiview(const QString &srcName, const QString &newName)
{
	if (!multiviews_.contains(srcName) || multiviews_.contains(newName))
		return;
	MultiviewConfig mv = multiviews_[srcName];
	mv.name = newName;
	mv.wasOpen = false;
	multiviews_[newName] = mv;
	saveCurrentCollection();
	emit multiviewAdded(newName);
}

// --- Global layout templates ---

QStringList ConfigManager::templateNames() const
{
	return templates_.keys();
}

bool ConfigManager::hasTemplate(const QString &name) const
{
	return templates_.contains(name);
}

TemplateConfig ConfigManager::getTemplate(const QString &name) const
{
	return templates_.value(name);
}

void ConfigManager::addTemplate(const TemplateConfig &t)
{
	templates_[t.name] = t;
	saveTemplates();
	emit templatesChanged();
}

void ConfigManager::removeTemplate(const QString &name)
{
	if (templates_.remove(name)) {
		saveTemplates();
		emit templatesChanged();
	}
}

TemplateConfig ConfigManager::defaultTemplate() const
{
	TemplateConfig t;
	t.name = LG_TEXT("DefaultTemplate.Name");
	t.gridRows = 4;
	t.gridCols = 4;

	// Top-left 2x2: Preview
	{
		CellConfig cell;
		cell.row = 0;
		cell.col = 0;
		cell.rowSpan = 2;
		cell.colSpan = 2;
		cell.widget.type = WidgetType::Preview;
		cell.widget.labelText = LG_TEXT("DefaultTemplate.Preview");
		cell.widget.labelVisible = true;
		QFont font;
		font.setPointSize(20);
		cell.widget.labelFont = font.toString();
		t.cells.append(cell);
	}

	// Top-Right 2x2: Program
	{
		CellConfig cell;
		cell.row = 0;
		cell.col = 2;
		cell.rowSpan = 2;
		cell.colSpan = 2;
		cell.widget.type = WidgetType::Program;
		cell.widget.labelText = LG_TEXT("DefaultTemplate.Program");
		cell.widget.labelVisible = true;
		QFont font;
		font.setPointSize(20);
		cell.widget.labelFont = font.toString();
		t.cells.append(cell);
	}

	// Gather available scene names for auto-fill
	QStringList sceneNames;
	struct obs_frontend_source_list sceneList = {};
	obs_frontend_get_scenes(&sceneList);
	for (size_t i = 0; i < sceneList.sources.num; i++) {
		obs_source_t *src = sceneList.sources.array[i];
		const char *name = obs_source_get_name(src);
		if (name)
			sceneNames.append(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&sceneList);

	// Bottom two rows: fill with scenes where available, otherwise placeholders
	int sceneIdx = 0;
	for (int r = 2; r <= 3; r++) {
		for (int c = 0; c <= 3; c++) {
			CellConfig cell;
			cell.row = r;
			cell.col = c;
			cell.widget.labelVisible = true;

			if (sceneIdx < sceneNames.size()) {
				cell.widget.type = WidgetType::Scene;
				cell.widget.sceneName = sceneNames[sceneIdx];
				cell.widget.labelText = sceneNames[sceneIdx];
				sceneIdx++;
			} else {
				cell.widget.type = WidgetType::Placeholder;
			}
			t.cells.append(cell);
		}
	}

	return t;
}

// --- JSON persistence ---

void ConfigManager::ensureConfigDir()
{
	char *path = obs_module_config_path("multiviews");
	if (path) {
		QDir().mkpath(QString::fromUtf8(path));
		bfree(path);
	}
}

QString ConfigManager::collectionConfigPath() const
{
	char *collection = obs_frontend_get_current_scene_collection();
	QString collectionName = collection ? QString::fromUtf8(collection) : "default";
	bfree(collection);
	// Sanitize filename
	collectionName.replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "_");

	char *dir = obs_module_config_path("multiviews");
	QString result;
	if (dir) {
		result = QString::fromUtf8(dir) + "/" + collectionName + ".json";
		bfree(dir);
	}
	return result;
}

QString ConfigManager::templatesConfigPath() const
{
	char *path = obs_module_config_path("templates.json");
	QString result;
	if (path) {
		result = QString::fromUtf8(path);
		bfree(path);
	}
	return result;
}

void ConfigManager::loadForCurrentCollection()
{
	multiviews_.clear();

	QString path = collectionConfigPath();
	if (path.isEmpty())
		return;

	obs_data_t *root = obs_data_create_from_json_file(path.toUtf8().constData());
	if (!root)
		return;

	obs_data_array_t *arr = obs_data_get_array(root, "multiviews");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(arr, i);
			MultiviewConfig mv = MultiviewSerializer::MultiviewFromData(item);
			if (!mv.name.isEmpty())
				multiviews_[mv.name] = mv;
			obs_data_release(item);
		}
		obs_data_array_release(arr);
	}
	obs_data_release(root);

	emit multiviewsReloaded();
}

void ConfigManager::saveCurrentCollection()
{
	if (suppressSave_)
		return;

	ensureConfigDir();
	QString path = collectionConfigPath();
	if (path.isEmpty())
		return;

	obs_data_t *root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();

	for (const auto &mv : multiviews_) {
		obs_data_t *item = MultiviewSerializer::MultiviewToData(mv);
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}

	obs_data_set_array(root, "multiviews", arr);
	obs_data_array_release(arr);

	obs_data_save_json(root, path.toUtf8().constData());
	obs_data_release(root);
}

void ConfigManager::loadTemplates()
{
	templates_.clear();

	// Always ensure default template exists
	TemplateConfig def = defaultTemplate();
	templates_[def.name] = def;

	QString path = templatesConfigPath();
	if (path.isEmpty())
		return;

	obs_data_t *root = obs_data_create_from_json_file(path.toUtf8().constData());
	if (!root)
		return;

	obs_data_array_t *arr = obs_data_get_array(root, "templates");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(arr, i);
			TemplateConfig t = MultiviewSerializer::TemplateFromData(item);
			if (!t.name.isEmpty())
				templates_[t.name] = t;
			obs_data_release(item);
		}
		obs_data_array_release(arr);
	}
	obs_data_release(root);

	emit templatesChanged();
}

void ConfigManager::saveTemplates()
{
	ensureConfigDir();
	QString path = templatesConfigPath();
	if (path.isEmpty())
		return;

	obs_data_t *root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();

	for (const auto &t : templates_) {
		// Don't persist the built-in default
		if (t.name == defaultTemplate().name)
			continue;
		obs_data_t *item = MultiviewSerializer::TemplateToData(t);
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}

	obs_data_set_array(root, "templates", arr);
	obs_data_array_release(arr);

	obs_data_save_json(root, path.toUtf8().constData());
	obs_data_release(root);
}

void ConfigManager::onSceneCollectionChanging()
{
	// Mark all currently open windows as wasOpen before saving, so they
	// can be restored when this collection is loaded again later.
	// Then save while the collection path still points to the old collection.
	// Suppress further saves so that closeAll() doesn't write stale data
	// to the new collection's file.
	for (auto &mv : multiviews_) {
		if (MultiviewWindow::findByName(mv.name))
			mv.wasOpen = true;
	}
	saveCurrentCollection();
	suppressSave_ = true;
}

void ConfigManager::onSceneCollectionChanged()
{
	suppressSave_ = false;
	loadForCurrentCollection();
}
