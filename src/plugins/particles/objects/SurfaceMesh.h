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

#ifndef __OVITO_SURFACE_MESH_H
#define __OVITO_SURFACE_MESH_H

#include <plugins/particles/Particles.h>
#include <plugins/particles/data/SimulationCell.h>
#include <core/scene/objects/DataObjectWithSharedStorage.h>
#include <core/utilities/mesh/HalfEdgeMesh.h>
#include <core/utilities/concurrent/Promise.h>

namespace Ovito { namespace Particles {

/**
 * \brief A closed triangle mesh representing a surface.
 */
class OVITO_PARTICLES_EXPORT SurfaceMesh : public DataObjectWithSharedStorage<HalfEdgeMesh<>>
{
public:

	/// \brief Constructor that creates an empty SurfaceMesh object.
	Q_INVOKABLE SurfaceMesh(DataSet* dataset, HalfEdgeMesh<>* mesh = nullptr);

	/// Returns the title of this object.
	virtual QString objectTitle() override { return tr("Surface mesh"); }

	/// \brief Returns whether this object, when returned as an editable sub-object by another object,
	///        should be displayed in the modification stack.
	///
	/// Return false because this object cannot be edited.
	virtual bool isSubObjectEditable() const override { return false; }

	/// Returns the planar cuts applied to this mesh.
	const QVector<Plane3>& cuttingPlanes() const { return _cuttingPlanes; }

	/// Sets the planar cuts applied to this mesh.
	void setCuttingPlanes(const QVector<Plane3>& planes) {
		_cuttingPlanes = planes;
		notifyDependents(ReferenceEvent::TargetChanged);
	}

	/// Fairs the triangle mesh stored in this object.
	bool smoothMesh(const SimulationCell& cell, int numIterations, PromiseBase& promise, FloatType k_PB = FloatType(0.1), FloatType lambda = FloatType(0.5)) {
		if(!smoothMesh(*modifiableStorage(), cell, numIterations, promise, k_PB, lambda))
			return false;
		changed();
		return true;
	}

	/// Fairs a triangle mesh.
	static bool smoothMesh(HalfEdgeMesh<>& mesh, const SimulationCell& cell, int numIterations, PromiseBase& promise, FloatType k_PB = FloatType(0.1), FloatType lambda = FloatType(0.5));

protected:

	/// Performs one iteration of the smoothing algorithm.
	static void smoothMeshIteration(HalfEdgeMesh<>& mesh, FloatType prefactor, const SimulationCell& cell);

	/// Creates a copy of this object.
	virtual OORef<RefTarget> clone(bool deepCopy, CloneHelper& cloneHelper) override;

private:

	/// Indicates that the entire simulation cell is part of the solid region.
	DECLARE_MODIFIABLE_PROPERTY_FIELD(bool, isCompletelySolid, setIsCompletelySolid);

	/// The planar cuts applied to this mesh.
	QVector<Plane3> _cuttingPlanes;

	Q_OBJECT
	OVITO_OBJECT
};

}	// End of namespace
}	// End of namespace

#endif // __OVITO_SURFACE_MESH_H
