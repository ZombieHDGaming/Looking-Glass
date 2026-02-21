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

#pragma once

#include <QObject>
#include <QMap>
#include <QString>

#include "multiview-config.hpp"

/**
 * Manages multiview layouts per scene collection and global reusable templates.
 * Handles persistence to JSON files and emits signals when configs change.
 */
class ConfigManager : public QObject {
	Q_OBJECT

public:
	explicit ConfigManager(QObject *parent = nullptr);
	~ConfigManager();

	// Per-collection multiview CRUD
	QStringList multiviewNames() const;
	bool hasMultiview(const QString &name) const;
	MultiviewConfig getMultiview(const QString &name) const;
	void addMultiview(const MultiviewConfig &mv);
	void updateMultiview(const MultiviewConfig &mv);
	void removeMultiview(const QString &name);
	void renameMultiview(const QString &oldName, const QString &newName);
	void duplicateMultiview(const QString &srcName, const QString &newName);

	// Global layout templates
	QStringList templateNames() const;
	bool hasTemplate(const QString &name) const;
	TemplateConfig getTemplate(const QString &name) const;
	void addTemplate(const TemplateConfig &t);
	void removeTemplate(const QString &name);
	TemplateConfig defaultTemplate() const;

	// JSON persistence
	void loadForCurrentCollection();
	void saveCurrentCollection();
	void loadTemplates();
	void saveTemplates();

	// Scene collection lifecycle
	void onSceneCollectionChanging();
	void onSceneCollectionChanged();

	// Suppress saves (used during collection switches to avoid cross-contamination)
	bool isSavingSuppressed() const { return suppressSave_; }

signals:
	void multiviewAdded(const QString &name);
	void multiviewRemoved(const QString &name);
	void multiviewRenamed(const QString &oldName, const QString &newName);
	void multiviewUpdated(const QString &name);
	void multiviewsReloaded();
	void templatesChanged();

private:
	QString collectionConfigPath() const;
	QString templatesConfigPath() const;
	void ensureConfigDir();

	QMap<QString, MultiviewConfig> multiviews_;
	QMap<QString, TemplateConfig> templates_;
	bool suppressSave_ = false;
};
