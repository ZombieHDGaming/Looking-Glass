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

#include <QDialog>
#include <QListWidget>

/**
 * Dialog for managing global layout templates.
 * Provides rename, edit, and delete actions for user-created templates.
 * The built-in default template is listed but cannot be renamed or deleted.
 */
class ManageTemplatesDialog : public QDialog {
	Q_OBJECT

public:
	explicit ManageTemplatesDialog(QWidget *parent = nullptr);

private slots:
	void onRename();
	void onEdit();
	void onDelete();
	void refreshList();
	void onSelectionChanged();

private:
	bool isDefaultTemplate(const QString &name) const;

	QListWidget *listWidget_;
	QPushButton *renameBtn_;
	QPushButton *editBtn_;
	QPushButton *deleteBtn_;
};
