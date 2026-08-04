/*=====================================================================
GaussianSplatSettingsWidget.h
------------------------------
coded by AI agent under @russiaman supervision -
Generated at Wed Aug  5 00:00:00 2026
=====================================================================*/
#pragma once


#include "ui_GaussianSplatSettingsWidget.h"

class QSettings;


/*=====================================================================
GaussianSplatSettingsWidget
----------------------------
Live-tunable Gaussian Splat LoD parameters (Claude_LOD_plan.md stage 7) - hosted in a QDockWidget next to
Diagnostics (see MainWindow.ui's gaussianSplatSettingsDockWidget / menuWindow), added after real-scale
testing (an 8.6M-splat scene) showed the hardcoded defaults needed adjusting per-scene rather than
per-build. Mirrors DiagnosticsWidget's shape: this widget itself knows nothing about GUIClient/
GaussianSplatRenderer - it just emits settingsChangedSignal() whenever a control changes, and
MainWindow::gaussianSplatSettingsChanged() reads the current values and pushes them into
gui_client.gaussian_splat_renderer's live setters (pixel_scale_limit/max_splats_budget/
resort_move_threshold_ws - take effect on the next traversal/sort kick-off, no reload needed) or
gui_client.gaussian_splat_lod_base (a tree-BUILD-time parameter - only affects objects loaded/reloaded
after the change, see that field's comment in GUIClient.h).
=====================================================================*/
class GaussianSplatSettingsWidget : public QWidget, public Ui_GaussianSplatSettingsWidget
{
	Q_OBJECT
public:
	GaussianSplatSettingsWidget(QWidget* parent);
	~GaussianSplatSettingsWidget();

	// Populates the controls from whatever was last saved to settings (QSettings, same store DiagnosticsWidget uses - persists across app restarts, unlike GaussianSplatRenderer's own in-memory state),
	// falling back to the given defaults (the renderer's actual compiled-in defaults, so a first-ever run - nothing saved yet - still shows values that match what's really running) if nothing was saved
	// yet. Call once, after gui_client (and so gaussian_splat_renderer) exists. Blocks signals while doing so, so this doesn't itself trigger settingsChangedSignal() - caller is responsible for pushing
	// the now-populated (possibly restored-from-disk) values into gui_client once, right after calling this (see MainWindow's constructor) - this widget has no way to do that itself.
	void init(QSettings* settings, float default_lod_base, float default_pixel_scale_limit, size_t default_max_splats_budget, float default_resort_move_threshold_ws);

signals:;
	void settingsChangedSignal();

protected slots:
	void settingsChanged();

private:
	QSettings* settings;
};
