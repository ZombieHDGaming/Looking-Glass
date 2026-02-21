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

#include "multiview-manage-dialog.hpp"
#include "multiview-edit-dialog.hpp"
#include "multiview-window.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QFormLayout>

ManageMultiviewsDialog::ManageMultiviewsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(LG_TEXT("ManageDialog.Title"));
	setMinimumSize(400, 350);

	auto *mainLayout = new QHBoxLayout(this);

	// List
	listWidget_ = new QListWidget();
	mainLayout->addWidget(listWidget_, 1);

	// Buttons
	auto *btnLayout = new QVBoxLayout();
	showBtn_ = new QPushButton(LG_TEXT("ManageDialog.ShowMultiview"));
	editBtn_ = new QPushButton(LG_TEXT("ManageDialog.EditMultiview"));
	renameBtn_ = new QPushButton(LG_TEXT("ManageDialog.RenameMultiview"));
	deleteBtn_ = new QPushButton(LG_TEXT("ManageDialog.DeleteMultiview"));
	duplicateBtn_ = new QPushButton(LG_TEXT("ManageDialog.DuplicateMultiview"));
	createTemplateBtn_ = new QPushButton(LG_TEXT("ManageDialog.CreateTemplate"));

	btnLayout->addWidget(showBtn_);
	btnLayout->addWidget(editBtn_);
	btnLayout->addWidget(renameBtn_);
	btnLayout->addWidget(deleteBtn_);
	btnLayout->addWidget(duplicateBtn_);
	btnLayout->addSpacing(10);
	btnLayout->addWidget(createTemplateBtn_);
	btnLayout->addStretch();
	mainLayout->addLayout(btnLayout);

	connect(showBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onShow);
	connect(editBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onEdit);
	connect(renameBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onRename);
	connect(deleteBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onDelete);
	connect(duplicateBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onDuplicate);
	connect(createTemplateBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onCreateTemplate);
	connect(listWidget_, &QListWidget::currentItemChanged, this, &ManageMultiviewsDialog::onSelectionChanged);

	ConfigManager *cm = GetConfigManager();
	connect(cm, &ConfigManager::multiviewAdded, this, &ManageMultiviewsDialog::refreshList);
	connect(cm, &ConfigManager::multiviewRemoved, this, &ManageMultiviewsDialog::refreshList);
	connect(cm, &ConfigManager::multiviewRenamed, this, &ManageMultiviewsDialog::refreshList);

	refreshList();
}

void ManageMultiviewsDialog::refreshList()
{
	QString current;
	if (listWidget_->currentItem())
		current = listWidget_->currentItem()->text();

	listWidget_->clear();
	QStringList names = GetConfigManager()->multiviewNames();
	names.sort(Qt::CaseInsensitive);
	listWidget_->addItems(names);

	if (!current.isEmpty()) {
		auto items = listWidget_->findItems(current, Qt::MatchExactly);
		if (!items.isEmpty())
			listWidget_->setCurrentItem(items.first());
	}
	onSelectionChanged();
}

void ManageMultiviewsDialog::onSelectionChanged()
{
	bool hasSelection = listWidget_->currentItem() != nullptr;
	showBtn_->setEnabled(hasSelection);
	editBtn_->setEnabled(hasSelection);
	renameBtn_->setEnabled(hasSelection);
	deleteBtn_->setEnabled(hasSelection);
	duplicateBtn_->setEnabled(hasSelection);
	createTemplateBtn_->setEnabled(hasSelection);
}

void ManageMultiviewsDialog::onShow()
{
	if (!listWidget_->currentItem())
		return;
	MultiviewWindow::openOrFocus(listWidget_->currentItem()->text());
}

void ManageMultiviewsDialog::onEdit()
{
	if (!listWidget_->currentItem())
		return;

	QString name = listWidget_->currentItem()->text();
	MultiviewConfig config = GetConfigManager()->getMultiview(name);
	MultiviewEditDialog dlg(config, false, this);
	if (dlg.exec() == QDialog::Accepted) {
		// Reload any open window for this multiview
		MultiviewWindow *win = MultiviewWindow::findByName(name);
		if (win)
			win->reloadConfig();
	}
}

void ManageMultiviewsDialog::onRename()
{
	if (!listWidget_->currentItem())
		return;

	QString oldName = listWidget_->currentItem()->text();
	bool ok;
	QString newName = QInputDialog::getText(this, LG_TEXT("ManageDialog.RenameMultiview"), LG_TEXT("ManageDialog.RenamePrompt"),
						QLineEdit::Normal, oldName, &ok);
	if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName)
		return;

	newName = newName.trimmed();
	if (GetConfigManager()->hasMultiview(newName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageDialog.MultiviewExists"));
		return;
	}

	GetConfigManager()->renameMultiview(oldName, newName);
}

void ManageMultiviewsDialog::onDelete()
{
	if (!listWidget_->currentItem())
		return;

	QString name = listWidget_->currentItem()->text();
	auto reply = QMessageBox::question(this, LG_TEXT("ManageDialog.DeleteMultiview"),
					   QString(LG_TEXT("ManageDialog.DeleteConfirm")).arg(name),
					   QMessageBox::Yes | QMessageBox::No);
	if (reply == QMessageBox::Yes) {
		MultiviewWindow::closeByName(name);
		GetConfigManager()->removeMultiview(name);
	}
}

void ManageMultiviewsDialog::onDuplicate()
{
	if (!listWidget_->currentItem())
		return;

	QString srcName = listWidget_->currentItem()->text();
	bool ok;
	QString newName =
		QInputDialog::getText(this, LG_TEXT("ManageDialog.DuplicateMultiview"), LG_TEXT("ManageDialog.DuplicatePrompt"),
				      QLineEdit::Normal, srcName + LG_TEXT("ManageDialog.DuplicateSuffix"), &ok);
	if (!ok || newName.trimmed().isEmpty())
		return;

	newName = newName.trimmed();
	if (GetConfigManager()->hasMultiview(newName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageDialog.MultiviewExists"));
		return;
	}

	GetConfigManager()->duplicateMultiview(srcName, newName);
}

void ManageMultiviewsDialog::onCreateTemplate()
{
	if (!listWidget_->currentItem())
		return;

	QString mvName = listWidget_->currentItem()->text();

	// Build a custom dialog with name field and preserve-sources checkbox
	QDialog dlg(this);
	dlg.setWindowTitle(LG_TEXT("ManageDialog.CreateTemplate"));
	auto *layout = new QFormLayout(&dlg);

	auto *nameEdit = new QLineEdit(mvName + LG_TEXT("ManageDialog.TemplateSuffix"));
	layout->addRow(LG_TEXT("ManageDialog.TemplateNamePrompt"), nameEdit);

	auto *preserveCheck = new QCheckBox(LG_TEXT("ManageDialog.PreserveSources"));
	preserveCheck->setToolTip(LG_TEXT("ManageDialog.PreserveSourcesTooltip"));
	layout->addRow(preserveCheck);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	layout->addRow(buttons);

	if (dlg.exec() != QDialog::Accepted)
		return;

	QString templateName = nameEdit->text().trimmed();
	if (templateName.isEmpty())
		return;

	if (GetConfigManager()->hasTemplate(templateName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageDialog.TemplateExists"));
		return;
	}

	bool preserveSources = preserveCheck->isChecked();

	MultiviewConfig mv = GetConfigManager()->getMultiview(mvName);
	TemplateConfig tmpl;
	tmpl.name = templateName;
	tmpl.gridRows = mv.gridRows;
	tmpl.gridCols = mv.gridCols;
	tmpl.preserveSources = preserveSources;

	for (const CellConfig &cell : mv.cells) {
		CellConfig tc = cell;
		if (!preserveSources && tc.widget.type != WidgetType::None) {
			// Convert non-structural widgets to placeholders
			QString origLabel;
			switch (tc.widget.type) {
			case WidgetType::Preview:
				origLabel = "Preview";
				break;
			case WidgetType::Program:
				origLabel = "Program";
				break;
			case WidgetType::Canvas:
				origLabel = "Canvas";
				break;
			case WidgetType::Scene:
				origLabel = tc.widget.sceneName;
				break;
			case WidgetType::Source:
				origLabel = tc.widget.sourceName;
				break;
			default:
				break;
			}
			tc.widget.type = WidgetType::Placeholder;
			tc.widget.sceneName.clear();
			tc.widget.sourceName.clear();
			if (!origLabel.isEmpty() && tc.widget.labelText.isEmpty())
				tc.widget.labelText = origLabel;
		}
		// When preserveSources is true, keep the cell exactly as-is
		tmpl.cells.append(tc);
	}

	GetConfigManager()->addTemplate(tmpl);
	QMessageBox::information(this, LG_TEXT("ManageDialog.TemplateCreated"),
				 QString(LG_TEXT("ManageDialog.TemplateCreatedMsg")).arg(templateName));
}
