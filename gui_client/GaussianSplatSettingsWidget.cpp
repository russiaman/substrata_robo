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
}


GaussianSplatSettingsWidget::~GaussianSplatSettingsWidget()
{
}


void GaussianSplatSettingsWidget::init(QSettings* settings_)
{
	settings = settings_;

	// Defaults here match GaussianSplatRenderer's own hardcoded defaults (pixel_scale_limit, max_splats_budget,
	// resort_move_threshold_ws) and buildGaussianSplatLodTree()'s default lod_base, so a settings store with no saved
	// values yet reproduces the same behaviour as before this widget existed.
	this->pixelScaleLimitDoubleSpinBox->setValue(settings->value("gaussian_splats/pixel_scale_limit", 1.0).toDouble());
	this->maxSplatsBudgetSpinBox->setValue(settings->value("gaussian_splats/max_splats_budget", 10000000).toInt());
	this->resortMoveThresholdDoubleSpinBox->setValue(settings->value("gaussian_splats/resort_move_threshold_ws", 0.1).toDouble());
	this->lodBaseDoubleSpinBox->setValue(settings->value("gaussian_splats/lod_base", 1.5).toDouble());
}


void GaussianSplatSettingsWidget::settingsChanged()
{
	if(settings)
	{
		settings->setValue("gaussian_splats/pixel_scale_limit", this->pixelScaleLimitDoubleSpinBox->value());
		settings->setValue("gaussian_splats/max_splats_budget", this->maxSplatsBudgetSpinBox->value());
		settings->setValue("gaussian_splats/resort_move_threshold_ws", this->resortMoveThresholdDoubleSpinBox->value());
		settings->setValue("gaussian_splats/lod_base", this->lodBaseDoubleSpinBox->value());
	}

	emit settingsChangedSignal();
}
