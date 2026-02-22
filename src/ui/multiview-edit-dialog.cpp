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

#include "multiview-edit-dialog.hpp"
#include "grid-editor-widget.hpp"
#include "cell-config-dialog.hpp"
#include "multiview-window.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QColorDialog>

MultiviewEditDialog::MultiviewEditDialog(const MultiviewConfig &config, bool isNew, QWidget *parent)
	: QDialog(parent),
	  config_(config),
	  isNew_(isNew)
{
	setWindowTitle(isNew ? LG_TEXT("EditDialog.CreateTitle")
			     : QString(LG_TEXT("EditDialog.EditTitle")).arg(config.name));
	setMinimumSize(800, 550);

	auto *mainLayout = new QVBoxLayout(this);

	// Name field
	auto *nameLayout = new QHBoxLayout();
	nameLayout->addWidget(new QLabel(LG_TEXT("EditDialog.Name")));
	nameEdit_ = new QLineEdit(config_.name);
	if (isNew_)
		nameEdit_->setPlaceholderText(LG_TEXT("EditDialog.NamePlaceholder"));
	nameLayout->addWidget(nameEdit_);
	mainLayout->addLayout(nameLayout);

	// Two-pane layout
	auto *paneLayout = new QHBoxLayout();

	// Left pane: grid editor
	auto *leftLayout = new QVBoxLayout();
	gridEditor_ = new GridEditorWidget();
	leftLayout->addWidget(gridEditor_, 1);

	// Template dropdown
	auto *templateLayout = new QHBoxLayout();
	templateLayout->addWidget(new QLabel(LG_TEXT("EditDialog.Template")));
	templateCombo_ = new QComboBox();
	templateCombo_->addItem(LG_TEXT("EditDialog.TemplateCurrent"));
	QStringList templateNames = GetConfigManager()->templateNames();
	templateNames.sort(Qt::CaseInsensitive);
	for (const QString &name : templateNames)
		templateCombo_->addItem(name);
	templateLayout->addWidget(templateCombo_, 1);
	leftLayout->addLayout(templateLayout);

	paneLayout->addLayout(leftLayout, 3);

	// Right pane: grid settings and buttons
	auto *rightLayout = new QVBoxLayout();

	// Grid size controls
	auto *gridSettingsForm = new QFormLayout();
	rowsSpin_ = new QSpinBox();
	rowsSpin_->setRange(1, 16);
	rowsSpin_->setValue(config_.cells.isEmpty() ? GetConfigManager()->defaultTemplate().gridRows
						    : config_.gridRows);
	gridSettingsForm->addRow(LG_TEXT("EditDialog.Rows"), rowsSpin_);

	colsSpin_ = new QSpinBox();
	colsSpin_->setRange(1, 16);
	colsSpin_->setValue(config_.cells.isEmpty() ? GetConfigManager()->defaultTemplate().gridCols
						    : config_.gridCols);
	gridSettingsForm->addRow(LG_TEXT("EditDialog.Columns"), colsSpin_);

	// Grid border width
	borderWidthSpin_ = new QSpinBox();
	borderWidthSpin_->setRange(1, 10);
	borderWidthSpin_->setValue(config_.gridBorderWidth);
	gridSettingsForm->addRow(LG_TEXT("EditDialog.BorderWidth"), borderWidthSpin_);

	// Grid line color
	gridLineColor_ = config_.gridLineColor;
	lineColorBtn_ = new QPushButton(LG_TEXT("EditDialog.LineColorChoose"));
	lineColorBtn_->setAutoFillBackground(true);
	auto updateColorBtnStyle = [this]() {
		lineColorBtn_->setStyleSheet(QString("background-color: %1; color: %2;")
						     .arg(gridLineColor_.name())
						     .arg(gridLineColor_.lightness() > 127 ? "black" : "white"));
	};
	updateColorBtnStyle();
	connect(lineColorBtn_, &QPushButton::clicked, this, [this, updateColorBtnStyle]() {
		QColor c = QColorDialog::getColor(gridLineColor_, this, LG_TEXT("EditDialog.ChooseLineColor"));
		if (c.isValid()) {
			gridLineColor_ = c;
			updateColorBtnStyle();
		}
	});
	gridSettingsForm->addRow(LG_TEXT("EditDialog.LineColor"), lineColorBtn_);

	rightLayout->addLayout(gridSettingsForm);
	rightLayout->addSpacing(10);

	// Widget action buttons
	setWidgetBtn_ = new QPushButton(LG_TEXT("EditDialog.SetWidget"));
	editWidgetBtn_ = new QPushButton(LG_TEXT("EditDialog.EditWidget"));
	mergeBtn_ = new QPushButton(LG_TEXT("EditDialog.MergeWidgets"));
	resetBtn_ = new QPushButton(LG_TEXT("EditDialog.ResetWidgets"));

	rightLayout->addWidget(setWidgetBtn_);
	rightLayout->addWidget(editWidgetBtn_);
	rightLayout->addSpacing(10);
	rightLayout->addWidget(mergeBtn_);
	rightLayout->addWidget(resetBtn_);
	rightLayout->addStretch();

	paneLayout->addLayout(rightLayout, 1);
	mainLayout->addLayout(paneLayout, 1);

	// Dialog buttons
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &MultiviewEditDialog::onConfirm);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	mainLayout->addWidget(buttons);

	// Load existing grid
	if (!config_.cells.isEmpty()) {
		gridEditor_->setGrid(config_.gridRows, config_.gridCols, config_.cells);
	} else {
		// Use default template for new multiviews
		TemplateConfig def = GetConfigManager()->defaultTemplate();
		gridEditor_->setGrid(def.gridRows, def.gridCols, def.cells);
	}

	// Connections
	connect(setWidgetBtn_, &QPushButton::clicked, this, &MultiviewEditDialog::onSetWidget);
	connect(editWidgetBtn_, &QPushButton::clicked, this, &MultiviewEditDialog::onEditWidget);
	connect(mergeBtn_, &QPushButton::clicked, this, &MultiviewEditDialog::onMergeWidgets);
	connect(resetBtn_, &QPushButton::clicked, this, &MultiviewEditDialog::onResetWidgets);
	connect(templateCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&MultiviewEditDialog::onTemplateChanged);
	connect(gridEditor_, &GridEditorWidget::selectionChanged, this, &MultiviewEditDialog::onSelectionChanged);
	connect(rowsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MultiviewEditDialog::onGridSizeChanged);
	connect(colsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MultiviewEditDialog::onGridSizeChanged);

	onSelectionChanged();
}

void MultiviewEditDialog::onSetWidget()
{
	int idx = gridEditor_->selectedCellIndex();
	WidgetConfig current;
	if (idx >= 0)
		current = gridEditor_->cells()[idx].widget;

	CellConfigDialog dlg(current, this);
	if (dlg.exec() == QDialog::Accepted) {
		gridEditor_->setWidgetForSelected(dlg.result());
	}
}

void MultiviewEditDialog::onEditWidget()
{
	onSetWidget(); // Same behavior - opens the config dialog for the selected cell
}

void MultiviewEditDialog::onMergeWidgets()
{
	if (!gridEditor_->canMergeSelected()) {
		QMessageBox::warning(this, LG_TEXT("EditDialog.CannotMerge"), LG_TEXT("EditDialog.CannotMergeMsg"));
		return;
	}
	gridEditor_->mergeSelected();
}

void MultiviewEditDialog::onResetWidgets()
{
	gridEditor_->resetSelected();
}

void MultiviewEditDialog::onTemplateChanged(int index)
{
	if (index <= 0)
		return; // "(Current)" selected

	QString templateName = templateCombo_->currentText();
	if (!GetConfigManager()->hasTemplate(templateName))
		return;

	auto reply = QMessageBox::question(this, LG_TEXT("EditDialog.ApplyTemplate"),
					   LG_TEXT("EditDialog.ApplyTemplateMsg"), QMessageBox::Yes | QMessageBox::No);
	if (reply == QMessageBox::Yes) {
		loadTemplate(GetConfigManager()->getTemplate(templateName));
	}

	// Reset combo to "(Current)"
	templateCombo_->blockSignals(true);
	templateCombo_->setCurrentIndex(0);
	templateCombo_->blockSignals(false);
}

void MultiviewEditDialog::onSelectionChanged()
{
	int cellIdx = gridEditor_->selectedCellIndex();
	bool hasSingleCell = cellIdx >= 0;

	setWidgetBtn_->setEnabled(hasSingleCell);
	editWidgetBtn_->setEnabled(hasSingleCell && cellIdx < gridEditor_->cells().size() &&
				   gridEditor_->cells()[cellIdx].widget.type != WidgetType::None);
	mergeBtn_->setEnabled(gridEditor_->canMergeSelected());
	resetBtn_->setEnabled(gridEditor_->canResetSelected());
}

void MultiviewEditDialog::onConfirm()
{
	QString name = nameEdit_->text().trimmed();
	if (name.isEmpty()) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("EditDialog.EnterName"));
		return;
	}

	if (isNew_ && GetConfigManager()->hasMultiview(name)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageDialog.MultiviewExists"));
		return;
	}

	config_.name = name;
	config_.gridRows = gridEditor_->gridRows();
	config_.gridCols = gridEditor_->gridCols();
	config_.gridBorderWidth = borderWidthSpin_->value();
	config_.gridLineColor = gridLineColor_;
	config_.cells = gridEditor_->cells();

	ConfigManager *cm = GetConfigManager();
	if (isNew_) {
		cm->addMultiview(config_);
		// Auto-open the new multiview window
		MultiviewWindow::openOrFocus(config_.name);
	} else {
		cm->updateMultiview(config_);
	}

	accept();
}

void MultiviewEditDialog::onGridSizeChanged()
{
	int newRows = rowsSpin_->value();
	int newCols = colsSpin_->value();
	int oldRows = gridEditor_->gridRows();
	int oldCols = gridEditor_->gridCols();

	if (newRows == oldRows && newCols == oldCols)
		return;

	// Keep cells that still fit, discard ones outside the new bounds
	QVector<CellConfig> oldCells = gridEditor_->cells();
	QVector<CellConfig> newCells;

	// Build ownership map for existing cells
	QVector<QVector<bool>> occupied(newRows);
	for (int r = 0; r < newRows; r++)
		occupied[r].resize(newCols, false);

	for (const CellConfig &cell : oldCells) {
		if (cell.row < newRows && cell.col < newCols) {
			CellConfig c = cell;
			// Clamp spans to fit
			if (c.row + c.rowSpan > newRows)
				c.rowSpan = newRows - c.row;
			if (c.col + c.colSpan > newCols)
				c.colSpan = newCols - c.col;
			newCells.append(c);

			// Mark positions as occupied
			for (int r = c.row; r < c.row + c.rowSpan; r++) {
				for (int col = c.col; col < c.col + c.colSpan; col++) {
					occupied[r][col] = true;
				}
			}
		}
	}

	// Create new empty cells for any unoccupied positions
	for (int r = 0; r < newRows; r++) {
		for (int c = 0; c < newCols; c++) {
			if (!occupied[r][c]) {
				CellConfig newCell;
				newCell.row = r;
				newCell.col = c;
				newCell.rowSpan = 1;
				newCell.colSpan = 1;
				newCells.append(newCell);
			}
		}
	}

	gridEditor_->setGrid(newRows, newCols, newCells);
}

void MultiviewEditDialog::loadTemplate(const TemplateConfig &tmpl)
{
	rowsSpin_->blockSignals(true);
	colsSpin_->blockSignals(true);
	rowsSpin_->setValue(tmpl.gridRows);
	colsSpin_->setValue(tmpl.gridCols);
	rowsSpin_->blockSignals(false);
	colsSpin_->blockSignals(false);

	// Auto-fill is handled by the default template itself (in ConfigManager::defaultTemplate).
	// User-created templates apply their cells as-is — placeholders stay as placeholders.
	gridEditor_->setGrid(tmpl.gridRows, tmpl.gridCols, tmpl.cells);
}

MultiviewConfig MultiviewEditDialog::result() const
{
	MultiviewConfig mv = config_;
	mv.name = nameEdit_->text().trimmed();
	mv.gridRows = gridEditor_->gridRows();
	mv.gridCols = gridEditor_->gridCols();
	mv.gridBorderWidth = borderWidthSpin_->value();
	mv.gridLineColor = gridLineColor_;
	mv.cells = gridEditor_->cells();
	return mv;
}
