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

#include "../core/multiview-config.hpp"

#include <obs.h>
#include <graphics/graphics.h>

#include <QString>
#include <QWidget>

/**
 * Renders a single multiview cell using an OBS display.
 * Each cell gets its own obs_display_t backed by a native window surface,
 * and renders the configured content (preview, program, canvas, scene, or source)
 * with aspect-ratio-preserving scaling. Labels and placeholder icons are rendered
 * as OBS graphics overlays composited on top of the cell content.
 */
class CellRenderer {
public:
	CellRenderer();
	~CellRenderer();

	void init(QWidget *surface, const CellConfig &config);
	void cleanup();
	void updateConfig(const CellConfig &config);
	void resize(uint32_t width, uint32_t height);

	// Set the SVG file path for placeholder icon rendering
	void setPlaceholderSvgPath(const QString &path);

private:
	static void DrawCallback(void *data, uint32_t cx, uint32_t cy);
	void render(uint32_t cx, uint32_t cy);
	void renderPreviewProgram(uint32_t cx, uint32_t cy, bool isProgram);
	void renderCanvas(uint32_t cx, uint32_t cy);
	void renderSource(obs_source_t *source, uint32_t cx, uint32_t cy);
	void renderLabel(uint32_t cx, uint32_t cy);
	void renderPlaceholderIcon(uint32_t cx, uint32_t cy);

	void createLabelSource();
	void destroyLabelSource();
	void updateLabelSource();
	QString resolveLabelText() const;

	void createPlaceholderTexture();
	void destroyPlaceholderTexture();
	void destroyLabelBgTexture();

	obs_display_t *display_ = nullptr;
	obs_source_t *labelSource_ = nullptr;
	gs_texture_t *placeholderTexture_ = nullptr;
	int placeholderTexSize_ = 0;
	gs_texture_t *labelBgTexture_ = nullptr;
	int labelBgTexW_ = 0;
	int labelBgTexH_ = 0;
	QColor labelBgTexColor_;
	QString placeholderSvgPath_;
	CellConfig config_;
	QWidget *surface_ = nullptr;
};
