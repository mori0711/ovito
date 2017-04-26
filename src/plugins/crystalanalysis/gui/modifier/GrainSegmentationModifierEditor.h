///////////////////////////////////////////////////////////////////////////////
//
//  Copyright (2016) Alexander Stukowski
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  OVITO is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once


#include <plugins/crystalanalysis/CrystalAnalysis.h>
#include <plugins/particles/gui/modifier/ParticleModifierEditor.h>
#include <core/utilities/DeferredMethodInvocation.h>

class QwtPlot;
class QwtPlotCurve;
class QwtPlotZoneItem;

namespace Ovito { namespace Plugins { namespace CrystalAnalysis {

/**
 * Properties editor for the GrainSegmentationModifier class.
 */
class GrainSegmentationModifierEditor : public ParticleModifierEditor
{
public:

	/// Default constructor.
	Q_INVOKABLE GrainSegmentationModifierEditor() {}

protected Q_SLOTS:

	/// Replots the histogram computed by the modifier.
	void plotHistogram();

protected:

	/// Creates the user interface controls for the editor.
	virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

	/// This method is called when a reference target changes.
	virtual bool referenceEvent(RefTarget* source, ReferenceEvent* event) override;

private:

	/// The graph widget to display the RMSD histogram.
	QwtPlot* _plot;

	/// The plot item for the histogram.
    QwtPlotCurve* _plotCurve = nullptr;

	/// Marks the RMSD cutoff in the histogram plot.
	QwtPlotZoneItem* _rmsdRange = nullptr;

	/// For deferred invocation of the plot repaint function.
	DeferredMethodInvocation<GrainSegmentationModifierEditor, &GrainSegmentationModifierEditor::plotHistogram> plotHistogramLater;

	Q_OBJECT
	OVITO_OBJECT
};

}	// End of namespace
}	// End of namespace
}	// End of namespace


