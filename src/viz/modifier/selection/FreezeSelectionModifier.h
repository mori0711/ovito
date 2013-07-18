///////////////////////////////////////////////////////////////////////////////
//
//  Copyright (2013) Alexander Stukowski
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

#ifndef __OVITO_FREEZE_SELECTION_MODIFIER_H
#define __OVITO_FREEZE_SELECTION_MODIFIER_H

#include <core/Core.h>
#include "../ParticleModifier.h"

namespace Viz {

using namespace Ovito;

/******************************************************************************
* Preserves the selection of particles over animation time.
******************************************************************************/
class FreezeSelectionModifier : public ParticleModifier
{
public:

	/// Default constructor.
	Q_INVOKABLE FreezeSelectionModifier() :
		_selectionProperty(new ParticleProperty(0, ParticleProperty::SelectionProperty)){}

	/// This virtual method is called by the system when the modifier has been inserted into a PipelineObject.
	virtual void initializeModifier(PipelineObject* pipelineObject, ModifierApplication* modApp) override;

	/// Asks the modifier for its validity interval at the given time.
	virtual TimeInterval modifierValidity(TimePoint time) override { return TimeInterval::forever(); }

	/// Returns the frozen selection state.
	const ParticleProperty& selectionSnapshot() const { OVITO_CHECK_POINTER(_selectionProperty.constData()); return *_selectionProperty; }

	/// Takes a snapshot of the selection state.
	void takeSelectionSnapshot(const PipelineFlowState& state);

protected:

	/// Saves the class' contents to the given stream.
	virtual void saveToStream(ObjectSaveStream& stream) override;

	/// Loads the class' contents from the given stream.
	virtual void loadFromStream(ObjectLoadStream& stream) override;

	/// Creates a copy of this object.
	virtual OORef<RefTarget> clone(bool deepCopy, CloneHelper& cloneHelper) override;

	/// Modifies the particle object.
	virtual ObjectStatus modifyParticles(TimePoint time, TimeInterval& validityInterval) override;

private:

	/// This stores the frozen selection when not using particle identifiers.
	QExplicitlySharedDataPointer<ParticleProperty> _selectionProperty;

	/// This stores the frozen selection when using particle identifiers.
	QVector<int> _selectedParticles;

	Q_OBJECT
	OVITO_OBJECT

	Q_CLASSINFO("DisplayName", "Freeze Selection");
	Q_CLASSINFO("ModifierCategory", "Selection");
};

/******************************************************************************
* A properties editor for the FreezeSelectionModifier class.
******************************************************************************/
class FreezeSelectionModifierEditor : public ParticleModifierEditor
{
public:

	/// Default constructor
	Q_INVOKABLE FreezeSelectionModifierEditor() {}

protected:

	/// Creates the user interface controls for the editor.
	virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

protected Q_SLOTS:

	/// Takes a new snapshot of the current particle selection.
	void takeSelectionSnapshot();

private:

	Q_OBJECT
	OVITO_OBJECT
};

};	// End of namespace

#endif // __OVITO_FREEZE_SELECTION_MODIFIER_H
