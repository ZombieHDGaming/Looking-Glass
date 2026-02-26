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

#include "tools-menu.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "multiview-edit-dialog.hpp"
#include "multiview-manage-dialog.hpp"
#include "template-manage-dialog.hpp"
#include "multiview-window.hpp"

#include <obs-frontend-api.h>

#include <QMainWindow>
#include <QMenuBar>
#include <QScreen>
#include <QGuiApplication>

ToolsMenuManager::ToolsMenuManager(QObject *parent) : QObject(parent) {}

ToolsMenuManager::~ToolsMenuManager()
{
	if (submenu_)
		delete submenu_;
}

void ToolsMenuManager::initialize()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	if (!mainWindow)
		return;

	// Find the Tools menu
	QMenu *toolsMenu = nullptr;
	for (QAction *action : mainWindow->menuBar()->actions()) {
		if (action->menu() && action->text().contains("Tools", Qt::CaseInsensitive)) {
			toolsMenu = action->menu();
			break;
		}
	}
	if (!toolsMenu)
		return;

	submenu_ = new QMenu(LG_TEXT("LookingGlass"), toolsMenu);
	toolsMenu->addMenu(submenu_);

	rebuildMenu();

	// Connect to ConfigManager signals
	ConfigManager *cm = GetConfigManager();
	connect(cm, &ConfigManager::multiviewAdded, this, &ToolsMenuManager::rebuildMenu);
	connect(cm, &ConfigManager::multiviewRemoved, this, &ToolsMenuManager::rebuildMenu);
	connect(cm, &ConfigManager::multiviewRenamed, this, &ToolsMenuManager::rebuildMenu);
	connect(cm, &ConfigManager::multiviewsReloaded, this, &ToolsMenuManager::rebuildMenu);
}

void ToolsMenuManager::rebuildMenu()
{
	if (!submenu_)
		return;

	submenu_->clear();
	for (QMenu *menu : dynamicSubmenus_)
		delete menu;
	dynamicSubmenus_.clear();

	QAction *createAction = submenu_->addAction(LG_TEXT("ToolsMenu.CreateNewMultiview"));
	connect(createAction, &QAction::triggered, this, &ToolsMenuManager::onCreateNew);

	QAction *manageAction = submenu_->addAction(LG_TEXT("ToolsMenu.ManageMultiviews"));
	connect(manageAction, &QAction::triggered, this, &ToolsMenuManager::onManage);

	QAction *manageTemplatesAction = submenu_->addAction(LG_TEXT("ToolsMenu.ManageTemplates"));
	connect(manageTemplatesAction, &QAction::triggered, this, &ToolsMenuManager::onManageTemplates);

	QStringList names = GetConfigManager()->multiviewNames();
	if (!names.isEmpty()) {
		submenu_->addSeparator();
		names.sort(Qt::CaseInsensitive);

		QList<QScreen *> screens = QGuiApplication::screens();

		for (const QString &name : names) {
			QMenu *mvMenu = new QMenu(name, submenu_);
			dynamicSubmenus_.append(mvMenu);

			// Open action
			QAction *openAction = mvMenu->addAction(LG_TEXT("ToolsMenu.Open"));
			connect(openAction, &QAction::triggered, this, [this, name]() { onOpenMultiview(name); });

			// Edit action
			QAction *editAction = mvMenu->addAction(LG_TEXT("ToolsMenu.Edit"));
			connect(editAction, &QAction::triggered, this, [this, name]() { onEditMultiview(name); });

			// Send to main display action
			QAction *sendToMainAction = mvMenu->addAction(LG_TEXT("ToolsMenu.SendToMainDisplay"));
			connect(sendToMainAction, &QAction::triggered, this,
				[this, name]() { onSendToMainDisplay(name); });

			mvMenu->addSeparator();

			// Fullscreen options for each monitor
			for (int i = 0; i < screens.size(); i++) {
				QScreen *screen = screens[i];
				QString label = QString(LG_TEXT("ToolsMenu.FullscreenOn")).arg(screen->name());
				QAction *fsAction = mvMenu->addAction(label);
				connect(fsAction, &QAction::triggered, this,
					[this, name, i]() { onSetFullscreen(name, i); });
			}

			// Windowed option
			mvMenu->addSeparator();
			QAction *windowedAction = mvMenu->addAction(LG_TEXT("ToolsMenu.Windowed"));
			connect(windowedAction, &QAction::triggered, this, [this, name]() { onSetWindowed(name); });

			submenu_->addMenu(mvMenu);
		}
	}
}

void ToolsMenuManager::onCreateNew()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	MultiviewEditDialog dlg(MultiviewConfig(), true, mainWindow);
	dlg.exec();
}

void ToolsMenuManager::onManage()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	ManageMultiviewsDialog dlg(mainWindow);
	dlg.exec();
}

void ToolsMenuManager::onManageTemplates()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	ManageTemplatesDialog dlg(mainWindow);
	dlg.exec();
}

void ToolsMenuManager::onOpenMultiview(const QString &name)
{
	MultiviewWindow::openOrFocus(name);
}

void ToolsMenuManager::onEditMultiview(const QString &name)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	MultiviewConfig config = GetConfigManager()->getMultiview(name);
	MultiviewEditDialog dlg(config, false, mainWindow);
	if (dlg.exec() == QDialog::Accepted) {
		// Reload any open window for this multiview
		MultiviewWindow *win = MultiviewWindow::findByName(name);
		if (win)
			win->reloadConfig();
	}
}

void ToolsMenuManager::onSendToMainDisplay(const QString &name)
{
	// Open the window if not already open
	MultiviewWindow::openOrFocus(name);
	MultiviewWindow *win = MultiviewWindow::findByName(name);
	if (win) {
		// Set to windowed mode first
		win->setWindowed();

		// Get primary screen and center the 1280x720 window on it
		QScreen *primaryScreen = QGuiApplication::primaryScreen();
		if (primaryScreen) {
			QRect screenGeom = primaryScreen->geometry();
			int x = screenGeom.x() + (screenGeom.width() - 1280) / 2;
			int y = screenGeom.y() + (screenGeom.height() - 720) / 2;
			win->setGeometry(x, y, 1280, 720);
		} else {
			// Fallback if no primary screen found
			win->resize(1280, 720);
		}
	}
}

void ToolsMenuManager::onSetFullscreen(const QString &name, int screenIndex)
{
	// Open the window if not already open, then set fullscreen
	MultiviewWindow::openOrFocus(name);
	MultiviewWindow *win = MultiviewWindow::findByName(name);
	if (win)
		win->setFullscreenOnMonitor(screenIndex);
}

void ToolsMenuManager::onSetWindowed(const QString &name)
{
	// Open the window if not already open, then set windowed
	MultiviewWindow::openOrFocus(name);
	MultiviewWindow *win = MultiviewWindow::findByName(name);
	if (win)
		win->setWindowed();
}
