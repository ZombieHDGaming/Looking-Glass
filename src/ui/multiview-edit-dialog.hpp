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
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>

#include "../core/multiview-config.hpp"

class GridEditorWidget;

/**
 * Dialog for creating or editing a multiview layout.
 * Provides a visual grid editor, grid size controls, template selection,
 * and cell merge/reset/configure operations.
 */
class MultiviewEditDialog : public QDialog {
	Q_OBJECT

public:
	explicit MultiviewEditDialog(const MultiviewConfig &config, bool isNew, QWidget *parent = nullptr);
	MultiviewConfig result() const;

private slots:
	void onSetWidget();
	void onEditWidget();
	void onMergeWidgets();
	void onResetWidgets();
	void onTemplateChanged(int index);
	void onSelectionChanged();
	void onConfirm();

private:
	void loadTemplate(const TemplateConfig &tmpl);
	void onGridSizeChanged();

	GridEditorWidget *gridEditor_;
	QLineEdit *nameEdit_;
	QSpinBox *rowsSpin_;
	QSpinBox *colsSpin_;
	QComboBox *templateCombo_;
	QPushButton *setWidgetBtn_;
	QPushButton *editWidgetBtn_;
	QPushButton *mergeBtn_;
	QPushButton *resetBtn_;
	QSpinBox *borderWidthSpin_;
	QPushButton *lineColorBtn_;
	QColor gridLineColor_;

	MultiviewConfig config_;
	bool isNew_;
};
