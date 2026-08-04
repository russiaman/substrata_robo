/*=====================================================================
GaussianSplatSettingsWidget.cpp
---------------------------------
coded by AI agent under @russiaman supervision -
Generated at Wed Aug  5 00:00:00 2026
=====================================================================*/
#include "GaussianSplatSettingsWidget.h"


#include "../qt/SignalBlocker.h"
#include <QtCore/QSettings>


GaussianSplatSettingsWidget::GaussianSplatSettingsWidget(QWidget* parent)
:	settings(NULL)
{
	setupUi(this);

	connect(this->lodBaseDoubleSpinBox,             SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->pixelScaleLimitDoubleSpinBox,      SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
	connect(this->maxSplatsBudgetSpinBox,            SIGNAL(valueChanged(int)),    this, SLOT(settingsChanged()));
	connect(this->resortMoveThresholdDoubleSpinBox,  SIGNAL(valueChanged(double)), this, SLOT(settingsChanged()));
}


GaussianSplatSettingsWidget::~GaussianSplatSettingsWidget()
{
}


void GaussianSplatSettingsWidget::init(QSettings* settings_, float default_lod_base, float default_pixel_scale_limit, size_t default_max_splats_budget, float default_resort_move_threshold_ws)
{
	settings = settings_;
	SignalBlocker::setValue(this->lodBaseDoubleSpinBox, settings->value("gaussian_splats/lod_base", (double)default_lod_base).toDouble());
	SignalBlocker::setValue(this->pixelScaleLimitDoubleSpinBox, settings->value("gaussian_splats/pixel_scale_limit", (double)default_pixel_scale_limit).toDouble());
	SignalBlocker::setValue(this->maxSplatsBudgetSpinBox, settings->value("gaussian_splats/max_splats_budget", (qulonglong)default_max_splats_budget).toInt());
	SignalBlocker::setValue(this->resortMoveThresholdDoubleSpinBox, settings->value("gaussian_splats/resort_move_threshold_ws", (double)default_resort_move_threshold_ws).toDouble());
}


void GaussianSplatSettingsWidget::settingsChanged()
{
	if(settings)
	{
		settings->setValue("gaussian_splats/lod_base", this->lodBaseDoubleSpinBox->value());
		settings->setValue("gaussian_splats/pixel_scale_limit", this->pixelScaleLimitDoubleSpinBox->value());
		settings->setValue("gaussian_splats/max_splats_budget", this->maxSplatsBudgetSpinBox->value());
		settings->setValue("gaussian_splats/resort_move_threshold_ws", this->resortMoveThresholdDoubleSpinBox->value());
	}

	emit settingsChangedSignal();
}
