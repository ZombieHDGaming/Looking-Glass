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

#include "multiview-renderer.hpp"
#include "../plugin.hpp"

#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <graphics/vec4.h>

#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include <cmath>

#ifdef _WIN32
#include <Windows.h>
#endif

// Calculates scale factor and centered position to fit baseCX x baseCY
// content inside a windowCX x windowCY area while preserving aspect ratio.
static void GetScaleAndCenterPos(int baseCX, int baseCY, int windowCX, int windowCY, int &x, int &y, float &scale,
				 int &scaledCX, int &scaledCY)
{
	double windowAspect = (double)windowCX / (double)windowCY;
	double baseAspect = (double)baseCX / (double)baseCY;

	if (windowAspect > baseAspect) {
		// Height-constrained: scale to fit height, center horizontally
		scale = (float)windowCY / (float)baseCY;
		scaledCY = windowCY;
		scaledCX = (int)std::round((double)baseCX * scale);
		// Clamp to window width in case of rounding up
		if (scaledCX > windowCX)
			scaledCX = windowCX;
	} else {
		// Width-constrained: scale to fit width, center vertically
		scale = (float)windowCX / (float)baseCX;
		scaledCX = windowCX;
		scaledCY = (int)std::round((double)baseCY * scale);
		// Clamp to window height in case of rounding up
		if (scaledCY > windowCY)
			scaledCY = windowCY;
	}

	// Center the scaled content within the window area
	int remainderX = windowCX - scaledCX;
	int remainderY = windowCY - scaledCY;
	x = remainderX / 2;
	y = remainderY / 2;
}

CellRenderer::CellRenderer() {}

CellRenderer::~CellRenderer()
{
	cleanup();
}

void CellRenderer::init(QWidget *surface, const CellConfig &config)
{
	cleanup();

	surface_ = surface;
	config_ = config;

	if (!surface_)
		return;

	// Skip display creation for None widgets to avoid unnecessary
	// swap chain overhead. Each obs_display_t adds rendering cost
	// even when the draw callback exits early.
	if (config.widget.type == WidgetType::None)
		return;

	gs_init_data initData = {};
	initData.cx = surface_->width();
	initData.cy = surface_->height();
	initData.format = GS_BGRA;

#ifdef _WIN32
	initData.window.hwnd = (HWND)surface_->winId();
#elif defined(__APPLE__)
	initData.window.view = (id)surface_->winId();
#else
	initData.window.id = surface_->winId();
	initData.window.display = nullptr; // X11/Wayland
#endif

	display_ = obs_display_create(&initData, 0);
	if (display_) {
		obs_display_add_draw_callback(display_, DrawCallback, this);
		obs_display_set_background_color(display_, 0x000000);
	}

	createLabelSource();
}

void CellRenderer::cleanup()
{
	if (display_) {
		obs_display_remove_draw_callback(display_, DrawCallback, this);
		obs_display_destroy(display_);
		display_ = nullptr;
	}
	destroyLabelSource();
	destroyLabelBgTexture();
	destroyPlaceholderTexture();
	destroySafeAreaGeometry();
	surface_ = nullptr;
}

void CellRenderer::updateConfig(const CellConfig &config)
{
	config_ = config;
	updateLabelSource();
}

void CellRenderer::resize(uint32_t width, uint32_t height)
{
	if (display_)
		obs_display_resize(display_, width, height);
}

void CellRenderer::DrawCallback(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = (CellRenderer *)data;
	self->render(cx, cy);
}

void CellRenderer::render(uint32_t cx, uint32_t cy)
{
	if (cx == 0 || cy == 0)
		return;

	obs_source_t *source = nullptr;

	switch (config_.widget.type) {
	case WidgetType::Preview:
		renderPreviewProgram(cx, cy, false);
		renderLabel(cx, cy);
		renderStatusBorder(cx, cy);
		return;
	case WidgetType::Program:
		renderPreviewProgram(cx, cy, true);
		renderLabel(cx, cy);
		renderStatusBorder(cx, cy);
		return;
	case WidgetType::Canvas:
		renderCanvas(cx, cy);
		renderLabel(cx, cy);
		return;
	case WidgetType::Scene: {
		if (!config_.widget.sceneName.isEmpty())
			source = obs_get_source_by_name(config_.widget.sceneName.toUtf8().constData());
		break;
	}
	case WidgetType::Source: {
		if (!config_.widget.sourceName.isEmpty())
			source = obs_get_source_by_name(config_.widget.sourceName.toUtf8().constData());
		break;
	}
	case WidgetType::Placeholder:
		renderPlaceholderIcon(cx, cy);
		renderLabel(cx, cy);
		return;
	case WidgetType::None:
	default:
		return;
	}

	if (source) {
		renderSource(source, cx, cy);
		obs_source_release(source);
	}
	renderLabel(cx, cy);
	renderStatusBorder(cx, cy);
}

// Rec. ITU-R BT.1848-1 / EBU R 95 safe area constants
#define OUTLINE_COLOR 0xFFD0D0D0
#define LINE_LENGTH 0.1f
#define ACTION_SAFE_PERCENT 0.035f
#define GRAPHICS_SAFE_PERCENT 0.05f
#define FOURBYTHREE_SAFE_PERCENT 0.1625f

void CellRenderer::initSafeAreaGeometry()
{
	// Build pre-built vertex buffers with normalized 0-1 coordinates,
	// matching OBS Studio's InitSafeAreas() from display-helpers.hpp.
	// Already in graphics context (called from draw callback).

	// Action safe margin (3.5% inset)
	gs_render_start(true);
	gs_vertex2f(ACTION_SAFE_PERCENT, ACTION_SAFE_PERCENT);
	gs_vertex2f(ACTION_SAFE_PERCENT, 1 - ACTION_SAFE_PERCENT);
	gs_vertex2f(1 - ACTION_SAFE_PERCENT, 1 - ACTION_SAFE_PERCENT);
	gs_vertex2f(1 - ACTION_SAFE_PERCENT, ACTION_SAFE_PERCENT);
	gs_vertex2f(ACTION_SAFE_PERCENT, ACTION_SAFE_PERCENT);
	actionSafeVb_ = gs_render_save();

	// Graphics safe margin (5% inset)
	gs_render_start(true);
	gs_vertex2f(GRAPHICS_SAFE_PERCENT, GRAPHICS_SAFE_PERCENT);
	gs_vertex2f(GRAPHICS_SAFE_PERCENT, 1 - GRAPHICS_SAFE_PERCENT);
	gs_vertex2f(1 - GRAPHICS_SAFE_PERCENT, 1 - GRAPHICS_SAFE_PERCENT);
	gs_vertex2f(1 - GRAPHICS_SAFE_PERCENT, GRAPHICS_SAFE_PERCENT);
	gs_vertex2f(GRAPHICS_SAFE_PERCENT, GRAPHICS_SAFE_PERCENT);
	graphicsSafeVb_ = gs_render_save();

	// 4:3 safe area for widescreen
	gs_render_start(true);
	gs_vertex2f(FOURBYTHREE_SAFE_PERCENT, GRAPHICS_SAFE_PERCENT);
	gs_vertex2f(1 - FOURBYTHREE_SAFE_PERCENT, GRAPHICS_SAFE_PERCENT);
	gs_vertex2f(1 - FOURBYTHREE_SAFE_PERCENT, 1 - GRAPHICS_SAFE_PERCENT);
	gs_vertex2f(FOURBYTHREE_SAFE_PERCENT, 1 - GRAPHICS_SAFE_PERCENT);
	gs_vertex2f(FOURBYTHREE_SAFE_PERCENT, GRAPHICS_SAFE_PERCENT);
	fourByThreeSafeVb_ = gs_render_save();

	// Center tick marks
	gs_render_start(true);
	gs_vertex2f(0.0f, 0.5f);
	gs_vertex2f(LINE_LENGTH, 0.5f);
	leftLineVb_ = gs_render_save();

	gs_render_start(true);
	gs_vertex2f(0.5f, 0.0f);
	gs_vertex2f(0.5f, LINE_LENGTH);
	topLineVb_ = gs_render_save();

	gs_render_start(true);
	gs_vertex2f(1.0f, 0.5f);
	gs_vertex2f(1 - LINE_LENGTH, 0.5f);
	rightLineVb_ = gs_render_save();
}

void CellRenderer::destroySafeAreaGeometry()
{
	obs_enter_graphics();
	gs_vertexbuffer_destroy(actionSafeVb_);
	gs_vertexbuffer_destroy(graphicsSafeVb_);
	gs_vertexbuffer_destroy(fourByThreeSafeVb_);
	gs_vertexbuffer_destroy(leftLineVb_);
	gs_vertexbuffer_destroy(topLineVb_);
	gs_vertexbuffer_destroy(rightLineVb_);
	obs_leave_graphics();
	actionSafeVb_ = nullptr;
	graphicsSafeVb_ = nullptr;
	fourByThreeSafeVb_ = nullptr;
	leftLineVb_ = nullptr;
	topLineVb_ = nullptr;
	rightLineVb_ = nullptr;
}

void CellRenderer::renderSafeAreas(int contentW, int contentH)
{
	// Lazily create vertex buffers on first use (already in graphics context)
	if (!actionSafeVb_)
		initSafeAreaGeometry();

	// Scale matrix from normalized 0-1 coordinates to content dimensions
	matrix4 transform;
	matrix4_identity(&transform);
	transform.x.x = (float)contentW;
	transform.y.y = (float)contentH;

	gs_matrix_push();
	gs_matrix_mul(&transform);

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_color(color, OUTLINE_COLOR);

	// Draw all six safe area elements in a single effect loop
	while (gs_effect_loop(solid, "Solid")) {
		gs_load_vertexbuffer(actionSafeVb_);
		gs_draw(GS_LINESTRIP, 0, 0);

		gs_load_vertexbuffer(graphicsSafeVb_);
		gs_draw(GS_LINESTRIP, 0, 0);

		gs_load_vertexbuffer(fourByThreeSafeVb_);
		gs_draw(GS_LINESTRIP, 0, 0);

		gs_load_vertexbuffer(leftLineVb_);
		gs_draw(GS_LINESTRIP, 0, 0);

		gs_load_vertexbuffer(topLineVb_);
		gs_draw(GS_LINESTRIP, 0, 0);

		gs_load_vertexbuffer(rightLineVb_);
		gs_draw(GS_LINESTRIP, 0, 0);
	}

	gs_matrix_pop();
}

// OBS multiview-style status colors (ARGB)
static const uint32_t previewColor = 0xFF00D000;
static const uint32_t programColor = 0xFFD00000;

void CellRenderer::renderStatusBorder(uint32_t cx, uint32_t cy)
{
	if (!config_.widget.showStatus)
		return;

	// Determine border color based on widget type and current OBS state
	uint32_t borderColor = 0;

	switch (config_.widget.type) {
	// case WidgetType::Preview:
	// 	borderColor = previewColor;
	// 	break;
	// case WidgetType::Program:
	// 	borderColor = programColor;
	// 	break;
	case WidgetType::Scene: {
		if (config_.widget.sceneName.isEmpty())
			return;
		QByteArray nameUtf8 = config_.widget.sceneName.toUtf8();

		// Check if this scene is the current program scene
		obs_source_t *programScene = obs_frontend_get_current_scene();
		if (programScene) {
			if (strcmp(obs_source_get_name(programScene), nameUtf8.constData()) == 0)
				borderColor = programColor;
			obs_source_release(programScene);
		}

		// Check if this scene is the current preview scene (studio mode)
		if (!borderColor && obs_frontend_preview_program_mode_active()) {
			obs_source_t *previewScene = obs_frontend_get_current_preview_scene();
			if (previewScene) {
				if (strcmp(obs_source_get_name(previewScene), nameUtf8.constData()) == 0)
					borderColor = previewColor;
				obs_source_release(previewScene);
			}
		}
		break;
	}
	default:
		return;
	}

	if (!borderColor)
		return;

	// Draw border as 4 filled strips around the cell edges
	const int borderW = qMax(2, (int)(qMin(cx, cy) * 0.015f));

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_color(color, borderColor);

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(0, 0, cx, cy);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

	while (gs_effect_loop(solid, "Solid")) {
		// Top strip
		gs_draw_sprite(nullptr, 0, cx, borderW);

		// Bottom strip
		gs_matrix_push();
		gs_matrix_translate3f(0.0f, (float)(cy - borderW), 0.0f);
		gs_draw_sprite(nullptr, 0, cx, borderW);
		gs_matrix_pop();

		// Left strip
		gs_matrix_push();
		gs_matrix_translate3f(0.0f, (float)borderW, 0.0f);
		gs_draw_sprite(nullptr, 0, borderW, cy - 2 * borderW);
		gs_matrix_pop();

		// Right strip
		gs_matrix_push();
		gs_matrix_translate3f((float)(cx - borderW), (float)borderW, 0.0f);
		gs_draw_sprite(nullptr, 0, borderW, cy - 2 * borderW);
		gs_matrix_pop();
	}

	gs_projection_pop();
	gs_viewport_pop();
}

void CellRenderer::renderPreviewProgram(uint32_t cx, uint32_t cy, bool isProgram)
{
	// Get canvas dimensions
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	uint32_t canvasW = ovi.base_width;
	uint32_t canvasH = ovi.base_height;

	if (canvasW == 0 || canvasH == 0)
		return;

	// Calculate scale and offset to fit canvas in cell while maintaining aspect ratio
	int offsetX, offsetY, scaledW, scaledH;
	float scale;
	GetScaleAndCenterPos(canvasW, canvasH, cx, cy, offsetX, offsetY, scale, scaledW, scaledH);

	// Set up transform for rendering using calculated scaled dimensions
	// Use viewport-based rendering for pixel-perfect centering
	gs_viewport_push();
	gs_projection_push();

	gs_set_viewport(offsetX, offsetY, scaledW, scaledH);
	gs_ortho(0.0f, (float)canvasW, 0.0f, (float)canvasH, -100.0f, 100.0f);

	// Program always shows main output; Preview shows the preview scene
	// in studio mode, otherwise falls back to main output
	if (isProgram || !obs_frontend_preview_program_mode_active()) {
		obs_render_main_texture();
	} else {
		obs_source_t *previewScene = obs_frontend_get_current_preview_scene();
		if (previewScene) {
			obs_source_video_render(previewScene);
			obs_source_release(previewScene);
		}
	}

	if (config_.widget.safeRegion)
		renderSafeAreas(canvasW, canvasH);

	gs_projection_pop();
	gs_viewport_pop();
}

void CellRenderer::renderCanvas(uint32_t cx, uint32_t cy)
{
	// Get the canvas - either by name or the main canvas
	obs_canvas_t *canvas = nullptr;
	bool releaseCanvas = false;

	if (!config_.widget.canvasName.isEmpty()) {
		// Get canvas by name using OBS Canvas API
		canvas = obs_get_canvas_by_name(config_.widget.canvasName.toUtf8().constData());
		releaseCanvas = true;
	} else {
		// Use main canvas
		canvas = obs_get_main_canvas();
		releaseCanvas = true;
	}

	if (!canvas) {
		// Fallback: render main texture if canvas not found
		struct obs_video_info ovi;
		if (!obs_get_video_info(&ovi))
			return;

		uint32_t canvasW = ovi.base_width;
		uint32_t canvasH = ovi.base_height;
		if (canvasW == 0 || canvasH == 0)
			return;

		int offsetX, offsetY, scaledW, scaledH;
		float scale;
		GetScaleAndCenterPos(canvasW, canvasH, cx, cy, offsetX, offsetY, scale, scaledW, scaledH);

		gs_viewport_push();
		gs_projection_push();
		gs_set_viewport(offsetX, offsetY, scaledW, scaledH);
		gs_ortho(0.0f, (float)canvasW, 0.0f, (float)canvasH, -100.0f, 100.0f);
		obs_render_main_texture();
		if (config_.widget.safeRegion)
			renderSafeAreas(canvasW, canvasH);
		gs_projection_pop();
		gs_viewport_pop();
		return;
	}

	// Get canvas video info for dimensions
	struct obs_video_info ovi;
	if (!obs_canvas_get_video_info(canvas, &ovi)) {
		if (releaseCanvas)
			obs_canvas_release(canvas);
		return;
	}

	uint32_t canvasW = ovi.base_width;
	uint32_t canvasH = ovi.base_height;

	if (canvasW == 0 || canvasH == 0) {
		if (releaseCanvas)
			obs_canvas_release(canvas);
		return;
	}

	// Calculate scale and offset to fit canvas in cell while maintaining aspect ratio
	int offsetX, offsetY, scaledW, scaledH;
	float scale;
	GetScaleAndCenterPos(canvasW, canvasH, cx, cy, offsetX, offsetY, scale, scaledW, scaledH);

	// Set up viewport for pixel-perfect centering
	gs_viewport_push();
	gs_projection_push();

	gs_set_viewport(offsetX, offsetY, scaledW, scaledH);
	gs_ortho(0.0f, (float)canvasW, 0.0f, (float)canvasH, -100.0f, 100.0f);

	// Render the canvas texture using OBS Canvas API
	obs_render_canvas_texture(canvas);

	if (config_.widget.safeRegion)
		renderSafeAreas(canvasW, canvasH);

	gs_projection_pop();
	gs_viewport_pop();

	if (releaseCanvas)
		obs_canvas_release(canvas);
}

void CellRenderer::renderSource(obs_source_t *source, uint32_t cx, uint32_t cy)
{
	uint32_t srcW = obs_source_get_width(source);
	uint32_t srcH = obs_source_get_height(source);

	if (srcW == 0 || srcH == 0)
		return;

	// Calculate scale to fit while maintaining aspect ratio
	int offsetX, offsetY, scaledW, scaledH;
	float scale;
	GetScaleAndCenterPos(srcW, srcH, cx, cy, offsetX, offsetY, scale, scaledW, scaledH);

	gs_viewport_push();
	gs_projection_push();

	gs_set_viewport(offsetX, offsetY, scaledW, scaledH);
	gs_ortho(0.0f, (float)srcW, 0.0f, (float)srcH, -100.0f, 100.0f);

	obs_source_video_render(source);

	if (config_.widget.safeRegion)
		renderSafeAreas(srcW, srcH);

	gs_projection_pop();
	gs_viewport_pop();
}

// --- Label text source management ---

static const char *GetTextSourceId()
{
#ifdef _WIN32
	return "text_gdiplus";
#else
	return "text_ft2_source";
#endif
}

QString CellRenderer::resolveLabelText() const
{
	if (!config_.widget.labelVisible || config_.widget.type == WidgetType::None)
		return QString();

	if (!config_.widget.labelText.isEmpty())
		return config_.widget.labelText;

	switch (config_.widget.type) {
	case WidgetType::Preview:
		return QString::fromUtf8(LG_TEXT("Renderer.Preview"));
	case WidgetType::Program:
		return QString::fromUtf8(LG_TEXT("Renderer.Program"));
	case WidgetType::Canvas:
		return config_.widget.canvasName.isEmpty() ? QString::fromUtf8(LG_TEXT("Renderer.Canvas"))
							   : config_.widget.canvasName;
	case WidgetType::Scene:
		return config_.widget.sceneName;
	case WidgetType::Source:
		return config_.widget.sourceName;
	case WidgetType::Placeholder:
		return QString::fromUtf8(LG_TEXT("Renderer.Placeholder"));
	default:
		return QString();
	}
}

void CellRenderer::createLabelSource()
{
	destroyLabelSource();

	QString text = resolveLabelText();
	if (text.isEmpty())
		return;

	// Parse font from config (QFont::toString() format) or use default
	QFont font;
	if (!config_.widget.labelFont.isEmpty())
		font.fromString(config_.widget.labelFont);
	else
		font.setPointSize(36);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.toUtf8().constData());
	obs_data_set_string(settings, "font_face", font.family().toUtf8().constData());
	obs_data_set_int(settings, "font_size", font.pointSize() > 0 ? font.pointSize() : 36);

	// Build font flags bitmask (bit 0 = bold, bit 1 = italic)
	int fontFlags = 0;
	if (font.bold())
		fontFlags |= 1;
	if (font.italic())
		fontFlags |= 2;

	// Derive the full style name (e.g. "Light Italic", "SemiBold") from
	// the font's weight and italic properties so sub-weights are preserved.
	QString style = QFontDatabase::styleString(font);
	if (style.isEmpty())
		style = QStringLiteral("Regular");

	obs_data_t *fontData = obs_data_create();
	obs_data_set_string(fontData, "face", font.family().toUtf8().constData());
	obs_data_set_int(fontData, "size", font.pointSize() > 0 ? font.pointSize() : 36);
	obs_data_set_int(fontData, "flags", fontFlags);
	obs_data_set_string(fontData, "style", style.toUtf8().constData());
	obs_data_set_obj(settings, "font", fontData);
	obs_data_release(fontData);

	// White text with full opacity
	obs_data_set_int(settings, "color1", 0xFFFFFFFF);
	obs_data_set_int(settings, "color2", 0xFFFFFFFF);

	labelSource_ = obs_source_create_private(GetTextSourceId(), "lg_label", settings);
	obs_data_release(settings);
}

void CellRenderer::destroyLabelSource()
{
	if (labelSource_) {
		obs_enter_graphics();
		obs_source_release(labelSource_);
		obs_leave_graphics();
		labelSource_ = nullptr;
	}
}

void CellRenderer::updateLabelSource()
{
	// Recreate the label source with updated config
	createLabelSource();
}

// Creates a gs_texture from a QImage with a rounded rectangle filled with the
// given color and alpha. The texture is w x h pixels with the specified corner radius.
static gs_texture_t *CreateRoundedRectTexture(int w, int h, int radius, QColor color)
{
	if (w <= 0 || h <= 0)
		return nullptr;

	QImage img(w, h, QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);

	QPainter painter(&img);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setBrush(color);
	painter.setPen(Qt::NoPen);
	painter.drawRoundedRect(0, 0, w, h, radius, radius);
	painter.end();

	// Upload to GPU — obs_enter_graphics is already active in draw callback
	const uint8_t *bits = img.constBits();
	gs_texture_t *tex = gs_texture_create(w, h, GS_RGBA, 1, &bits, 0);
	return tex;
}

void CellRenderer::renderLabel(uint32_t cx, uint32_t cy)
{
	if (!labelSource_)
		return;

	uint32_t labelW = obs_source_get_width(labelSource_);
	uint32_t labelH = obs_source_get_height(labelSource_);

	if (labelW == 0 || labelH == 0)
		return;

	// Scale the label proportionally to the cell size using the approach
	// from OBS Studio's multiview. The label should occupy at most ~15%
	// of the cell height and must fit within the cell width. The scale
	// is capped at 1.0 so labels never upscale beyond their native size.
	float maxH = (float)cy * 0.15f;
	float heightScale = maxH / (float)labelH;
	float widthScale = ((float)cx * 0.9f) / (float)labelW;
	float scale = qMin(1.0f, qMin(heightScale, widthScale));

	// Scaled label dimensions in cell pixels
	int scaledW = qMax(1, (int)(labelW * scale));
	int scaledH = qMax(1, (int)(labelH * scale));

	// Scale padding proportionally
	int padding = qMax(1, (int)(6.0f * scale));

	// Calculate label position using scaled dimensions
	int labelX = 0;
	int labelY = 0;

	// Horizontal alignment
	Qt::Alignment hAlign = config_.widget.labelHAlign;
	if (hAlign & Qt::AlignHCenter)
		labelX = ((int)cx - scaledW) / 2;
	else if (hAlign & Qt::AlignRight)
		labelX = (int)cx - scaledW - padding;
	else
		labelX = padding;

	// Vertical alignment
	Qt::Alignment vAlign = config_.widget.labelVAlign;
	if (vAlign & Qt::AlignVCenter)
		labelY = ((int)cy - scaledH) / 2;
	else if (vAlign & Qt::AlignBottom)
		labelY = (int)cy - scaledH - padding;
	else
		labelY = padding;

	// Draw rounded background rectangle behind the label if it has any opacity.
	// The background texture is created at the scaled pixel size so it
	// matches the label and is recreated when dimensions or color change.
	QColor bgColor = config_.widget.labelBgColor;
	if (bgColor.alpha() > 0) {
		int bgPad = qMax(1, (int)(4.0f * scale));
		int bgRadius = qMax(1, (int)(6.0f * scale));
		int bgX = labelX - bgPad;
		int bgY = labelY - bgPad;
		int bgW = scaledW + 2 * bgPad;
		int bgH = scaledH + 2 * bgPad;

		// Recreate cached texture only when dimensions or color change
		if (!labelBgTexture_ || labelBgTexW_ != bgW || labelBgTexH_ != bgH || labelBgTexColor_ != bgColor) {
			if (labelBgTexture_) {
				gs_texture_destroy(labelBgTexture_);
				labelBgTexture_ = nullptr;
			}
			labelBgTexture_ = CreateRoundedRectTexture(bgW, bgH, bgRadius, bgColor);
			labelBgTexW_ = bgW;
			labelBgTexH_ = bgH;
			labelBgTexColor_ = bgColor;
		}

		if (labelBgTexture_) {
			gs_blend_state_push();
			gs_enable_blending(true);
			gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

			gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
			gs_eparam_t *imageParam = gs_effect_get_param_by_name(effect, "image");
			gs_effect_set_texture(imageParam, labelBgTexture_);

			gs_viewport_push();
			gs_projection_push();
			gs_set_viewport(bgX, bgY, bgW, bgH);
			gs_ortho(0.0f, (float)bgW, 0.0f, (float)bgH, -100.0f, 100.0f);

			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(labelBgTexture_, 0, bgW, bgH);

			gs_projection_pop();
			gs_viewport_pop();

			gs_blend_state_pop();
		}
	}

	// Render the text source scaled via viewport mapping: the viewport is
	// set to the scaled pixel area while the ortho projection spans the
	// original label dimensions, so the GPU performs the scaling.
	gs_viewport_push();
	gs_projection_push();

	gs_set_viewport(labelX, labelY, scaledW, scaledH);
	gs_ortho(0.0f, (float)labelW, 0.0f, (float)labelH, -100.0f, 100.0f);

	obs_source_video_render(labelSource_);

	gs_projection_pop();
	gs_viewport_pop();
}

// --- Placeholder icon rendering ---

void CellRenderer::setPlaceholderSvgPath(const QString &path)
{
	placeholderSvgPath_ = path;
}

// Called from the draw callback (graphics context already active)
void CellRenderer::createPlaceholderTexture()
{
	// Destroy old texture directly (we're in graphics context)
	if (placeholderTexture_) {
		gs_texture_destroy(placeholderTexture_);
		placeholderTexture_ = nullptr;
		placeholderTexSize_ = 0;
	}

	if (placeholderSvgPath_.isEmpty())
		return;

	// Determine icon size: 50% of cell dimension, min 16px
	int cellW = surface_ ? surface_->width() : 0;
	int cellH = surface_ ? surface_->height() : 0;
	int iconSize = qMin(cellW, cellH) / 2;
	if (iconSize < 16)
		iconSize = 16;

	// Render SVG to QImage
	QSvgRenderer svgRenderer(placeholderSvgPath_);
	if (!svgRenderer.isValid())
		return;

	QImage img(iconSize, iconSize, QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);
	QPainter painter(&img);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	svgRenderer.render(&painter);
	painter.end();

	// Upload to GPU (already in graphics context)
	const uint8_t *bits = img.constBits();
	placeholderTexture_ = gs_texture_create(iconSize, iconSize, GS_RGBA, 1, &bits, 0);
	placeholderTexSize_ = iconSize;
}

// Called from cleanup/destructor (graphics context may not be active)
void CellRenderer::destroyPlaceholderTexture()
{
	if (placeholderTexture_) {
		obs_enter_graphics();
		gs_texture_destroy(placeholderTexture_);
		obs_leave_graphics();
		placeholderTexture_ = nullptr;
		placeholderTexSize_ = 0;
	}
}

// Called from cleanup/destructor (graphics context may not be active)
void CellRenderer::destroyLabelBgTexture()
{
	if (labelBgTexture_) {
		obs_enter_graphics();
		gs_texture_destroy(labelBgTexture_);
		obs_leave_graphics();
		labelBgTexture_ = nullptr;
		labelBgTexW_ = 0;
		labelBgTexH_ = 0;
		labelBgTexColor_ = QColor();
	}
}

void CellRenderer::renderPlaceholderIcon(uint32_t cx, uint32_t cy)
{
	if (placeholderSvgPath_.isEmpty())
		return;

	// Determine desired icon size for the current cell dimensions
	int desiredSize = qMin((int)cx, (int)cy) / 2;
	if (desiredSize < 16)
		desiredSize = 16;

	// Recreate texture if size changed or not yet created
	if (!placeholderTexture_ || placeholderTexSize_ != desiredSize)
		createPlaceholderTexture();

	if (!placeholderTexture_)
		return;

	int iconW = placeholderTexSize_;
	int iconH = placeholderTexSize_;

	// Center the icon in the cell
	int iconX = ((int)cx - iconW) / 2;
	int iconY = ((int)cy - iconH) / 2;

	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *imageParam = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(imageParam, placeholderTexture_);

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(iconX, iconY, iconW, iconH);
	gs_ortho(0.0f, (float)iconW, 0.0f, (float)iconH, -100.0f, 100.0f);

	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(placeholderTexture_, 0, iconW, iconH);

	gs_projection_pop();
	gs_viewport_pop();

	gs_blend_state_pop();
}
