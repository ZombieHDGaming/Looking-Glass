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

#include <QString>
#include <QVector>
#include <QRect>
#include <QColor>
#include <Qt>

#include <obs-data.h>

// Content types that can be displayed in a multiview cell
enum class WidgetType {
	None,
	Preview,
	Program,
	Canvas,
	Scene,
	Source,
	Placeholder,
};

// Per-cell display and label configuration
struct WidgetConfig {
	WidgetType type = WidgetType::None;
	QString sceneName;
	QString sourceName;
	QString placeholderPath;
	QString canvasName; // Empty means main canvas
	bool labelVisible = true;
	Qt::Alignment labelHAlign = Qt::AlignHCenter;
	Qt::Alignment labelVAlign = Qt::AlignBottom;
	QString labelText;
	QString labelFont;    // QFont::toString() format
	QColor labelBgColor = QColor(0, 0, 0, 128); // Label background color with alpha
};

// Position and span of a single cell within the grid
struct CellConfig {
	int row = 0;
	int col = 0;
	int rowSpan = 1;
	int colSpan = 1;
	WidgetConfig widget;
};

// Complete layout definition for a multiview window
struct MultiviewConfig {
	QString name;
	int gridRows = 4;
	int gridCols = 4;
	QVector<CellConfig> cells;
	QRect geometry = QRect(100, 100, 1280, 720);
	int monitorId = -1;
	bool fullscreen = false;
	bool wasOpen = false;
};

// Reusable layout template (no window state)
// When preserveSources is true, the template retains exact widget types and
// source/scene names. When false, non-structural widgets are reset to placeholders.
struct TemplateConfig {
	QString name;
	int gridRows = 4;
	int gridCols = 4;
	QVector<CellConfig> cells;
	bool preserveSources = false;
};

// Serialization helpers for persisting configs to OBS JSON data objects
namespace MultiviewSerializer {

obs_data_t *WidgetToData(const WidgetConfig &w);
WidgetConfig WidgetFromData(obs_data_t *data);

obs_data_t *CellToData(const CellConfig &c);
CellConfig CellFromData(obs_data_t *data);

obs_data_t *MultiviewToData(const MultiviewConfig &mv);
MultiviewConfig MultiviewFromData(obs_data_t *data);

obs_data_t *TemplateToData(const TemplateConfig &t);
TemplateConfig TemplateFromData(obs_data_t *data);

} // namespace MultiviewSerializer
