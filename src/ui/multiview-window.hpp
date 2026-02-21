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

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QString>

#include "../core/multiview-config.hpp"
#include "../render/multiview-renderer.hpp"

/**
 * Top-level window that displays a multiview grid layout.
 * Manages cell surfaces and renderers. Labels and placeholder icons
 * are rendered by CellRenderer within the OBS graphics pipeline.
 * Supports windowed and per-monitor fullscreen modes with state persistence.
 */
class MultiviewWindow : public QWidget {
	Q_OBJECT

public:
	explicit MultiviewWindow(const QString &name, QWidget *parent = nullptr);
	~MultiviewWindow();

	QString multiviewName() const { return name_; }
	void setMultiviewName(const QString &name);
	void reloadConfig();

	// Window mode controls
	void setFullscreenOnMonitor(int screenIndex);
	void setWindowed();

	// Static window management
	static void openOrFocus(const QString &name);
	static void closeByName(const QString &name);
	static void closeAll();
	static void reopenPreviouslyOpen();
	static MultiviewWindow *findByName(const QString &name);

protected:
	void resizeEvent(QResizeEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void changeEvent(QEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

private:
	void buildGrid();
	void initRenderers();
	void updateLayout();
	void saveWindowState();
	void openEditDialog();
	void updateTitle();
	void calculateGridMetrics(int &gridW, int &gridH, int &offsetX, int &offsetY,
				  float &cellW, float &cellH) const;

	QString name_;
	MultiviewConfig config_;
	QVector<QWidget *> cellSurfaces_;
	QVector<CellRenderer *> renderers_;
	QString placeholderSvgPath_;
	bool fullscreen_ = false;
	bool updatingConfig_ = false;

	// Cached grid metrics for painting
	int gridOffsetX_ = 0;
	int gridOffsetY_ = 0;
	int gridWidth_ = 0;
	int gridHeight_ = 0;
	float cellWidth_ = 0;
	float cellHeight_ = 0;

	static QMap<QString, MultiviewWindow *> openWindows_;
};
