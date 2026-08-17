/*=====================================================================
GaussianSplatSettingsWidget.h
------------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "ui_GaussianSplatSettingsWidget.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

	// Refresh the "Scene splats" dropdown from an externally-built list of (UID, label) pairs.
	// Called ~1Hz from MainWindow while the panel's dock is visible; skipped internally while the
	// popup is open so user selection isn't disrupted, and skipped when the list hasn't changed.
	void setSplatList(const std::vector<std::pair<uint64_t, std::string>>& items);

signals:;
	void settingsChangedSignal();
	void splatSelectedSignal(quint64 ob_uid); // "Scene splats" dropdown - user picked a splat; MainWindow selects the corresponding WorldObject.
	void countInFrustumRequestedSignal(); // Emitted by the "Count in frustum" button - see GaussianSplatRenderer::countSplatsInFrustum().
	void frustumReportRequestedSignal(); // Emitted by the "Frustum report" button - see GaussianSplatRenderer::getFrustumStructureReport().
	void resetImportanceRequestedSignal(); // Emitted by the "Reset importance" button - see GaussianSplatRenderer::resetImportanceAccumulator().
	void mergeCoplanarRequestedSignal(); // Emitted by the "Merge coplanar" button - see GaussianSplatRenderer::applyCoplanarMerge().
	void restoreUnmergedRequestedSignal(); // Emitted by the "Restore unmerged" button - see GaussianSplatRenderer::restoreUnmergedSplats().
	void saturationSnapshotsRequestedSignal(); // DIAGNOSTIC ONLY - emitted by the "Saturation snapshots" button, see OpenGLEngine::requestSplatSaturationSnapshots().
	void layerCapEstimateRequestedSignal(); // DIAGNOSTIC ONLY - emitted by the "Estimate" button, see OpenGLEngine::estimateSplatLayerCapSaving().

protected slots:
	void settingsChanged();
	void splatComboActivated(int index); // "Scene splats" dropdown - user picked an entry.

	// The one shrink box serves both shrink modes, and the same number means different things in them - see
	// GaussianSplatRenderer::getCoverageShrinkMode(). This gives each mode its own remembered value, so flipping the
	// combo box compares two settings that were each tuned rather than one number reinterpreted.
	void coverageShrinkModeChanged(int mode);
private:
	QSettings* settings;

	// Last value the shrink box held in each mode, indexed by mode - see coverageShrinkModeChanged().
	double coverage_shrink_value_for_mode[3];
	int coverage_shrink_prev_mode;
};
