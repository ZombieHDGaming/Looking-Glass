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

#include "multiview-window.hpp"
#include "multiview-edit-dialog.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"

#include <obs-module.h>

#include <QResizeEvent>
#include <QMoveEvent>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QMenu>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QTimer>

QMap<QString, MultiviewWindow *> MultiviewWindow::openWindows_;

MultiviewWindow::MultiviewWindow(const QString &name, QWidget *parent)
	: QWidget(parent, Qt::Window),
	  name_(name)
{
	setAttribute(Qt::WA_DeleteOnClose);

	config_ = GetConfigManager()->getMultiview(name);
	updateTitle();

	if (config_.geometry.isValid())
		setGeometry(config_.geometry);
	else
		resize(1280, 720);

	buildGrid();

	if (config_.fullscreen) {
		int idx = config_.monitorId;
		if (idx >= 0 && idx < QGuiApplication::screens().size())
			setFullscreenOnMonitor(idx);
	}

	openWindows_[name] = this;

	// Reload when config changes externally
	connect(GetConfigManager(), &ConfigManager::multiviewUpdated, this, [this](const QString &updatedName) {
		if (updatedName == name_ && !updatingConfig_)
			reloadConfig();
	});

	// Mark as open
	updatingConfig_ = true;
	config_.wasOpen = true;
	GetConfigManager()->updateMultiview(config_);
	updatingConfig_ = false;
}

MultiviewWindow::~MultiviewWindow()
{
	for (CellRenderer *r : renderers_)
		delete r;
	renderers_.clear();
	cellSurfaces_.clear();

	openWindows_.remove(name_);
}

void MultiviewWindow::setMultiviewName(const QString &name)
{
	openWindows_.remove(name_);
	name_ = name;
	openWindows_[name] = this;
	updateTitle();
}

void MultiviewWindow::reloadConfig()
{
	config_ = GetConfigManager()->getMultiview(name_);
	buildGrid();
	updateLayout();
}

void MultiviewWindow::openOrFocus(const QString &name)
{
	if (openWindows_.contains(name)) {
		MultiviewWindow *w = openWindows_[name];
		w->raise();
		w->activateWindow();
		return;
	}

	auto *w = new MultiviewWindow(name);
	w->show();
}

void MultiviewWindow::closeByName(const QString &name)
{
	if (openWindows_.contains(name)) {
		openWindows_[name]->close();
	}
}

void MultiviewWindow::closeAll()
{
	QList<MultiviewWindow *> windows = openWindows_.values();
	for (MultiviewWindow *w : windows)
		w->close();
}

void MultiviewWindow::reopenPreviouslyOpen()
{
	QStringList names = GetConfigManager()->multiviewNames();
	for (const QString &name : names) {
		MultiviewConfig mv = GetConfigManager()->getMultiview(name);
		if (mv.wasOpen)
			openOrFocus(name);
	}
}

MultiviewWindow *MultiviewWindow::findByName(const QString &name)
{
	return openWindows_.value(name, nullptr);
}

void MultiviewWindow::buildGrid()
{
	// Clean up existing renderers first (must destroy displays before surfaces)
	for (CellRenderer *r : renderers_)
		delete r;
	renderers_.clear();
	for (QWidget *s : cellSurfaces_)
		delete s;
	cellSurfaces_.clear();

	// Get path to the placeholder icon
	char *dataPath = obs_module_file("looking-glass.svg");
	placeholderSvgPath_ = dataPath ? QString::fromUtf8(dataPath) : QString();
	bfree(dataPath);

	// Create surfaces for each cell (labels and icons are rendered by CellRenderer)
	for (const CellConfig &cell : config_.cells) {
		(void)cell;
		auto *surface = new QWidget(this);
		surface->setAttribute(Qt::WA_NativeWindow);
		surface->setStyleSheet("background-color: transparent; border-radius: 6px;");
		cellSurfaces_.append(surface);

		// Reserve slot; actual renderer created in initRenderers() after surfaces are realized
		renderers_.append(nullptr);
	}

	updateLayout();

	// Show all surfaces so they get valid native window handles
	for (QWidget *s : cellSurfaces_)
		s->show();

	// Defer obs_display creation until native windows are realized
	QTimer::singleShot(50, this, &MultiviewWindow::initRenderers);
}

void MultiviewWindow::initRenderers()
{
	for (int i = 0; i < config_.cells.size() && i < cellSurfaces_.size(); i++) {
		if (renderers_[i]) {
			delete renderers_[i];
			renderers_[i] = nullptr;
		}

		auto *renderer = new CellRenderer();
		renderer->setPlaceholderSvgPath(placeholderSvgPath_);
		renderer->init(cellSurfaces_[i], config_.cells[i]);
		renderers_[i] = renderer;
	}
}

void MultiviewWindow::calculateGridMetrics(int &gridW, int &gridH, int &offsetX, int &offsetY,
					    float &cellW, float &cellH) const
{
	int totalW = width();
	int totalH = height();

	// Reserve 1 pixel on each edge for the border lines to be fully visible
	int availableW = totalW - 2;
	int availableH = totalH - 2;

	// Calculate cell size maintaining 16:9 aspect ratio for the grid
	float gridAspect = (float)(config_.gridCols * 16) / (float)(config_.gridRows * 9);
	float availableAspect = (float)availableW / (float)availableH;

	if (availableAspect > gridAspect) {
		gridH = availableH;
		gridW = (int)(gridH * gridAspect);
	} else {
		gridW = availableW;
		gridH = (int)(gridW / gridAspect);
	}

	// Calculate cell size first, then adjust grid size to be exact multiple
	// This ensures the grid edges align perfectly with cell boundaries
	cellW = (float)gridW / config_.gridCols;
	cellH = (float)gridH / config_.gridRows;

	// Round cell sizes down and recalculate grid size to be exact
	int intCellW = (int)cellW;
	int intCellH = (int)cellH;
	gridW = intCellW * config_.gridCols;
	gridH = intCellH * config_.gridRows;

	// Update cell sizes to exact integer values
	cellW = (float)intCellW;
	cellH = (float)intCellH;

	// Center the grid in the window (with the reserved margin)
	offsetX = (totalW - gridW) / 2;
	offsetY = (totalH - gridH) / 2;
}

void MultiviewWindow::updateLayout()
{
	if (config_.gridRows <= 0 || config_.gridCols <= 0)
		return;

	int gridW, gridH, offsetX, offsetY;
	float cellW, cellH;
	calculateGridMetrics(gridW, gridH, offsetX, offsetY, cellW, cellH);

	// Cache metrics for paintEvent
	gridWidth_ = gridW;
	gridHeight_ = gridH;
	gridOffsetX_ = offsetX;
	gridOffsetY_ = offsetY;
	cellWidth_ = cellW;
	cellHeight_ = cellH;

	// Border inside each cell (1 pixel on each side)
	static const int kBorder = 1;

	// Use integer cell dimensions for consistent positioning
	int intCellW = (int)cellW;
	int intCellH = (int)cellH;

	for (int i = 0; i < config_.cells.size() && i < cellSurfaces_.size(); i++) {
		const CellConfig &cell = config_.cells[i];
		int x = offsetX + cell.col * intCellW + kBorder;
		int y = offsetY + cell.row * intCellH + kBorder;
		int w = cell.colSpan * intCellW - 2 * kBorder;
		int h = cell.rowSpan * intCellH - 2 * kBorder;
		if (w < 1)
			w = 1;
		if (h < 1)
			h = 1;

		cellSurfaces_[i]->setGeometry(x, y, w, h);
		if (i < renderers_.size() && renderers_[i])
			renderers_[i]->resize(w, h);
	}

	// Trigger repaint for grid borders
	update();
}

void MultiviewWindow::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	updateLayout();
	saveWindowState();
}

void MultiviewWindow::moveEvent(QMoveEvent *event)
{
	QWidget::moveEvent(event);
	saveWindowState();
}

void MultiviewWindow::closeEvent(QCloseEvent *event)
{
	updatingConfig_ = true;
	config_.wasOpen = false;
	GetConfigManager()->updateMultiview(config_);
	updatingConfig_ = false;
	QWidget::closeEvent(event);
}

void MultiviewWindow::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::WindowStateChange) {
		bool nowFullscreen = windowState() & Qt::WindowFullScreen;
		if (nowFullscreen != fullscreen_) {
			fullscreen_ = nowFullscreen;
			updateTitle();
			saveWindowState();
		}
	}
}

void MultiviewWindow::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu menu(this);

	// Fullscreen options per monitor
	QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); i++) {
		QScreen *screen = screens[i];
		QString label = QString(LG_TEXT("ToolsMenu.FullscreenOn")).arg(screen->name());
		menu.addAction(label, this, [this, i]() { setFullscreenOnMonitor(i); });
	}

	if (fullscreen_) {
		menu.addSeparator();
		menu.addAction(LG_TEXT("ToolsMenu.Windowed"), this, &MultiviewWindow::setWindowed);
	}

	menu.addSeparator();
	menu.addAction(LG_TEXT("WindowMenu.EditMultiview"), this, &MultiviewWindow::openEditDialog);

	menu.addSeparator();
	menu.addAction(LG_TEXT("WindowMenu.CloseMultiview"), this, &QWidget::close);

	menu.exec(event->globalPos());
}

void MultiviewWindow::saveWindowState()
{
	if (!fullscreen_)
		config_.geometry = geometry();
	config_.fullscreen = fullscreen_;

	// Find current monitor
	QScreen *screen = QGuiApplication::screenAt(geometry().center());
	if (screen) {
		int idx = QGuiApplication::screens().indexOf(screen);
		config_.monitorId = idx;
	}

	updatingConfig_ = true;
	GetConfigManager()->updateMultiview(config_);
	updatingConfig_ = false;
}

void MultiviewWindow::setFullscreenOnMonitor(int screenIndex)
{
	QList<QScreen *> screens = QGuiApplication::screens();
	if (screenIndex < 0 || screenIndex >= screens.size())
		return;

	if (!fullscreen_)
		config_.geometry = geometry(); // Save windowed geometry before going fullscreen

	QScreen *screen = screens[screenIndex];
	setScreen(screen);
	setGeometry(screen->geometry());
	showFullScreen();

	fullscreen_ = true;
	config_.fullscreen = true;
	config_.monitorId = screenIndex;
	updateTitle();
	saveWindowState();
}

void MultiviewWindow::setWindowed()
{
	showNormal();
	fullscreen_ = false;
	config_.fullscreen = false;

	if (config_.geometry.isValid())
		setGeometry(config_.geometry);

	updateTitle();
	saveWindowState();
}

void MultiviewWindow::openEditDialog()
{
	MultiviewEditDialog dlg(config_, false, this);
	if (dlg.exec() == QDialog::Accepted) {
		reloadConfig();
	}
}

void MultiviewWindow::updateTitle()
{
	QString title = name_;
	if (fullscreen_)
		title += LG_TEXT("WindowMenu.FullscreenSuffix");
	setWindowTitle(title);
}

void MultiviewWindow::paintEvent(QPaintEvent *event)
{
	QWidget::paintEvent(event);

	if (config_.gridRows <= 0 || config_.gridCols <= 0)
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);

	// Fill background with black
	painter.fillRect(rect(), Qt::black);

	// Build ownership map: which cell index owns each grid position
	// -1 means no cell owns it (shouldn't happen in valid config)
	QVector<QVector<int>> ownership(config_.gridRows, QVector<int>(config_.gridCols, -1));
	for (int i = 0; i < config_.cells.size(); i++) {
		const CellConfig &cell = config_.cells[i];
		for (int r = cell.row; r < cell.row + cell.rowSpan && r < config_.gridRows; r++) {
			for (int c = cell.col; c < cell.col + cell.colSpan && c < config_.gridCols; c++) {
				ownership[r][c] = i;
			}
		}
	}

	QPen gridPen(Qt::white);
	gridPen.setWidth(1);
	painter.setPen(gridPen);

	int intCellW = (int)cellWidth_;
	int intCellH = (int)cellHeight_;

	// Draw vertical lines - only where there's a cell boundary
	// A vertical line at column 'col' should be drawn between row positions
	// only if the cells on either side are different
	for (int col = 0; col <= config_.gridCols; col++) {
		int x = gridOffsetX_ + col * intCellW;

		// For edge lines (col 0 and col == gridCols), always draw full line
		if (col == 0 || col == config_.gridCols) {
			painter.drawLine(x, gridOffsetY_, x, gridOffsetY_ + gridHeight_);
			continue;
		}

		// For interior lines, only draw segments where cells differ
		int segmentStart = -1;
		for (int row = 0; row < config_.gridRows; row++) {
			int leftCell = ownership[row][col - 1];
			int rightCell = ownership[row][col];

			bool needLine = (leftCell != rightCell);

			if (needLine && segmentStart < 0) {
				// Start a new segment
				segmentStart = row;
			} else if (!needLine && segmentStart >= 0) {
				// End the segment
				int y1 = gridOffsetY_ + segmentStart * intCellH;
				int y2 = gridOffsetY_ + row * intCellH;
				painter.drawLine(x, y1, x, y2);
				segmentStart = -1;
			}
		}
		// Close any open segment
		if (segmentStart >= 0) {
			int y1 = gridOffsetY_ + segmentStart * intCellH;
			int y2 = gridOffsetY_ + gridHeight_;
			painter.drawLine(x, y1, x, y2);
		}
	}

	// Draw horizontal lines - only where there's a cell boundary
	for (int row = 0; row <= config_.gridRows; row++) {
		int y = gridOffsetY_ + row * intCellH;

		// For edge lines (row 0 and row == gridRows), always draw full line
		if (row == 0 || row == config_.gridRows) {
			painter.drawLine(gridOffsetX_, y, gridOffsetX_ + gridWidth_, y);
			continue;
		}

		// For interior lines, only draw segments where cells differ
		int segmentStart = -1;
		for (int col = 0; col < config_.gridCols; col++) {
			int topCell = ownership[row - 1][col];
			int bottomCell = ownership[row][col];

			bool needLine = (topCell != bottomCell);

			if (needLine && segmentStart < 0) {
				// Start a new segment
				segmentStart = col;
			} else if (!needLine && segmentStart >= 0) {
				// End the segment
				int x1 = gridOffsetX_ + segmentStart * intCellW;
				int x2 = gridOffsetX_ + col * intCellW;
				painter.drawLine(x1, y, x2, y);
				segmentStart = -1;
			}
		}
		// Close any open segment
		if (segmentStart >= 0) {
			int x1 = gridOffsetX_ + segmentStart * intCellW;
			int x2 = gridOffsetX_ + gridWidth_;
			painter.drawLine(x1, y, x2, y);
		}
	}
}
