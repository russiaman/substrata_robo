/*=====================================================================
GaussianSplatSettingsWidget.cpp
---------------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GaussianSplatSettingsWidget.h"


#include <QtCore/QSettings>


GaussianSplatSettingsWidget::GaussianSplatSettingsWidget(
	QWidget* parent
)
:	settings(NULL)
{
	setupUi(this);

	connect(this->pixelScaleLimitDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->maxSplatsBudgetSpinBox,           SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->resortMoveThresholdDoubleSpinBox, SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->lodBaseDoubleSpinBox,             SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->sizeClampMinDoubleSpinBox,        SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->sizeClampMaxDoubleSpinBox,        SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->sizeClampInvertCheckBox,          SIGNAL(toggled(bool)),        this, SLOT(settingsChanged()));
	connect(this->alphaCutoffDoubleSpinBox,         SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->countInFrustumPushButton,         SIGNAL(clicked()), this, SIGNAL(countInFrustumRequestedSignal()));
	connect(this->showOverdrawCheckBox,             SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
	connect(this->overdrawSumAlphaCheckBox,         SIGNAL(toggled(bool)), this, SLOT(settingsChanged()));
	connect(this->overdrawRangeMinDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->overdrawRangeMaxDoubleSpinBox,    SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->maxLayerDensityDoubleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->maxTreeDepthSpinBox,              SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->numDrawSlicesSpinBox,             SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
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
	this->maxSplatsBudgetSpinBox->setValue(settings_->value("gaussian_splats/max_splats_budget", 10000000).toInt());
	this->resortMoveThresholdDoubleSpinBox->setValue(settings_->value("gaussian_splats/resort_move_threshold_ws", 0.1).toDouble());
	this->lodBaseDoubleSpinBox->setValue(settings_->value("gaussian_splats/lod_base", 1.5).toDouble());
	this->sizeClampMinDoubleSpinBox->setValue(settings_->value("gaussian_splats/size_clamp_min", 0.0).toDouble());
	this->sizeClampMaxDoubleSpinBox->setValue(settings_->value("gaussian_splats/size_clamp_max", 0.0).toDouble());
	this->sizeClampInvertCheckBox->setChecked(settings_->value("gaussian_splats/size_clamp_invert", false).toBool());
	this->alphaCutoffDoubleSpinBox->setValue(settings_->value("gaussian_splats/alpha_cutoff", 1.0 / 255.0).toDouble());
	this->showOverdrawCheckBox->setChecked(false); // Deliberately not persisted - a momentary debug view, not a preference; starting a session with it silently on would be confusing.
	this->overdrawSumAlphaCheckBox->setChecked(false); // Not persisted either, for the same reason - it only qualifies the view above.
	this->overdrawRangeMinDoubleSpinBox->setValue(settings_->value("gaussian_splats/overdraw_range_min", 2.0).toDouble());
	this->overdrawRangeMaxDoubleSpinBox->setValue(settings_->value("gaussian_splats/overdraw_range_max", 100.0).toDouble());
	this->maxLayerDensityDoubleSpinBox->setValue(settings_->value("gaussian_splats/max_layer_density", 0.0).toDouble());
	this->maxTreeDepthSpinBox->setValue(settings_->value("gaussian_splats/max_tree_depth", 0).toInt());
	this->numDrawSlicesSpinBox->setValue(settings_->value("gaussian_splats/num_draw_slices", 1).toInt());

	this->settings = settings_; // Last, so that none of the above wrote anything - see the note at the top of this function.
}


void GaussianSplatSettingsWidget::settingsChanged()
{
	if(settings)
	{
		settings->setValue("gaussian_splats/pixel_scale_limit", this->pixelScaleLimitDoubleSpinBox->value());
		settings->setValue("gaussian_splats/max_splats_budget", this->maxSplatsBudgetSpinBox->value());
		settings->setValue("gaussian_splats/resort_move_threshold_ws", this->resortMoveThresholdDoubleSpinBox->value());
		settings->setValue("gaussian_splats/lod_base", this->lodBaseDoubleSpinBox->value());
		settings->setValue("gaussian_splats/size_clamp_min", this->sizeClampMinDoubleSpinBox->value());
		settings->setValue("gaussian_splats/size_clamp_max", this->sizeClampMaxDoubleSpinBox->value());
		settings->setValue("gaussian_splats/size_clamp_invert", this->sizeClampInvertCheckBox->isChecked());
		settings->setValue("gaussian_splats/alpha_cutoff", this->alphaCutoffDoubleSpinBox->value());
		settings->setValue("gaussian_splats/overdraw_range_min", this->overdrawRangeMinDoubleSpinBox->value());
		settings->setValue("gaussian_splats/overdraw_range_max", this->overdrawRangeMaxDoubleSpinBox->value());
		settings->setValue("gaussian_splats/max_layer_density", this->maxLayerDensityDoubleSpinBox->value());
		settings->setValue("gaussian_splats/max_tree_depth", this->maxTreeDepthSpinBox->value());
		settings->setValue("gaussian_splats/num_draw_slices", this->numDrawSlicesSpinBox->value());
	}

	emit settingsChangedSignal();
}
