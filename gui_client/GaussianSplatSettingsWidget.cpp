/*=====================================================================
GaussianSplatSettingsWidget.cpp
---------------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GaussianSplatSettingsWidget.h"


#include <QtCore/QSettings>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QSpinBox>
#include <qt/QtUtils.h>
#include "../qt/SignalBlocker.h"


GaussianSplatSettingsWidget::GaussianSplatSettingsWidget(
	QWidget* parent
)
:	settings(NULL),
	coverage_shrink_prev_mode(0)
{
	setupUi(this);

	// SESSION073: accumUpsampleModeComboBox replaces the old bilinear-upsample checkbox with a 2-item dropdown (room to
	// grow past two without another UI rework) - per-item tooltips aren't settable from the .ui file, so set here. Order
	// matches the .ui item order (index 0 = nearest, 1 = bilinear).
	this->accumUpsampleModeComboBox->setItemData(0, tr("Nearest: reads the downscaled accumulation buffer back up to the frame with no interpolation, i.e. visibly blocky. Kept as a diagnostic - seeing the blocks is how one confirms the buffer really is smaller, and it is the honest baseline the bilinear cost is compared against."), Qt::ToolTipRole);
	this->accumUpsampleModeComboBox->setItemData(1, tr("Bilinear: what makes the downscale usable. A splat is a Gaussian, so its screen footprint is band-limited by construction, and a smooth reconstruction of it loses far less than the same downscale would on ordinary geometry."), Qt::ToolTipRole);

	// SESSION078: "Saturation mask"/"Saturation ramp" - the sat_depth debug overlay (session076 §9), moved into this
	// dropdown from its own standalone "diag"/"ramp" checkboxes so it activates the same way every other debug view
	// here does: "Show debug" on, this entry selected. Per-item tooltips again set here rather than in the .ui - see
	// the comment above. Indices 4 and 5 match the .ui's item order, after the four existing entries.
	this->debugModeComboBox->setItemData(4, tr("What this scene's saturation-cull mechanism (the \"Saturation filter\" row above) thinks is already fully opaque. Draws a giant sphere around the anchor point, in Bondi Blue wherever the mechanism has decided nothing further behind that direction needs drawing - the same test the prune itself makes, so this is its actual verdict, not a re-derivation. Compare against what the scene plainly looks like: an opaque wall or ceiling should come out solidly coloured, and a gap should sit exactly at the edge of open space, not inside solid geometry (which would mean things are being wrongly culled) or well past it (which would mean the cull isn't doing much). Works even with \"Saturation filter\" itself off, to see the mask over the untouched scene rather than one it has already edited."), Qt::ToolTipRole);
	this->debugModeComboBox->setItemData(5, tr("The same overlay as \"Saturation mask\", but showing HOW saturated each direction is rather than just the yes/no verdict - a blue -> green -> red ramp over how much occluding material has piled up behind each point on the sphere, using the \"Show overdraw\" row's min/max range below. Useful when the mask above looks wrong: a flat ceiling coming out half coloured and half not looks the same in the binary view whether the cull is working correctly at a sharp edge or the underlying accumulation is broken, and this ramp tells the two apart by showing the actual quantity behind the verdict."), Qt::ToolTipRole);

	// Per-mode starting values for the shared shrink box - see coverageShrinkModeChanged(). Mode 0 starts off; mode 1
	// starts at the setting measured to be worth having, which init() then selects - see there.
	coverage_shrink_value_for_mode[0] = 0.0;
	coverage_shrink_value_for_mode[1] = 0.02;
	coverage_shrink_value_for_mode[2] = 0.1; // Mode 2's threshold is bounded, so the same number is far milder in it than in mode 1 - 0.1 is where it was measured to pay, at 16 ms against 22.2 with no shrink, with nothing visible given up.

	connect(this->pixelScaleLimitDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->adaptivePixelScaleCheckBox,       SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION079
	connect(this->adaptiveTargetFPSDoubleSpinBox,   SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION079
	connect(this->maxSplatsBudgetSpinBox,           SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->resortMoveThresholdDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->lodBaseDoubleSpinBox,             SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->sizeClampMinDoubleSpinBox,        SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->sizeClampMaxDoubleSpinBox,        SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->sizeClampInvertCheckBox,          SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->ewaProjectionFixCheckBox,         SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->nearFadeWidthDoubleSpinBox,       SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->alphaCutoffDoubleSpinBox,         SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->alphaGainDoubleSpinBox,           SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->alphaGammaDoubleSpinBox,          SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->alphaAdjustIgnoreCheckBox,        SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->countInFrustumPushButton,         SIGNAL(clicked()), this, SIGNAL(countInFrustumRequestedSignal()));
	connect(this->frustumReportPushButton,          SIGNAL(clicked()), this, SIGNAL(frustumReportRequestedSignal()));
	connect(this->saturationSnapshotsPushButton,    SIGNAL(clicked()), this, SIGNAL(saturationSnapshotsRequestedSignal())); // DIAGNOSTIC ONLY - see GaussianSplatSettingsWidget.h.
	connect(this->showDebugCheckBox,                SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
	connect(this->debugModeComboBox,                SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
	connect(this->clipCheckBox,                     SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
	connect(this->overdrawRangeMinDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->overdrawRangeMaxDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->maxLayerDensityDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->maxTreeDepthSpinBox,              SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->splitPipelineCheckBox,            SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION075: was frustumCullCheckBox, split from "cull" below.
	connect(this->saturationFilterCheckBox,         SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION075: was the Sat pre-filter row's combo box.
	connect(this->satPrefilterThresholdDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION079
	connect(this->satGridSubdivDoubleSpinBox,       SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION076 CALIBRATION
	connect(this->satRegionRadiusDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION078
	connect(this->satDiagCheckBox,                  SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION076 DIAGNOSTIC
	connect(this->cullCheckBox,                     SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION075: was filterFrustumPlanesCheckBox, relocated+relabelled.
	connect(this->filterDilationLatencyDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K3
	connect(this->filterMinRotRateDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K3
	connect(this->filterMaxRotRateDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION071
	connect(this->filterMinTransRateDoubleSpinBox,  SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K3
	connect(this->coarseFloorCheckBox,              SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION063 K4
	connect(this->coarsePixelScaleDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K4
	connect(this->coarseDilationLatencyDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K4
	connect(this->coarseLayerDebugCheckBox,         SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION063 K4
	connect(this->energyMergeColourCheckBox,        SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION071
	connect(this->mergeSpreadWidenDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION071
	connect(this->numDrawSlicesSpinBox,             SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->drawSliceLimitSpinBox,            SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->visibleSlicingCheckBox,           SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->sliceGrowthDoubleSpinBox,         SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->saturationGateCheckBox,           SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->saturationThresholdDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->saturationMaskDownscaleSpinBox,   SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->coverageReduceModeComboBox,       SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
	connect(this->coverageShrinkStrengthDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	// Two connections, in this order: the mode's own handler swaps the value in the shared box first, so that by the time
	// settingsChanged() reads the box it holds the value belonging to the mode now selected.
	connect(this->coverageShrinkModeComboBox,       SIGNAL(currentIndexChanged(int)), this, SLOT(coverageShrinkModeChanged(int)));
	connect(this->coverageShrinkModeComboBox,       SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
	connect(this->layerCapSpinBox,                  SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->layerCapOnCheckBox,               SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->layerCapOpaqueCheckBox,           SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->layerCapEstimatePushButton,       SIGNAL(clicked()), this, SIGNAL(layerCapEstimateRequestedSignal()));
	connect(this->coverageCapThresholdDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->coverageCapOnCheckBox,            SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->ablationStageComboBox,            SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
	connect(this->pointSizePxDoubleSpinBox,         SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION071 diagnostic
	connect(this->quadRadiusScaleDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->areaScaleGammaDoubleSpinBox,      SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->areaScaleRefPxDoubleSpinBox,      SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->hideTestComboBox,                 SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
	connect(this->accumBuffer8BitCheckBox,          SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->accumBufferScaleDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION067
	connect(this->accumUpsampleModeComboBox,        SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged())); // SESSION073: was accumUpsampleBilinearCheckBox.
	connect(this->areaSliceModeComboBox,            SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged())); // SESSION067 DIAGNOSTIC
	connect(this->areaSlicePxDoubleSpinBox,         SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->deconvEnabledCheckBox,            SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION068
	connect(this->deconvGainDoubleSpinBox,          SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->rcasEnabledCheckBox,              SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->rcasSharpnessDoubleSpinBox,       SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->taaEnabledCheckBox,               SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION069
	connect(this->dofDepthModeComboBox,             SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
	connect(this->distClampEnabledCheckBox,         SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION072
	connect(this->distClampMinDoubleSpinBox,        SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->distClampMaxDoubleSpinBox,        SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->distClampInvertCheckBox,          SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->mergeColourTolDoubleSpinBox,      SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->mergeAngleTolDoubleSpinBox,       SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->mergeAcrossDoubleSpinBox,         SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->mergeThroughDoubleSpinBox,        SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->mergeFlattenCheckBox,             SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->mergeCoplanarPushButton,          SIGNAL(clicked()), this, SIGNAL(mergeCoplanarRequestedSignal()));
	connect(this->restoreUnmergedPushButton,        SIGNAL(clicked()), this, SIGNAL(restoreUnmergedRequestedSignal()));
	connect(this->rebuildLodsPushButton,            SIGNAL(clicked()), this, SIGNAL(rebuildLodsRequestedSignal())); // SESSION073

	// SESSION072: "Settings presets" row.
	connect(this->resetToDefaultPushButton,         SIGNAL(clicked()), this, SLOT(resetToDefaultsClicked()));
	connect(this->presetComboBox,                   SIGNAL(currentIndexChanged(int)), this, SLOT(presetSelected(int)));
	connect(this->savePresetPushButton,             SIGNAL(clicked()), this, SLOT(savePresetClicked()));

	// SESSION072: "Console logs" row.
	connect(this->filterLogCheckBox,                SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
	connect(this->kickLogCheckBox,                  SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
	connect(this->profLogCheckBox,                  SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));

	// activated(int) fires only on user interaction, not on programmatic model updates, so the ~1Hz refresh from
	// MainWindow can't accidentally re-select anything.
	connect(this->sceneSplatsComboBox,              SIGNAL(activated(int)), this, SLOT(splatComboActivated(int)));

	// SESSION059: toggled(bool) fires on setChecked() too, unlike activated(int) above - setHideCheckboxState() blocks
	// this connection with SignalBlocker while syncing, so a programmatic sync never loops back into
	// splatHideToggledSignal as if the user had clicked it.
	connect(this->hideSplatCheckBox,                SIGNAL(toggled(bool)), this, SLOT(hideCheckBoxToggled(bool)));
}


GaussianSplatSettingsWidget::~GaussianSplatSettingsWidget()
{
}


void GaussianSplatSettingsWidget::init(QSettings* settings_)
{
	// NOTE: this->settings is deliberately left null until the end of this function, and settings_ read directly below.
	// setValue()/setChecked() emit their change signals, which reach settingsChanged(), which writes *every* key from
	// the current widget values - and during this function most widgets are still at the defaults baked into the .ui
	// file.  With this->settings already set, the first control initialised would overwrite the saved values of all the
	// ones after it with those .ui defaults, and the reads below would then return what had just been clobbered rather
	// than what the user last chose.  settingsChanged() does nothing while this->settings is null, so the whole
	// function reads cleanly and the first real write is the user's next edit.
	//
	// Defaults here match GaussianSplatRenderer's own hardcoded defaults (pixel_scale_limit, max_splats_budget,
	// resort_move_threshold_ws) and buildGaussianSplatLodTree()'s default lod_base, so a settings store with no saved
	// values yet reproduces the same behaviour as before this widget existed.
	this->pixelScaleLimitDoubleSpinBox->setValue(settings_->value("gaussian_splats/pixel_scale_limit", 2.0).toDouble()); // SESSION072
	this->adaptivePixelScaleCheckBox->setChecked(settings_->value("gaussian_splats/adaptive_pixel_scale", false).toBool()); // SESSION079
	this->adaptiveTargetFPSDoubleSpinBox->setValue(settings_->value("gaussian_splats/adaptive_target_fps", 50.0).toDouble()); // SESSION079
	this->alphaGainDoubleSpinBox->setValue(settings_->value("gaussian_splats/alpha_gain", 1.0).toDouble());   // 1 and 1 = the stored alpha untouched, see GaussianSplatRenderer::getAlphaGain().
	this->alphaGammaDoubleSpinBox->setValue(settings_->value("gaussian_splats/alpha_gamma", 1.0).toDouble());
	this->alphaAdjustIgnoreCheckBox->setChecked(true); // SESSION072 default on. Deliberately not persisted, same as the debug view below: it is an A/B switch, and a session starting with the saved gain/gamma silently bypassed would read as them not working.
	this->maxSplatsBudgetSpinBox->setValue(settings_->value("gaussian_splats/max_splats_budget", 10000000).toInt());
	this->resortMoveThresholdDoubleSpinBox->setValue(settings_->value("gaussian_splats/resort_move_threshold_ws", 0.1).toDouble());
	this->lodBaseDoubleSpinBox->setValue(settings_->value("gaussian_splats/lod_base", 1.5).toDouble());
	this->sizeClampMinDoubleSpinBox->setValue(settings_->value("gaussian_splats/size_clamp_min", 0.0).toDouble());
	this->sizeClampMaxDoubleSpinBox->setValue(settings_->value("gaussian_splats/size_clamp_max", 0.0).toDouble());
	this->sizeClampInvertCheckBox->setChecked(settings_->value("gaussian_splats/size_clamp_invert", false).toBool());
	this->ewaProjectionFixCheckBox->setChecked(settings_->value("gaussian_splats/ewa_projection_fix", true).toBool());
	this->nearFadeWidthDoubleSpinBox->setValue(settings_->value("gaussian_splats/near_fade_width", 0.3).toDouble());
	this->alphaCutoffDoubleSpinBox->setValue(settings_->value("gaussian_splats/alpha_cutoff", 0.0201).toDouble()); // SESSION072: was 1/255 (lossless); 0.0201 owner-corrected value.
	this->layerCapSpinBox->setValue(settings_->value("gaussian_splats/layer_cap", 0).toInt()); // 0 = uncapped, i.e. the pass as it was before this existed.
	this->layerCapOpaqueCheckBox->setChecked(settings_->value("gaussian_splats/layer_cap_opaque", true).toBool());
	this->layerCapOnCheckBox->setChecked(false); // Deliberately not persisted, like the other A/B switches: a session starting with a cap silently applied would look like broken LoD.
	this->coverageCapThresholdDoubleSpinBox->setValue(settings_->value("gaussian_splats/coverage_cap_threshold", 0.96).toDouble()); // SESSION072. ~0.96 coverage is about 3 units of summed alpha, see GaussianSplatRenderer::getCoverageCap(). Widget: "Alpha saturation cap" row.
	this->coverageCapOnCheckBox->setChecked(false); // Not persisted, for the same reason as the layer cap's tick above.
	this->ablationStageComboBox->setCurrentIndex(0); // Not persisted: every stage but 0 draws a deliberately incomplete picture, and finding one still selected after a restart would read as a broken scene.
	this->quadRadiusScaleDoubleSpinBox->setValue(1.0); // Not persisted either, and for the same reason: anything but 1 is a deliberately wrong picture.
	this->pointSizePxDoubleSpinBox->setValue(1.0); // SESSION071: not persisted, same reason - it is a measurement aid for the ablation stages, not a picture setting.
	this->areaScaleGammaDoubleSpinBox->setValue(1.0); // Not persisted, same reason - inert while quad radius scale is 1, but kept off by default like the rest of this row.
	this->areaScaleRefPxDoubleSpinBox->setValue(20.0);
	this->hideTestComboBox->setCurrentIndex(settings_->value("gaussian_splats/hide_test_centre", false).toBool() ? 1 : 0); // Conservative by default: it is the only one of the two that leaves a picture.
	this->drawSliceLimitSpinBox->setValue(0); // Deliberately not persisted, like the debug views: it draws an incomplete frame, and a session starting with it on would look like broken LoD.
	// Still not persisted, for the same reason as the shrink mode below: an A/B is only honest if both sessions start
	// from the same placement. On rather than off now, though - it puts the slice boundaries where the censuses have
	// something to find, which is what everything riding on the saturation gate depends on.
	this->visibleSlicingCheckBox->setChecked(true);
	this->showDebugCheckBox->setChecked(false); // Deliberately not persisted - a momentary debug view, not a preference; starting a session with it silently on would be confusing.
	// SESSION072: console log toggles - deliberately not persisted, for the same reason as the debug view above. They
	// cost real frame time while on (measured - main-thread conPrint(), flushed per line, firing every frame during
	// motion), so a session should never silently start already paying for logging nobody asked to see.
	this->filterLogCheckBox->setChecked(false);
	this->kickLogCheckBox->setChecked(false);
	this->profLogCheckBox->setChecked(false);
	// SESSION074/075: not persisted, same reasoning as the log checkboxes just above - a session should always start
	// with the stage off, not silently resume mid-measurement from a previous session's state.
	this->saturationFilterCheckBox->setChecked(false); // off.
	this->satPrefilterThresholdDoubleSpinBox->setValue(settings_->value("gaussian_splats/sat_prefilter_threshold", 0.98).toDouble()); // SESSION079: this stage's own threshold, persisted independently of the gate's below.
	this->satDiagCheckBox->setChecked(false); // off. SESSION076 - measurement mode, deliberately not persisted (same reason as the row's own checkbox above).
	this->satGridSubdivDoubleSpinBox->setValue(settings_->value("gaussian_splats/sat_grid_subdiv", 0.3).toDouble()); // SESSION076 CALIBRATION, SESSION078: persisted, unlike the toggles above - losing a half-found working point on every restart would make the search useless.
	// SESSION078: 0.3 is the owner's own aggressive pick after visually verifying the fix in GaussianSplatSaturationGrid.cpp
	// (the octahedral local-tile-angle correction, see that file) on the session's problem scene (chair back, glasses on
	// table). A coarser grid than this does show small artifacts under close visual inspection - there is more headroom
	// here for someone willing to keep tuning - but the owner judged it a good stopping point and chose not to spend more
	// time on it. Paired with saturation_threshold's 0.99 default below - 0.96 visibly strengthens this grid's artifacts.
	this->satRegionRadiusDoubleSpinBox->setValue(settings_->value("gaussian_splats/sat_region_radius", 0.0).toDouble()); // SESSION078: persisted like sub - 0 is the point-anchored baseline, non-zero is the region assertion.
	this->debugModeComboBox->setCurrentIndex(0); // Overdraw. Not persisted either, for the same reason - it only says which measure the view above shows.
	// Not persisted, like the other A/B switches: both reduce modes have to start a session in the same place or one
	// session's numbers cannot be set beside another's. Min rather than the original mean: mean answers a splat that
	// straddles the edge of a finished region with a number describing neither half, and too high over the unfinished
	// half, which showed as splat-sized rectangles winking in and out of a cushion as the camera turned - session051.
	this->coverageReduceModeComboBox->setCurrentIndex(1);
	this->overdrawRangeMinDoubleSpinBox->setValue(settings_->value("gaussian_splats/overdraw_range_min", 2.0).toDouble());
	this->overdrawRangeMaxDoubleSpinBox->setValue(settings_->value("gaussian_splats/overdraw_range_max", 100.0).toDouble());
	this->maxLayerDensityDoubleSpinBox->setValue(settings_->value("gaussian_splats/max_layer_density", 0.0).toDouble());
	this->maxTreeDepthSpinBox->setValue(settings_->value("gaussian_splats/max_tree_depth", 0).toInt());
	// SESSION075: previously one checkbox drove both flags identically (see MainWindow.cpp's session055/063 comments,
	// now superseded) - split into two independent settings. The old "frustum_cull" key is kept for the "cull"
	// checkbox, the setting it's the closer continuation of; "split_pipeline" is new, with its own default (true).
	this->cullCheckBox->setChecked(settings_->value("gaussian_splats/frustum_cull", true).toBool());
	this->splitPipelineCheckBox->setChecked(settings_->value("gaussian_splats/split_pipeline", true).toBool());
	this->filterDilationLatencyDoubleSpinBox->setValue(settings_->value("gaussian_splats/filter_dilation_latency", 0.2).toDouble()); // SESSION063 K3, SESSION072: matches measured kick-to-drain round trip; SESSION079: 0.17->0.2, a list is on screen from its own kick until the NEXT drain, so the envelope is ~2x the 85ms round trip measured over the forest.
	this->filterMinRotRateDoubleSpinBox->setValue(settings_->value("gaussian_splats/filter_min_rot_rate", 50.0).toDouble()); // SESSION072; SESSION079: 10->50, the band a STANDING camera carries, which is what a sharp turn tears through before the first rotating kick lands.
	this->filterMaxRotRateDoubleSpinBox->setValue(settings_->value("gaussian_splats/filter_max_rot_rate", 200.0).toDouble()); // SESSION071; SESSION079: 40->200. At 40 the band pins to 8deg while the camera turns at 350, and over the forest the applied list went 31deg stale against a 16deg band - a visible hole along the frustum edge. The old default was confirmed on the interior, the one scene where this knob never binds.
	this->filterMinTransRateDoubleSpinBox->setValue(settings_->value("gaussian_splats/filter_min_trans_rate", 10.0).toDouble()); // SESSION072
	this->energyMergeColourCheckBox->setChecked(settings_->value("gaussian_splats/energy_merge_colour", true).toBool()); // SESSION071
	this->mergeSpreadWidenDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_spread_widen", 3.0).toDouble()); // SESSION071
	this->coarseFloorCheckBox->setChecked(settings_->value("gaussian_splats/coarse_floor", true).toBool()); // SESSION063 K4
	this->coarsePixelScaleDoubleSpinBox->setValue(settings_->value("gaussian_splats/coarse_pixel_scale", 25.0).toDouble()); // SESSION072
	this->coarseDilationLatencyDoubleSpinBox->setValue(settings_->value("gaussian_splats/coarse_dilation_latency", 0.9).toDouble());
	this->numDrawSlicesSpinBox->setValue(settings_->value("gaussian_splats/num_draw_slices", 6).toInt()); // SESSION072
	this->sliceGrowthDoubleSpinBox->setValue(settings_->value("gaussian_splats/slice_growth", 1.3).toDouble()); // SESSION072
	this->saturationGateCheckBox->setChecked(settings_->value("gaussian_splats/saturation_gate", true).toBool()); // SESSION072
	this->saturationThresholdDoubleSpinBox->setValue(settings_->value("gaussian_splats/saturation_threshold", 0.99).toDouble()); // SESSION078: raised from 0.96 - see sat_grid_subdiv's comment above, paired with its 0.3 default.
	// A threshold of 0 would mark every pixel as finished the moment the gate ran, so it cannot be a value anyone chose.
	// It is what the bug described above wrote into existing settings stores before it was fixed; treat it as unset.
	if(this->saturationThresholdDoubleSpinBox->value() <= 0.0)
		this->saturationThresholdDoubleSpinBox->setValue(0.99);
	this->saturationMaskDownscaleSpinBox->setValue(settings_->value("gaussian_splats/saturation_mask_downscale", 4).toInt());
	// Not persisted, deliberately: a fixed starting point is what makes one session's measurements comparable with the
	// next one's. The value is the box's mode 0 entry only for the moment it takes the line below to switch modes, which
	// parks it and brings mode 1's own in - see coverageShrinkModeChanged().
	this->coverageShrinkStrengthDoubleSpinBox->setValue(0.0);

	// The mode the measurements picked, engaged at the budget they picked. Unlike the shrink's own historical default of
	// off, this one starts on.
	//
	// Mode 2 rather than session050's mode 1: mode 1 at 0.02 was measured at 17.1 ms against 22.2 ms with no shrink at
	// all, but it bought that by removing whole splats over well-covered pixels rather than trimming their edges, which
	// froze the pixel short of full coverage and read as holes onto the floor that flickered as the camera turned. Mode
	// 2 bounds the threshold, so the knob can be turned five times further before it costs anything visible: at 0.1 it
	// is about 16 ms with no artefacts, i.e. cheaper than mode 1 ever managed and correct as well - session051.
	this->coverageShrinkModeComboBox->setCurrentIndex(2);
	this->accumBuffer8BitCheckBox->setChecked(settings_->value("gaussian_splats/accum_buffer_8bit", false).toBool());
	// SESSION070 - not persisted, deliberately (a session should always start from a known, comparable point) - but the
	// starting point itself is now the owner's settled working point rather than "everything off": 0.5 buffer scale with
	// deconvolution + RCAS + TAA all on, at the gain/sharpness values the owner converged on across session069/070. The
	// bilinear switch rides along with it.
	this->accumBufferScaleDoubleSpinBox->setValue(0.5);
	this->accumUpsampleModeComboBox->setCurrentIndex(1); // Bilinear - see the combobox's item tooltips.
	// Likewise not persisted - a slice left on would silently make the next session's frame a fraction of the cloud.
	this->areaSliceModeComboBox->setCurrentIndex(0);
	this->areaSlicePxDoubleSpinBox->setValue(256.0);
	// SESSION068/070 - post-processing enhancers.  Not persisted, for the same reason the buffer scale above is not -
	// but on by default now, at the owner's settled values (session069 §2, session070).
	this->deconvEnabledCheckBox->setChecked(true);
	this->deconvGainDoubleSpinBox->setValue(3.0);
	this->rcasEnabledCheckBox->setChecked(true);
	this->rcasSharpnessDoubleSpinBox->setValue(0.6);
	// SESSION069/070 - TAA on by default now too, same reasoning as the enhancers above.
	this->taaEnabledCheckBox->setChecked(true);
	// SESSION072: weighted (index 2) by default - the owner's settled DoF mode, smooth by construction with no
	// silhouette steps (see the combo box's own tooltip). Was off (0), matching upstream behaviour, before this.
	{
		const int stored = settings_->value("gaussian_splats/dof_depth_mode", 2).toInt();
		this->dofDepthModeComboBox->setCurrentIndex((stored < 0) ? 0 : ((stored > 2) ? 2 : stored));
	}
	this->clipCheckBox->setChecked(false); // Deliberately not persisted: it removes splats from the picture, and finding it still on after a restart would read as the scene having lost geometry.
	// The distance slice is deliberately not persisted, for the same reason as the overdraw view: it is a momentary way of
	// looking into a capture, not a preference, and a session that silently started with half the cloud missing would read
	// as a broken scene rather than as a setting left on.
	this->distClampEnabledCheckBox->setChecked(false); // SESSION072: same reasoning as the fields below - not persisted, a session should never silently start with part of the cloud pruned.
	this->distClampMinDoubleSpinBox->setValue(0.0);
	this->distClampMaxDoubleSpinBox->setValue(10000.0); // SESSION072: was 1000.
	this->distClampInvertCheckBox->setChecked(false);
	// Merge tolerances are persisted - unlike the slice, leaving one set has no effect on what is drawn, only on what the
	// report says, and a sweep is easier to carry across sessions than to retype.
	this->mergeColourTolDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_colour_tol", 0.15).toDouble()); // SESSION072
	this->mergeAngleTolDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_angle_tol_deg", 20.0).toDouble()); // SESSION072
	// The reach the "Merge coplanar" button works to, persisted for the same reason: it changes nothing until the button
	// is pressed, and a sweep across settings is easier to carry between sessions than to retype.
	this->mergeAcrossDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_across_cm", 1.0).toDouble());
	this->mergeThroughDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_through_cm", 8.0).toDouble()); // SESSION072
	this->mergeFlattenCheckBox->setChecked(settings_->value("gaussian_splats/merge_flatten", true).toBool());

	this->settings = settings_; // Last, so that none of the above wrote anything - see the note at the top of this function.
}


void GaussianSplatSettingsWidget::coverageShrinkModeChanged(int mode)
{
	if((mode < 0) || (mode >= (int)(sizeof(coverage_shrink_value_for_mode) / sizeof(coverage_shrink_value_for_mode[0]))) || (mode == coverage_shrink_prev_mode))
		return;

	// Park the value the box holds under the mode it was tuned for, and bring back the other mode's own. Without this
	// the same number would carry across a mode change and mean something else on the other side, which is exactly the
	// comparison this control exists to make honest - see GaussianSplatRenderer::getCoverageShrinkMode().
	coverage_shrink_value_for_mode[coverage_shrink_prev_mode] = this->coverageShrinkStrengthDoubleSpinBox->value();
	coverage_shrink_prev_mode = mode;

	// The label and the step go with the value: in mode 1 the number is a budget on light lost, which lives down where
	// the alpha cutoff does (a hundredth is already a visible amount of light), not a shrink factor spanning 0 to 1.
	// Mode 2 is mode 1's number with the amplification taken out, so it wants the same units, range and step - and the
	// same label, since it is still a budget on light. Only mode 0 differs.
	this->coverageShrinkStrengthDoubleSpinBox->setPrefix((mode == 0) ? "shrink " : "loss ");
	this->coverageShrinkStrengthDoubleSpinBox->setDecimals((mode == 0) ? 2 : 3);
	this->coverageShrinkStrengthDoubleSpinBox->setSingleStep((mode == 0) ? 0.05 : 0.005);
	this->coverageShrinkStrengthDoubleSpinBox->setValue(coverage_shrink_value_for_mode[mode]);
}


void GaussianSplatSettingsWidget::settingsChanged()
{
	if(settings)
	{
		settings->setValue("gaussian_splats/pixel_scale_limit", this->pixelScaleLimitDoubleSpinBox->value());
		settings->setValue("gaussian_splats/adaptive_pixel_scale", this->adaptivePixelScaleCheckBox->isChecked()); // SESSION079
		settings->setValue("gaussian_splats/adaptive_target_fps", this->adaptiveTargetFPSDoubleSpinBox->value()); // SESSION079
		settings->setValue("gaussian_splats/layer_cap", this->layerCapSpinBox->value());
		settings->setValue("gaussian_splats/layer_cap_opaque", this->layerCapOpaqueCheckBox->isChecked());
		settings->setValue("gaussian_splats/hide_test_centre", this->hideTestComboBox->currentIndex() == 1);
		settings->setValue("gaussian_splats/alpha_gain", this->alphaGainDoubleSpinBox->value());
		settings->setValue("gaussian_splats/alpha_gamma", this->alphaGammaDoubleSpinBox->value());
		settings->setValue("gaussian_splats/max_splats_budget", this->maxSplatsBudgetSpinBox->value());
		settings->setValue("gaussian_splats/resort_move_threshold_ws", this->resortMoveThresholdDoubleSpinBox->value());
		settings->setValue("gaussian_splats/lod_base", this->lodBaseDoubleSpinBox->value());
		settings->setValue("gaussian_splats/size_clamp_min", this->sizeClampMinDoubleSpinBox->value());
		settings->setValue("gaussian_splats/size_clamp_max", this->sizeClampMaxDoubleSpinBox->value());
		settings->setValue("gaussian_splats/size_clamp_invert", this->sizeClampInvertCheckBox->isChecked());
		settings->setValue("gaussian_splats/ewa_projection_fix", this->ewaProjectionFixCheckBox->isChecked());
		settings->setValue("gaussian_splats/near_fade_width", this->nearFadeWidthDoubleSpinBox->value());
		settings->setValue("gaussian_splats/alpha_cutoff", this->alphaCutoffDoubleSpinBox->value());
		settings->setValue("gaussian_splats/overdraw_range_min", this->overdrawRangeMinDoubleSpinBox->value());
		settings->setValue("gaussian_splats/overdraw_range_max", this->overdrawRangeMaxDoubleSpinBox->value());
		settings->setValue("gaussian_splats/max_layer_density", this->maxLayerDensityDoubleSpinBox->value());
		settings->setValue("gaussian_splats/max_tree_depth", this->maxTreeDepthSpinBox->value());
		settings->setValue("gaussian_splats/frustum_cull", this->cullCheckBox->isChecked()); // SESSION075: was frustumCullCheckBox.
		settings->setValue("gaussian_splats/split_pipeline", this->splitPipelineCheckBox->isChecked()); // SESSION075
		settings->setValue("gaussian_splats/filter_dilation_latency", this->filterDilationLatencyDoubleSpinBox->value()); // SESSION063 K3
		settings->setValue("gaussian_splats/filter_min_rot_rate", this->filterMinRotRateDoubleSpinBox->value());
		settings->setValue("gaussian_splats/filter_max_rot_rate", this->filterMaxRotRateDoubleSpinBox->value()); // SESSION071
		settings->setValue("gaussian_splats/filter_min_trans_rate", this->filterMinTransRateDoubleSpinBox->value());
		settings->setValue("gaussian_splats/energy_merge_colour", this->energyMergeColourCheckBox->isChecked()); // SESSION071
		settings->setValue("gaussian_splats/merge_spread_widen", this->mergeSpreadWidenDoubleSpinBox->value()); // SESSION071
		settings->setValue("gaussian_splats/coarse_floor", this->coarseFloorCheckBox->isChecked()); // SESSION063 K4
		settings->setValue("gaussian_splats/coarse_pixel_scale", this->coarsePixelScaleDoubleSpinBox->value());
		settings->setValue("gaussian_splats/sat_grid_subdiv", this->satGridSubdivDoubleSpinBox->value()); // SESSION076 CALIBRATION
		settings->setValue("gaussian_splats/sat_region_radius", this->satRegionRadiusDoubleSpinBox->value()); // SESSION078
		settings->setValue("gaussian_splats/sat_prefilter_threshold", this->satPrefilterThresholdDoubleSpinBox->value()); // SESSION079
		settings->setValue("gaussian_splats/coarse_dilation_latency", this->coarseDilationLatencyDoubleSpinBox->value());
		settings->setValue("gaussian_splats/num_draw_slices", this->numDrawSlicesSpinBox->value());
		settings->setValue("gaussian_splats/slice_growth", this->sliceGrowthDoubleSpinBox->value());
		settings->setValue("gaussian_splats/saturation_gate", this->saturationGateCheckBox->isChecked());
		settings->setValue("gaussian_splats/saturation_threshold", this->saturationThresholdDoubleSpinBox->value());
		settings->setValue("gaussian_splats/saturation_mask_downscale", this->saturationMaskDownscaleSpinBox->value());
		settings->setValue("gaussian_splats/coverage_cap_threshold", this->coverageCapThresholdDoubleSpinBox->value());
		settings->setValue("gaussian_splats/accum_buffer_8bit", this->accumBuffer8BitCheckBox->isChecked());
		settings->setValue("gaussian_splats/dof_depth_mode", this->dofDepthModeComboBox->currentIndex());
		settings->setValue("gaussian_splats/merge_colour_tol", this->mergeColourTolDoubleSpinBox->value());
		settings->setValue("gaussian_splats/merge_angle_tol_deg", this->mergeAngleTolDoubleSpinBox->value());
		settings->setValue("gaussian_splats/merge_across_cm", this->mergeAcrossDoubleSpinBox->value());
		settings->setValue("gaussian_splats/merge_through_cm", this->mergeThroughDoubleSpinBox->value());
		settings->setValue("gaussian_splats/merge_flatten", this->mergeFlattenCheckBox->isChecked());
	}

	emit settingsChangedSignal();
}


void GaussianSplatSettingsWidget::splatComboActivated(int index)
{
	if(index < 0) return;
	const QVariant item_data = this->sceneSplatsComboBox->itemData(index);
	if(!item_data.isValid()) return;
	emit splatSelectedSignal(item_data.toULongLong());
}


void GaussianSplatSettingsWidget::hideCheckBoxToggled(bool checked)
{
	const int idx = this->sceneSplatsComboBox->currentIndex();
	if(idx < 0) return;
	const QVariant item_data = this->sceneSplatsComboBox->itemData(idx);
	if(!item_data.isValid()) return;
	emit splatHideToggledSignal(item_data.toULongLong(), checked);
}


void GaussianSplatSettingsWidget::setHideCheckboxState(bool hidden)
{
	SignalBlocker blocker(this->hideSplatCheckBox); // Don't let this programmatic sync re-emit splatHideToggledSignal - see the .h comment.
	this->hideSplatCheckBox->setChecked(hidden);
}


void GaussianSplatSettingsWidget::setSplatList(const std::vector<std::pair<uint64_t, std::string>>& items)
{
	// Don't disturb the visible popup while the user is picking.
	if(this->sceneSplatsComboBox->view()->isVisible())
		return;

	// Fast path: same list as last time - skip rebuild to avoid needless UI churn.
	if((int)items.size() == this->sceneSplatsComboBox->count())
	{
		bool same = true;
		for(size_t i = 0; i < items.size(); ++i)
		{
			if(this->sceneSplatsComboBox->itemData((int)i).toULongLong() != (qulonglong)items[i].first)
			{
				same = false;
				break;
			}
		}
		if(same) return;
	}

	// Preserve current selection by UID if still present.
	const int prev_idx = this->sceneSplatsComboBox->currentIndex();
	const qulonglong prev_uid = (prev_idx >= 0) ? this->sceneSplatsComboBox->itemData(prev_idx).toULongLong() : 0;

	SignalBlocker blocker(this->sceneSplatsComboBox);
	this->sceneSplatsComboBox->clear();
	int restore_idx = -1;
	for(size_t i = 0; i < items.size(); ++i)
	{
		this->sceneSplatsComboBox->addItem(QtUtils::toQString(items[i].second), (qulonglong)items[i].first);
		if((qulonglong)items[i].first == prev_uid && prev_uid != 0)
			restore_idx = (int)i;
	}
	this->sceneSplatsComboBox->setCurrentIndex(restore_idx);
}


// SESSION072: "Reset to default" - every control back to its shipped default. Deliberately a standalone list rather
// than routed through init()'s settings_->value(key, default) reads (contract §2: additive, not a rewrite of proven
// init() code) - the literals below must be kept in sync with init()'s defaults by hand.
//
// Setting each control fires its own valueChanged/toggled signal -> settingsChanged(), which persists the new
// (default) value to QSettings same as any other edit - so this is a real reset, not just a visual one; the next
// session starts from defaults too, until the user changes something again.
void GaussianSplatSettingsWidget::resetToDefaultsClicked()
{
	this->pixelScaleLimitDoubleSpinBox->setValue(2.0);
	this->adaptivePixelScaleCheckBox->setChecked(false); // SESSION079
	this->adaptiveTargetFPSDoubleSpinBox->setValue(50.0); // SESSION079
	this->alphaGainDoubleSpinBox->setValue(1.0);
	this->alphaGammaDoubleSpinBox->setValue(1.0);
	this->alphaAdjustIgnoreCheckBox->setChecked(true);
	this->maxSplatsBudgetSpinBox->setValue(10000000);
	this->resortMoveThresholdDoubleSpinBox->setValue(0.1);
	this->lodBaseDoubleSpinBox->setValue(1.5);
	this->sizeClampMinDoubleSpinBox->setValue(0.0);
	this->sizeClampMaxDoubleSpinBox->setValue(0.0);
	this->sizeClampInvertCheckBox->setChecked(false);
	this->ewaProjectionFixCheckBox->setChecked(true);
	this->nearFadeWidthDoubleSpinBox->setValue(0.3);
	this->alphaCutoffDoubleSpinBox->setValue(0.0201);
	this->layerCapSpinBox->setValue(0);
	this->layerCapOpaqueCheckBox->setChecked(true);
	this->layerCapOnCheckBox->setChecked(false);
	this->coverageCapThresholdDoubleSpinBox->setValue(0.96);
	this->coverageCapOnCheckBox->setChecked(false);
	this->ablationStageComboBox->setCurrentIndex(0);
	this->quadRadiusScaleDoubleSpinBox->setValue(1.0);
	this->pointSizePxDoubleSpinBox->setValue(1.0);
	this->areaScaleGammaDoubleSpinBox->setValue(1.0);
	this->areaScaleRefPxDoubleSpinBox->setValue(20.0);
	this->hideTestComboBox->setCurrentIndex(0);
	this->drawSliceLimitSpinBox->setValue(0);
	this->visibleSlicingCheckBox->setChecked(true);
	this->showDebugCheckBox->setChecked(false);
	this->filterLogCheckBox->setChecked(false);
	this->kickLogCheckBox->setChecked(false);
	this->profLogCheckBox->setChecked(false);
	this->saturationFilterCheckBox->setChecked(false); // off. SESSION074/075 - see load()'s comment.
	this->satPrefilterThresholdDoubleSpinBox->setValue(0.98); // SESSION079 - see load()'s comment.
	this->satDiagCheckBox->setChecked(false); // off. SESSION076 - see load()'s comment.
	this->satGridSubdivDoubleSpinBox->setValue(0.3); // SESSION076 CALIBRATION, SESSION078: see load()'s comment.
	this->satRegionRadiusDoubleSpinBox->setValue(0.0); // SESSION078: 0 = point-anchored, the pre-region behaviour.
	this->debugModeComboBox->setCurrentIndex(0);
	this->coverageReduceModeComboBox->setCurrentIndex(1);
	this->overdrawRangeMinDoubleSpinBox->setValue(2.0);
	this->overdrawRangeMaxDoubleSpinBox->setValue(100.0);
	this->maxLayerDensityDoubleSpinBox->setValue(0.0);
	this->maxTreeDepthSpinBox->setValue(0);
	this->cullCheckBox->setChecked(true); // SESSION075: was frustumCullCheckBox.
	this->splitPipelineCheckBox->setChecked(true); // SESSION075
	this->filterDilationLatencyDoubleSpinBox->setValue(0.2); // SESSION079
	this->filterMinRotRateDoubleSpinBox->setValue(50.0); // SESSION079
	this->filterMaxRotRateDoubleSpinBox->setValue(200.0); // SESSION079
	this->filterMinTransRateDoubleSpinBox->setValue(10.0);
	this->energyMergeColourCheckBox->setChecked(true);
	this->mergeSpreadWidenDoubleSpinBox->setValue(3.0);
	this->coarseFloorCheckBox->setChecked(true);
	this->coarsePixelScaleDoubleSpinBox->setValue(25.0);
	this->coarseDilationLatencyDoubleSpinBox->setValue(0.9);
	this->numDrawSlicesSpinBox->setValue(6);
	this->sliceGrowthDoubleSpinBox->setValue(1.3);
	this->saturationGateCheckBox->setChecked(true);
	this->saturationThresholdDoubleSpinBox->setValue(0.99); // SESSION078: see load()'s comment.
	this->saturationMaskDownscaleSpinBox->setValue(4);
	// Bypasses coverageShrinkModeChanged()'s signal-driven park/restore (which is a no-op when the box is already at
	// index 2, leaving a stale value) - set the mode's remembered array and the box directly instead, so the result
	// is deterministic regardless of what was selected before the reset.
	this->coverage_shrink_value_for_mode[0] = 0.0;
	this->coverage_shrink_value_for_mode[1] = 0.02;
	this->coverage_shrink_value_for_mode[2] = 0.1;
	this->coverage_shrink_prev_mode = 2;
	this->coverageShrinkModeComboBox->setCurrentIndex(2);
	this->coverageShrinkStrengthDoubleSpinBox->setPrefix("loss ");
	this->coverageShrinkStrengthDoubleSpinBox->setDecimals(3);
	this->coverageShrinkStrengthDoubleSpinBox->setSingleStep(0.005);
	this->coverageShrinkStrengthDoubleSpinBox->setValue(0.1);
	this->accumBuffer8BitCheckBox->setChecked(false);
	this->accumBufferScaleDoubleSpinBox->setValue(0.5);
	this->accumUpsampleModeComboBox->setCurrentIndex(1); // Bilinear - see the combobox's item tooltips.
	this->areaSliceModeComboBox->setCurrentIndex(0);
	this->areaSlicePxDoubleSpinBox->setValue(256.0);
	this->deconvEnabledCheckBox->setChecked(true);
	this->deconvGainDoubleSpinBox->setValue(3.0);
	this->rcasEnabledCheckBox->setChecked(true);
	this->rcasSharpnessDoubleSpinBox->setValue(0.6);
	this->taaEnabledCheckBox->setChecked(true);
	this->dofDepthModeComboBox->setCurrentIndex(2); // weighted
	this->clipCheckBox->setChecked(false);
	this->distClampEnabledCheckBox->setChecked(false);
	this->distClampMinDoubleSpinBox->setValue(0.0);
	this->distClampMaxDoubleSpinBox->setValue(10000.0);
	this->distClampInvertCheckBox->setChecked(false);
	this->mergeColourTolDoubleSpinBox->setValue(0.15);
	this->mergeAngleTolDoubleSpinBox->setValue(20.0);
	this->mergeAcrossDoubleSpinBox->setValue(1.0);
	this->mergeThroughDoubleSpinBox->setValue(8.0);
	this->mergeFlattenCheckBox->setChecked(true);
}


QVariantMap GaussianSplatSettingsWidget::captureAllValues() const
{
	QVariantMap values;
	for(QDoubleSpinBox* w : this->findChildren<QDoubleSpinBox*>())
		values[w->objectName()] = w->value();
	for(QSpinBox* w : this->findChildren<QSpinBox*>())
		values[w->objectName()] = w->value();
	for(QCheckBox* w : this->findChildren<QCheckBox*>())
	{
		if(w == this->hideSplatCheckBox) // Per-selected-object state, not a panel setting - see .h.
			continue;
		values[w->objectName()] = w->isChecked();
	}
	for(QComboBox* w : this->findChildren<QComboBox*>())
	{
		if(w == this->sceneSplatsComboBox || w == this->presetComboBox) // Scene object list / the preset picker itself, not a panel setting.
			continue;
		values[w->objectName()] = w->currentIndex();
	}
	return values;
}


// SESSION072: preset dropdown - all 5 slots start identical to the shipped defaults (nothing saved into them yet).
// Resets to defaults first, then overlays whatever this slot has saved (if anything) - so a control added after a
// preset was last saved still lands on its default rather than being left at whatever the panel happened to show.
void GaussianSplatSettingsWidget::presetSelected(int index)
{
	if(index < 0)
		return;

	resetToDefaultsClicked();

	if(!this->settings)
		return;

	const QString group = QString("gaussian_splats/presets/Preset%1").arg(index + 1);
	this->settings->beginGroup(group);
	const QStringList keys = this->settings->childKeys();
	for(const QString& key : keys)
	{
		QWidget* w = this->findChild<QWidget*>(key);
		if(!w)
			continue;
		const QVariant val = this->settings->value(key);
		if(QDoubleSpinBox* dsb = qobject_cast<QDoubleSpinBox*>(w))
			dsb->setValue(val.toDouble());
		else if(QSpinBox* sb = qobject_cast<QSpinBox*>(w))
			sb->setValue(val.toInt());
		else if(QCheckBox* cb = qobject_cast<QCheckBox*>(w))
			cb->setChecked(val.toBool());
		else if(QComboBox* cmb = qobject_cast<QComboBox*>(w))
			cmb->setCurrentIndex(val.toInt());
	}
	this->settings->endGroup();
}


// SESSION072: "Save" button - writes every control's current value into the selected preset slot, persisted via
// QSettings like the rest of the panel's state (so presets survive an app restart too, not just this session -
// piggy-backing on existing persistence rather than adding a separate store). Doesn't touch any other slot.
void GaussianSplatSettingsWidget::savePresetClicked()
{
	if(!this->settings)
		return;

	const int index = this->presetComboBox->currentIndex();
	if(index < 0)
		return;

	const QString group = QString("gaussian_splats/presets/Preset%1").arg(index + 1);
	this->settings->beginGroup(group);
	this->settings->remove(""); // Clear anything saved previously, so a control that no longer exists can't linger as a stale key.
	const QVariantMap values = captureAllValues();
	for(auto it = values.constBegin(); it != values.constEnd(); ++it)
		this->settings->setValue(it.key(), it.value());
	this->settings->endGroup();
}
