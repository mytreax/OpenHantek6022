// SPDX-License-Identifier: GPL-2.0+

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDockWidget>
#include <QLabel>
#include <QSignalBlocker>

#include <cmath>

#include "HorizontalDock.h"
#include "dockwindows.h"

#include "scopesettings.h"
#include "sispinbox.h"
#include "utils/printutils.h"

static int row = 0;

template < typename... Args > struct SELECT {
    template < typename C, typename R > static constexpr auto OVERLOAD_OF( R ( C::*pmf )( Args... ) ) -> decltype( pmf ) {
        return pmf;
    }
};

HorizontalDock::HorizontalDock( DsoSettingsScope *scope, const Dso::ControlSpecification *spec, QWidget *parent )
    : QDockWidget( tr( "Horizontal" ), parent ), scope( scope ) {

    // Initialize elements
    this->samplerateLabel = new QLabel( tr( "Samplerate" ) );
    this->samplerateSiSpinBox = new SiSpinBox( UNIT_SAMPLES );
    this->samplerateSiSpinBox->setMinimum( 1 );
    this->samplerateSiSpinBox->setMaximum( 1e8 );
    this->samplerateSiSpinBox->setUnitPostfix( tr( "/s" ) );

    timebaseSteps << 1.0 << 2.0 << 5.0 << 10.0;

    this->timebaseLabel = new QLabel( tr( "Timebase" ) );
    this->timebaseSiSpinBox = new SiSpinBox( UNIT_SECONDS );
    this->timebaseSiSpinBox->setSteps( timebaseSteps );
    this->timebaseSiSpinBox->setMinimum( 1e-9 );
    this->timebaseSiSpinBox->setMaximum( 1e3 );

    this->formatLabel = new QLabel( tr( "Format" ) );
    this->formatComboBox = new QComboBox();
    for ( Dso::GraphFormat format : Dso::GraphFormatEnum )
        this->formatComboBox->addItem( Dso::graphFormatString( format ) );

    this->calfreqLabel = new QLabel( tr( "Calibration out" ) );
    this->calfreqSiSpinBox = new SiSpinBox( UNIT_HERTZ );
    this->calfreqSiSpinBox->setSteps( spec->calfreqSteps );
    this->calfreqSiSpinBox->setMinimum( spec->calfreqSteps.first() );
    this->calfreqSiSpinBox->setMaximum( spec->calfreqSteps.last() );

    this->dockLayout = new QGridLayout();
    this->dockLayout->setColumnMinimumWidth( 0, 64 );
    this->dockLayout->setColumnStretch( 1, 1 );
    this->dockLayout->setSpacing( DOCK_LAYOUT_SPACING );

    row = 0; // allows flexible shift up/down
    this->dockLayout->addWidget( this->timebaseLabel, row, 0 );
    this->dockLayout->addWidget( this->timebaseSiSpinBox, row++, 1 );
    this->dockLayout->addWidget( this->samplerateLabel, row, 0 );
    this->dockLayout->addWidget( this->samplerateSiSpinBox, row++, 1 );
    this->dockLayout->addWidget( this->formatLabel, row, 0 );
    this->dockLayout->addWidget( this->formatComboBox, row++, 1 );
    this->dockLayout->addWidget( this->calfreqLabel, row, 0 );
    this->dockLayout->addWidget( this->calfreqSiSpinBox, row++, 1 );

    this->dockWidget = new QWidget();
    SetupDockWidget( this, dockWidget, dockLayout );

    // Load settings into GUI
    this->loadSettings( scope );

    // Connect signals and slots
    connect( this->samplerateSiSpinBox, SELECT< double >::OVERLOAD_OF( &QDoubleSpinBox::valueChanged ), this,
             &HorizontalDock::samplerateSelected );
    connect( this->timebaseSiSpinBox, SELECT< double >::OVERLOAD_OF( &QDoubleSpinBox::valueChanged ), this,
             &HorizontalDock::timebaseSelected );
    connect( this->formatComboBox, SELECT< int >::OVERLOAD_OF( &QComboBox::currentIndexChanged ), this,
             &HorizontalDock::formatSelected );
    connect( this->calfreqSiSpinBox, SELECT< double >::OVERLOAD_OF( &QDoubleSpinBox::valueChanged ), this,
             &HorizontalDock::calfreqSelected );
}

void HorizontalDock::loadSettings( DsoSettingsScope *scope ) {
    // Set values
    this->setSamplerate( scope->horizontal.samplerate );
    this->setTimebase( scope->horizontal.timebase );
    this->setFormat( scope->horizontal.format );
    this->setCalfreq( scope->horizontal.calfreq );
}

/// \brief Don't close the dock, just hide it.
/// \param event The close event that should be handled.
void HorizontalDock::closeEvent( QCloseEvent *event ) {
    this->hide();
    event->accept();
}


double HorizontalDock::setSamplerate( double samplerate ) {
    // printf( "HD::setSamplerate( %g )\n", samplerate );
    QSignalBlocker blocker( timebaseSiSpinBox );
    timebaseSiSpinBox->setMaximum( scope->horizontal.maxTimebase );
    blocker = QSignalBlocker( samplerateSiSpinBox );
    samplerateSiSpinBox->setValue( samplerate );
    return samplerateSiSpinBox->value();
}


double HorizontalDock::setTimebase( double timebase ) {
    // printf( "HD::setTimebase( %g )\n", timebase );
    QSignalBlocker blocker( timebaseSiSpinBox );
    // timebaseSteps are repeated in each decade
    double decade = pow( 10, floor( log10( timebase ) ) );
    double vNorm = timebase / decade;
    for ( int i = 0; i < timebaseSteps.size() - 1; ++i ) {
        if ( timebaseSteps.at( i ) <= vNorm && vNorm < timebaseSteps.at( i + 1 ) ) {
            timebaseSiSpinBox->setValue( decade * timebaseSteps.at( i ) );
            break;
        }
    }
    // printf( "return %g\n", timebaseSiSpinBox->value() );
    calculateSamplerateSteps( timebase );
    return timebaseSiSpinBox->value();
}


int HorizontalDock::setFormat( Dso::GraphFormat format ) {
    QSignalBlocker blocker( formatComboBox );
    if ( format >= Dso::GraphFormat::TY && format <= Dso::GraphFormat::XY ) {
        formatComboBox->setCurrentIndex( format );
        return format;
    }
    return -1;
}


double HorizontalDock::setCalfreq( double calfreq ) {
    QSignalBlocker blocker( calfreqSiSpinBox );
    calfreqSiSpinBox->setValue( calfreq );
    return calfreqSiSpinBox->value();
}


void HorizontalDock::setSamplerateLimits( double minimum, double maximum ) {
    // printf( "HD::setSamplerateLimits( %g, %g )\n", minimum, maximum );
    QSignalBlocker blocker( samplerateSiSpinBox );
    if ( bool( minimum ) )
        this->samplerateSiSpinBox->setMinimum( minimum );
    if ( bool( maximum ) )
        this->samplerateSiSpinBox->setMaximum( maximum );
}


void HorizontalDock::setSamplerateSteps( int mode, const QList< double > steps ) {
    // printf( "HD::setSamplerateSteps( %d )\n", mode );
    // qDebug() << "HD::setSamplerateSteps" << mode << steps;
    samplerateSteps = steps;
    // Assume that method is invoked for fixed samplerate devices only
    QSignalBlocker samplerateBlocker( samplerateSiSpinBox );
    samplerateSiSpinBox->setMode( mode );
    samplerateSiSpinBox->setSteps( steps );
    samplerateSiSpinBox->setMinimum( steps.first() );
    samplerateSiSpinBox->setMaximum( steps.last() );
    // Make reasonable adjustments to the timebase spinbox
    QSignalBlocker timebaseBlocker( timebaseSiSpinBox );
    timebaseSiSpinBox->setMinimum( pow( 10, floor( log10( 1.0 / steps.last() ) ) ) );
    calculateSamplerateSteps( timebaseSiSpinBox->value() );
}


/// \brief Called when the samplerate spinbox changes its value.
/// \param samplerate The samplerate in samples/second.
void HorizontalDock::samplerateSelected( double samplerate ) {
    // printf( "HD::samplerateSelected( %g )\n", samplerate );
    scope->horizontal.samplerate = samplerate;
    emit samplerateChanged( samplerate );
}


/// \brief Called when the timebase spinbox changes its value.
/// \param timebase The timebase in seconds.
void HorizontalDock::timebaseSelected( double timebase ) {
    // printf( "HD::timebaseSelected( %g )\n", timebase );
    scope->horizontal.timebase = timebase;
    calculateSamplerateSteps( timebase );
    emit timebaseChanged( timebase );
}


void HorizontalDock::calculateSamplerateSteps( double timebase ) {
    int size = samplerateSteps.size();
    if ( size ) {
        // search appropriate min & max sample rate
        double min = samplerateSteps[ 0 ];
        double max = samplerateSteps[ 0 ];
        for ( int id = 0; id < size; ++id ) {
            double sRate = samplerateSteps[ id ];
            // printf( "sRate %g, sRate*timebase %g\n", sRate, sRate * timebase );
            // min must be < maxRate
            // find minimal samplerate to get at least this number of samples per div
            if ( id < size - 1 && sRate * timebase <= 10 ) { // 10 samples/div
                min = sRate;
            }
            // max must be > minRate
            // find max samplesrate to get not more then this number of samples per div
            // number should be <= 1000 to get enough samples for two full screens (to ensure triggering)
            if ( id && sRate * timebase <= 1000 ) { // 1000 samples/div
                max = sRate;
            }
        }
        min = qMax( min, qMin( 10e3, max ) ); // not less than 10kS unless max is smaller
        // printf( "HD::cSS( %g )  -> %g, %g\n", timebase, min, max );
        setSamplerateLimits( min, max );
    }
}


/// \brief Called when the format combo box changes its value.
/// \param index The index of the combo box item.
void HorizontalDock::formatSelected( int index ) {
    scope->horizontal.format = Dso::GraphFormat( index );
    emit formatChanged( scope->horizontal.format );
}


/// \brief Called when the calfreq spinbox changes its value.
/// \param calfreq The calibration frequency in hertz.
void HorizontalDock::calfreqSelected( double calfreq ) {
    // printf( "calfreqSelected: %g\n", calfreq );
    scope->horizontal.calfreq = calfreq;
    emit calfreqChanged( calfreq );
}
