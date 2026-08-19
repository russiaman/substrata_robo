/*=====================================================================
GaussianSplatSettingsWidget.cpp
---------------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GaussianSplatSettingsWidget.h"


#include <QtCore/QSettings>
#include <QtWidgets/QAbstractItemView>
#include <qt/QtUtils.h>
#include "../qt/SignalBlocker.h"


GaussianSplatSettingsWidget::GaussianSplatSettingsWidget(
	QWidget* parent
)
:	settings(NULL),
	coverage_shrink_prev_mode(0)
{
	setupUi(this);

	// Per-mode starting values for the shared shrink box - see coverageShrinkModeChanged(). Mode 0 starts off; mode 1
	// starts at the setting measured to be worth having, which init() then selects - see there.
	coverage_shrink_value_for_mode[0] = 0.0;
	coverage_shrink_value_for_mode[1] = 0.02;
	coverage_shrink_value_for_mode[2] = 0.1; // Mode 2's threshold is bounded, so the same number is far milder in it than in mode 1 - 0.1 is where it was measured to pay, at 16 ms against 22.2 with no shrink, with nothing visible given up.

	connect(this->pixelScaleLimitDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
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
	connect(this->resetImportancePushButton,        SIGNAL(clicked()), this, SIGNAL(resetImportanceRequestedSignal()));
	connect(this->saturationSnapshotsPushButton,    SIGNAL(clicked()), this, SIGNAL(saturationSnapshotsRequestedSignal())); // DIAGNOSTIC ONLY - see GaussianSplatSettingsWidget.h.
	connect(this->showDebugCheckBox,                SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
	connect(this->debugModeComboBox,                SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
	connect(this->clipCheckBox,                     SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
	connect(this->overdrawRangeMinDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->overdrawRangeMaxDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->maxLayerDensityDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->maxTreeDepthSpinBox,              SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->frustumCullCheckBox,              SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->filterDilationLatencyDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K3
	connect(this->filterMinRotRateDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K3
	connect(this->filterMinTransRateDoubleSpinBox,  SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K3
	connect(this->coarseFloorCheckBox,              SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION063 K4
	connect(this->coarsePixelScaleDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K4
	connect(this->coarseDilationLatencyDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged())); // SESSION063 K4
	connect(this->coarseLayerDebugCheckBox,         SIGNAL(toggled(bool)),        this, SLOT(settingsChanged())); // SESSION063 K4
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
	connect(this->quadRadiusScaleDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->areaScaleGammaDoubleSpinBox,      SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->areaScaleRefPxDoubleSpinBox,      SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->hideTestComboBox,                 SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
	connect(this->accumBuffer8BitCheckBox,          SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->dofDepthModeComboBox,             SIGNAL(currentIndexChanged(int)), this, SLOT(settingsChanged()));
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
	this->pixelScaleLimitDoubleSpinBox->setValue(settings_->value("gaussian_splats/pixel_scale_limit", 1.0).toDouble());
	this->alphaGainDoubleSpinBox->setValue(settings_->value("gaussian_splats/alpha_gain", 1.0).toDouble());   // 1 and 1 = the stored alpha untouched, see GaussianSplatRenderer::getAlphaGain().
	this->alphaGammaDoubleSpinBox->setValue(settings_->value("gaussian_splats/alpha_gamma", 1.0).toDouble());
	this->alphaAdjustIgnoreCheckBox->setChecked(false); // Deliberately not persisted, same as the debug view below: it is an A/B switch, and a session starting with the saved gain/gamma silently bypassed would read as them not working.
	this->maxSplatsBudgetSpinBox->setValue(settings_->value("gaussian_splats/max_splats_budget", 10000000).toInt());
	this->resortMoveThresholdDoubleSpinBox->setValue(settings_->value("gaussian_splats/resort_move_threshold_ws", 0.1).toDouble());
	this->lodBaseDoubleSpinBox->setValue(settings_->value("gaussian_splats/lod_base", 1.5).toDouble());
	this->sizeClampMinDoubleSpinBox->setValue(settings_->value("gaussian_splats/size_clamp_min", 0.0).toDouble());
	this->sizeClampMaxDoubleSpinBox->setValue(settings_->value("gaussian_splats/size_clamp_max", 0.0).toDouble());
	this->sizeClampInvertCheckBox->setChecked(settings_->value("gaussian_splats/size_clamp_invert", false).toBool());
	this->ewaProjectionFixCheckBox->setChecked(settings_->value("gaussian_splats/ewa_projection_fix", true).toBool());
	this->nearFadeWidthDoubleSpinBox->setValue(settings_->value("gaussian_splats/near_fade_width", 0.3).toDouble());
	this->alphaCutoffDoubleSpinBox->setValue(settings_->value("gaussian_splats/alpha_cutoff", 1.0 / 255.0).toDouble());
	this->layerCapSpinBox->setValue(settings_->value("gaussian_splats/layer_cap", 0).toInt()); // 0 = uncapped, i.e. the pass as it was before this existed.
	this->layerCapOpaqueCheckBox->setChecked(settings_->value("gaussian_splats/layer_cap_opaque", true).toBool());
	this->layerCapOnCheckBox->setChecked(false); // Deliberately not persisted, like the other A/B switches: a session starting with a cap silently applied would look like broken LoD.
	this->coverageCapThresholdDoubleSpinBox->setValue(settings_->value("gaussian_splats/coverage_cap_threshold", 0.95).toDouble()); // 0.95 coverage = about 3 units of summed alpha, see GaussianSplatRenderer::getCoverageCap().
	this->coverageCapOnCheckBox->setChecked(false); // Not persisted, for the same reason as the layer cap's tick above.
	this->ablationStageComboBox->setCurrentIndex(0); // Not persisted: every stage but 0 draws a deliberately incomplete picture, and finding one still selected after a restart would read as a broken scene.
	this->quadRadiusScaleDoubleSpinBox->setValue(1.0); // Not persisted either, and for the same reason: anything but 1 is a deliberately wrong picture.
	this->areaScaleGammaDoubleSpinBox->setValue(1.0); // Not persisted, same reason - inert while quad radius scale is 1, but kept off by default like the rest of this row.
	this->areaScaleRefPxDoubleSpinBox->setValue(20.0);
	this->hideTestComboBox->setCurrentIndex(settings_->value("gaussian_splats/hide_test_centre", false).toBool() ? 1 : 0); // Conservative by default: it is the only one of the two that leaves a picture.
	this->drawSliceLimitSpinBox->setValue(0); // Deliberately not persisted, like the debug views: it draws an incomplete frame, and a session starting with it on would look like broken LoD.
	// Still not persisted, for the same reason as the shrink mode below: an A/B is only honest if both sessions start
	// from the same placement. On rather than off now, though - it puts the slice boundaries where the censuses have
	// something to find, which is what everything riding on the saturation gate depends on.
	this->visibleSlicingCheckBox->setChecked(true);
	this->showDebugCheckBox->setChecked(false); // Deliberately not persisted - a momentary debug view, not a preference; starting a session with it silently on would be confusing.
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
	this->frustumCullCheckBox->setChecked(settings_->value("gaussian_splats/frustum_cull", true).toBool()); // SESSION055 - see GaussianSplatRenderer::setFrustumCullEnabled(). SESSION063: also drives the split filter path.
	this->filterDilationLatencyDoubleSpinBox->setValue(settings_->value("gaussian_splats/filter_dilation_latency", 0.06).toDouble()); // SESSION063 K3
	this->filterMinRotRateDoubleSpinBox->setValue(settings_->value("gaussian_splats/filter_min_rot_rate", 45.0).toDouble());
	this->filterMinTransRateDoubleSpinBox->setValue(settings_->value("gaussian_splats/filter_min_trans_rate", 2.0).toDouble());
	this->coarseFloorCheckBox->setChecked(settings_->value("gaussian_splats/coarse_floor", true).toBool()); // SESSION063 K4
	this->coarsePixelScaleDoubleSpinBox->setValue(settings_->value("gaussian_splats/coarse_pixel_scale", 30.0).toDouble());
	this->coarseDilationLatencyDoubleSpinBox->setValue(settings_->value("gaussian_splats/coarse_dilation_latency", 0.9).toDouble());
	this->numDrawSlicesSpinBox->setValue(settings_->value("gaussian_splats/num_draw_slices", 1).toInt());
	this->sliceGrowthDoubleSpinBox->setValue(settings_->value("gaussian_splats/slice_growth", 1.0).toDouble());
	this->saturationGateCheckBox->setChecked(settings_->value("gaussian_splats/saturation_gate", false).toBool());
	this->saturationThresholdDoubleSpinBox->setValue(settings_->value("gaussian_splats/saturation_threshold", 0.96).toDouble());
	// A threshold of 0 would mark every pixel as finished the moment the gate ran, so it cannot be a value anyone chose.
	// It is what the bug described above wrote into existing settings stores before it was fixed; treat it as unset.
	if(this->saturationThresholdDoubleSpinBox->value() <= 0.0)
		this->saturationThresholdDoubleSpinBox->setValue(0.96);
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
	// Off by default, matching upstream behaviour: splats don't write depth, so DoF blurs them as it always has.
	// The two other modes both change how splats look under DoF/fog and should be an opt-in for new installs.
	{
		const int stored = settings_->value("gaussian_splats/dof_depth_mode", 0).toInt();
		this->dofDepthModeComboBox->setCurrentIndex((stored < 0) ? 0 : ((stored > 2) ? 2 : stored));
	}
	this->clipCheckBox->setChecked(false); // Deliberately not persisted: it removes splats from the picture, and finding it still on after a restart would read as the scene having lost geometry.
	// The distance slice is deliberately not persisted, for the same reason as the overdraw view: it is a momentary way of
	// looking into a capture, not a preference, and a session that silently started with half the cloud missing would read
	// as a broken scene rather than as a setting left on.
	this->distClampMinDoubleSpinBox->setValue(0.0);
	this->distClampMaxDoubleSpinBox->setValue(1000.0);
	this->distClampInvertCheckBox->setChecked(false);
	// Merge tolerances are persisted - unlike the slice, leaving one set has no effect on what is drawn, only on what the
	// report says, and a sweep is easier to carry across sessions than to retype.
	this->mergeColourTolDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_colour_tol", 0.1).toDouble());
	this->mergeAngleTolDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_angle_tol_deg", 26.0).toDouble());
	// The reach the "Merge coplanar" button works to, persisted for the same reason: it changes nothing until the button
	// is pressed, and a sweep across settings is easier to carry between sessions than to retype.
	this->mergeAcrossDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_across_cm", 1.0).toDouble());
	this->mergeThroughDoubleSpinBox->setValue(settings_->value("gaussian_splats/merge_through_cm", 5.0).toDouble());
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
		settings->setValue("gaussian_splats/frustum_cull", this->frustumCullCheckBox->isChecked());
		settings->setValue("gaussian_splats/filter_dilation_latency", this->filterDilationLatencyDoubleSpinBox->value()); // SESSION063 K3
		settings->setValue("gaussian_splats/filter_min_rot_rate", this->filterMinRotRateDoubleSpinBox->value());
		settings->setValue("gaussian_splats/filter_min_trans_rate", this->filterMinTransRateDoubleSpinBox->value());
		settings->setValue("gaussian_splats/coarse_floor", this->coarseFloorCheckBox->isChecked()); // SESSION063 K4
		settings->setValue("gaussian_splats/coarse_pixel_scale", this->coarsePixelScaleDoubleSpinBox->value());
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
