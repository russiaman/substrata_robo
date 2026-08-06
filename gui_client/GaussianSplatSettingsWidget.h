/*=====================================================================
GaussianSplatSettingsWidget.h
------------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "ui_GaussianSplatSettingsWidget.h"

class QSettings;


/*=====================================================================
GaussianSplatSettingsWidget
----------------------------
Live-tunable Gaussian Splat LoD parameters (pixel_scale_limit,
max_splats_budget, resort_move_threshold_ws, lod_base), modelled on
DiagnosticsWidget - same init(settings)/settingsChangedSignal() pattern,
persisted the same way (QSettings).

The first three apply live, picked up by GaussianSplatRenderer on its next
traversal kick-off. lod_base only affects a tree built after it changes -
see the widget's own note label.
=====================================================================*/
class GaussianSplatSettingsWidget : public QWidget, public Ui_GaussianSplatSettingsWidget
{
	Q_OBJECT
public:
	GaussianSplatSettingsWidget(QWidget* parent);
	~GaussianSplatSettingsWidget();

	void init(QSettings* settings);

signals:;
	void settingsChangedSignal();

protected slots:
	void settingsChanged();
private:
	QSettings* settings;
};
