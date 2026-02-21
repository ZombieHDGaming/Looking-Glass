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

#include "multiview-config.hpp"

// --- String conversion helpers for enum serialization ---

static const char *WidgetTypeToString(WidgetType t)
{
	switch (t) {
	case WidgetType::Preview:
		return "preview";
	case WidgetType::Program:
		return "program";
	case WidgetType::Canvas:
		return "canvas";
	case WidgetType::Scene:
		return "scene";
	case WidgetType::Source:
		return "source";
	case WidgetType::Placeholder:
		return "placeholder";
	default:
		return "none";
	}
}

static WidgetType StringToWidgetType(const char *s)
{
	if (!s)
		return WidgetType::None;
	if (strcmp(s, "preview") == 0)
		return WidgetType::Preview;
	if (strcmp(s, "program") == 0)
		return WidgetType::Program;
	if (strcmp(s, "canvas") == 0)
		return WidgetType::Canvas;
	if (strcmp(s, "scene") == 0)
		return WidgetType::Scene;
	if (strcmp(s, "source") == 0)
		return WidgetType::Source;
	if (strcmp(s, "placeholder") == 0)
		return WidgetType::Placeholder;
	return WidgetType::None;
}

static const char *AlignHToString(Qt::Alignment a)
{
	if (a & Qt::AlignLeft)
		return "left";
	if (a & Qt::AlignRight)
		return "right";
	return "center";
}

static Qt::Alignment StringToAlignH(const char *s)
{
	if (!s)
		return Qt::AlignHCenter;
	if (strcmp(s, "left") == 0)
		return Qt::AlignLeft;
	if (strcmp(s, "right") == 0)
		return Qt::AlignRight;
	return Qt::AlignHCenter;
}

static const char *AlignVToString(Qt::Alignment a)
{
	if (a & Qt::AlignTop)
		return "top";
	if (a & Qt::AlignBottom)
		return "bottom";
	return "middle";
}

static Qt::Alignment StringToAlignV(const char *s)
{
	if (!s)
		return Qt::AlignTop;
	if (strcmp(s, "top") == 0)
		return Qt::AlignTop;
	if (strcmp(s, "bottom") == 0)
		return Qt::AlignBottom;
	return Qt::AlignVCenter;
}

namespace MultiviewSerializer {

obs_data_t *WidgetToData(const WidgetConfig &w)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", WidgetTypeToString(w.type));
	obs_data_set_string(data, "scene_name", w.sceneName.toUtf8().constData());
	obs_data_set_string(data, "source_name", w.sourceName.toUtf8().constData());
	obs_data_set_string(data, "placeholder_path", w.placeholderPath.toUtf8().constData());
	obs_data_set_string(data, "canvas_name", w.canvasName.toUtf8().constData());
	obs_data_set_bool(data, "label_visible", w.labelVisible);
	obs_data_set_string(data, "label_h_align", AlignHToString(w.labelHAlign));
	obs_data_set_string(data, "label_v_align", AlignVToString(w.labelVAlign));
	obs_data_set_string(data, "label_text", w.labelText.toUtf8().constData());
	obs_data_set_string(data, "label_font", w.labelFont.toUtf8().constData());
	obs_data_set_string(data, "label_bg_color", w.labelBgColor.name(QColor::HexArgb).toUtf8().constData());
	return data;
}

WidgetConfig WidgetFromData(obs_data_t *data)
{
	WidgetConfig w;
	w.type = StringToWidgetType(obs_data_get_string(data, "type"));
	w.sceneName = QString::fromUtf8(obs_data_get_string(data, "scene_name"));
	w.sourceName = QString::fromUtf8(obs_data_get_string(data, "source_name"));
	w.placeholderPath = QString::fromUtf8(obs_data_get_string(data, "placeholder_path"));
	w.canvasName = QString::fromUtf8(obs_data_get_string(data, "canvas_name"));
	w.labelVisible = obs_data_get_bool(data, "label_visible");
	w.labelHAlign = StringToAlignH(obs_data_get_string(data, "label_h_align"));
	w.labelVAlign = StringToAlignV(obs_data_get_string(data, "label_v_align"));
	w.labelText = QString::fromUtf8(obs_data_get_string(data, "label_text"));
	w.labelFont = QString::fromUtf8(obs_data_get_string(data, "label_font"));

	QString bgColorStr = QString::fromUtf8(obs_data_get_string(data, "label_bg_color"));
	if (!bgColorStr.isEmpty())
		w.labelBgColor = QColor(bgColorStr);
	else
		w.labelBgColor = QColor(0, 0, 0, 128);

	return w;
}

obs_data_t *CellToData(const CellConfig &c)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "row", c.row);
	obs_data_set_int(data, "col", c.col);
	obs_data_set_int(data, "row_span", c.rowSpan);
	obs_data_set_int(data, "col_span", c.colSpan);

	obs_data_t *widgetData = WidgetToData(c.widget);
	obs_data_set_obj(data, "widget", widgetData);
	obs_data_release(widgetData);

	return data;
}

CellConfig CellFromData(obs_data_t *data)
{
	CellConfig c;
	c.row = (int)obs_data_get_int(data, "row");
	c.col = (int)obs_data_get_int(data, "col");
	c.rowSpan = (int)obs_data_get_int(data, "row_span");
	c.colSpan = (int)obs_data_get_int(data, "col_span");
	if (c.rowSpan <= 0)
		c.rowSpan = 1;
	if (c.colSpan <= 0)
		c.colSpan = 1;

	obs_data_t *widgetData = obs_data_get_obj(data, "widget");
	if (widgetData) {
		c.widget = WidgetFromData(widgetData);
		obs_data_release(widgetData);
	}
	return c;
}

obs_data_t *MultiviewToData(const MultiviewConfig &mv)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "name", mv.name.toUtf8().constData());
	obs_data_set_int(data, "grid_rows", mv.gridRows);
	obs_data_set_int(data, "grid_cols", mv.gridCols);
	obs_data_set_int(data, "grid_border_width", mv.gridBorderWidth);
	obs_data_set_string(data, "grid_line_color", mv.gridLineColor.name(QColor::HexArgb).toUtf8().constData());
	obs_data_set_int(data, "geometry_x", mv.geometry.x());
	obs_data_set_int(data, "geometry_y", mv.geometry.y());
	obs_data_set_int(data, "geometry_w", mv.geometry.width());
	obs_data_set_int(data, "geometry_h", mv.geometry.height());
	obs_data_set_int(data, "monitor_id", mv.monitorId);
	obs_data_set_bool(data, "fullscreen", mv.fullscreen);
	obs_data_set_bool(data, "was_open", mv.wasOpen);

	obs_data_array_t *cellsArray = obs_data_array_create();
	for (const auto &cell : mv.cells) {
		obs_data_t *cellData = CellToData(cell);
		obs_data_array_push_back(cellsArray, cellData);
		obs_data_release(cellData);
	}
	obs_data_set_array(data, "cells", cellsArray);
	obs_data_array_release(cellsArray);

	return data;
}

MultiviewConfig MultiviewFromData(obs_data_t *data)
{
	MultiviewConfig mv;
	mv.name = QString::fromUtf8(obs_data_get_string(data, "name"));
	mv.gridRows = (int)obs_data_get_int(data, "grid_rows");
	mv.gridCols = (int)obs_data_get_int(data, "grid_cols");
	mv.gridBorderWidth = (int)obs_data_get_int(data, "grid_border_width");
	if (mv.gridRows <= 0)
		mv.gridRows = 4;
	if (mv.gridCols <= 0)
		mv.gridCols = 4;
	if (mv.gridBorderWidth <= 0)
		mv.gridBorderWidth = 1;
	if (mv.gridBorderWidth > 10)
		mv.gridBorderWidth = 10;

	QString lineColorStr = QString::fromUtf8(obs_data_get_string(data, "grid_line_color"));
	if (!lineColorStr.isEmpty())
		mv.gridLineColor = QColor(lineColorStr);
	else
		mv.gridLineColor = QColor(255, 255, 255);

	int gx = (int)obs_data_get_int(data, "geometry_x");
	int gy = (int)obs_data_get_int(data, "geometry_y");
	int gw = (int)obs_data_get_int(data, "geometry_w");
	int gh = (int)obs_data_get_int(data, "geometry_h");
	if (gw <= 0)
		gw = 1280;
	if (gh <= 0)
		gh = 720;
	mv.geometry = QRect(gx, gy, gw, gh);
	mv.monitorId = (int)obs_data_get_int(data, "monitor_id");
	mv.fullscreen = obs_data_get_bool(data, "fullscreen");
	mv.wasOpen = obs_data_get_bool(data, "was_open");

	obs_data_array_t *cellsArray = obs_data_get_array(data, "cells");
	if (cellsArray) {
		size_t count = obs_data_array_count(cellsArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *cellData = obs_data_array_item(cellsArray, i);
			mv.cells.append(CellFromData(cellData));
			obs_data_release(cellData);
		}
		obs_data_array_release(cellsArray);
	}

	return mv;
}

obs_data_t *TemplateToData(const TemplateConfig &t)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "name", t.name.toUtf8().constData());
	obs_data_set_int(data, "grid_rows", t.gridRows);
	obs_data_set_int(data, "grid_cols", t.gridCols);
	obs_data_set_bool(data, "preserve_sources", t.preserveSources);

	obs_data_array_t *cellsArray = obs_data_array_create();
	for (const auto &cell : t.cells) {
		obs_data_t *cellData = CellToData(cell);
		obs_data_array_push_back(cellsArray, cellData);
		obs_data_release(cellData);
	}
	obs_data_set_array(data, "cells", cellsArray);
	obs_data_array_release(cellsArray);

	return data;
}

TemplateConfig TemplateFromData(obs_data_t *data)
{
	TemplateConfig t;
	t.name = QString::fromUtf8(obs_data_get_string(data, "name"));
	t.gridRows = (int)obs_data_get_int(data, "grid_rows");
	t.gridCols = (int)obs_data_get_int(data, "grid_cols");
	t.preserveSources = obs_data_get_bool(data, "preserve_sources");
	if (t.gridRows <= 0)
		t.gridRows = 4;
	if (t.gridCols <= 0)
		t.gridCols = 4;

	obs_data_array_t *cellsArray = obs_data_get_array(data, "cells");
	if (cellsArray) {
		size_t count = obs_data_array_count(cellsArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *cellData = obs_data_array_item(cellsArray, i);
			t.cells.append(CellFromData(cellData));
			obs_data_release(cellData);
		}
		obs_data_array_release(cellsArray);
	}

	return t;
}

} // namespace MultiviewSerializer
