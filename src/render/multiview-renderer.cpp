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
static void GetScaleAndCenterPos(int baseCX, int baseCY, int windowCX, int windowCY,
				 int &x, int &y, float &scale, int &scaledCX, int &scaledCY)
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
		return;
	case WidgetType::Program:
		renderPreviewProgram(cx, cy, true);
		renderLabel(cx, cy);
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

#ifdef _WIN32
	// text_gdiplus uses a nested "font" object for font configuration
	obs_data_t *fontData = obs_data_create();
	obs_data_set_string(fontData, "face", font.family().toUtf8().constData());
	obs_data_set_int(fontData, "size", font.pointSize() > 0 ? font.pointSize() : 36);
	obs_data_set_int(fontData, "flags", font.bold() ? 1 : 0);
	obs_data_set_string(fontData, "style", font.bold() ? "Bold" : "Regular");
	obs_data_set_obj(settings, "font", fontData);
	obs_data_release(fontData);

	// White text with full opacity
	obs_data_set_int(settings, "color1", 0xFFFFFFFF);
	obs_data_set_int(settings, "color2", 0xFFFFFFFF);
#else
	// text_ft2_source settings
	obs_data_set_int(settings, "color1", 0xFFFFFFFF);
	obs_data_set_int(settings, "color2", 0xFFFFFFFF);
#endif

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

	// Calculate label position based on alignment settings
	const int padding = 6;
	int labelX = 0;
	int labelY = 0;

	// Horizontal alignment
	Qt::Alignment hAlign = config_.widget.labelHAlign;
	if (hAlign & Qt::AlignHCenter)
		labelX = ((int)cx - (int)labelW) / 2;
	else if (hAlign & Qt::AlignRight)
		labelX = (int)cx - (int)labelW - padding;
	else
		labelX = padding;

	// Vertical alignment
	Qt::Alignment vAlign = config_.widget.labelVAlign;
	if (vAlign & Qt::AlignVCenter)
		labelY = ((int)cy - (int)labelH) / 2;
	else if (vAlign & Qt::AlignBottom)
		labelY = (int)cy - (int)labelH - padding;
	else
		labelY = padding;

	// Draw rounded background rectangle behind the label if it has any opacity
	QColor bgColor = config_.widget.labelBgColor;
	if (bgColor.alpha() > 0) {
		const int bgPad = 4;
		const int bgRadius = 6;
		int bgX = labelX - bgPad;
		int bgY = labelY - bgPad;
		int bgW = (int)labelW + 2 * bgPad;
		int bgH = (int)labelH + 2 * bgPad;

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

	// Render the text source as an overlay at the calculated position
	gs_viewport_push();
	gs_projection_push();

	gs_set_viewport(labelX, labelY, labelW, labelH);
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
