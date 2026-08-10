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

Also carries size_clamp_min/max, a debug tool rather than a LoD parameter:
hides any splat whose feature size falls outside the given range, applied
directly in the vertex shader every frame regardless of LoD tree state. See
GaussianSplatRenderer::getSizeClampMin()/getSizeClampMax().
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
	void countInFrustumRequestedSignal(); // Emitted by the "Count in frustum" button - see GaussianSplatRenderer::countSplatsInFrustum().
	void frustumReportRequestedSignal(); // Emitted by the "Frustum report" button - see GaussianSplatRenderer::getFrustumStructureReport().
	void resetImportanceRequestedSignal(); // Emitted by the "Reset importance" button - see GaussianSplatRenderer::resetImportanceAccumulator().
	void mergeCoplanarRequestedSignal(); // Emitted by the "Merge coplanar" button - see GaussianSplatRenderer::applyCoplanarMerge().
	void restoreUnmergedRequestedSignal(); // Emitted by the "Restore unmerged" button - see GaussianSplatRenderer::restoreUnmergedSplats().

protected slots:
	void settingsChanged();
private:
	QSettings* settings;
};
