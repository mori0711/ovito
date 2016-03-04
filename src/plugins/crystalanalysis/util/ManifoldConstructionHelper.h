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

#ifndef __OVITO_MANIFOLD_CONSTRUCTION_HELPER_H
#define __OVITO_MANIFOLD_CONSTRUCTION_HELPER_H

#include <plugins/crystalanalysis/CrystalAnalysis.h>
#include <plugins/particles/data/SimulationCell.h>
#include <plugins/particles/data/ParticleProperty.h>
#include <core/utilities/concurrent/FutureInterface.h>
#include <plugins/crystalanalysis/util/DelaunayTessellation.h>

#include <unordered_map>
#include <boost/functional/hash.hpp>

namespace Ovito { namespace Plugins { namespace CrystalAnalysis {

/**
 * Constructs a closed manifold which separates different regions
 * in a tetrahedral mesh.
 */
template<class HalfEdgeStructureType, bool FlipOrientation = false, bool CreateTwoSidedMesh = false>
class ManifoldConstructionHelper
{
public:

	// A no-op face-preparation functor.
	struct DefaultPrepareMeshFaceFunc {
		void operator()(typename HalfEdgeStructureType::Face* face,
				const std::array<int,3>& vertexIndices,
				const std::array<DelaunayTessellation::VertexHandle,3>& vertexHandles,
				DelaunayTessellation::CellHandle cell) {}
	};

public:

	/// Constructor.
	ManifoldConstructionHelper(const DelaunayTessellation& tessellation, HalfEdgeStructureType& outputMesh, FloatType alpha,
			ParticleProperty* positions) : _tessellation(tessellation), _mesh(outputMesh), _alpha(alpha), _positions(positions) {}

	/// This is the main function, which constructs the manifold triangle mesh.
	template<typename CellRegionFunc, typename PrepareMeshFaceFunc = DefaultPrepareMeshFaceFunc>
	bool construct(CellRegionFunc&& determineCellRegion, FutureInterfaceBase* progress, PrepareMeshFaceFunc&& prepareMeshFaceFunc = PrepareMeshFaceFunc())
	{
		// Algorithm is divided into several sub-steps.
		// Assign weights to sub-steps according to estimated runtime.
		if(progress) progress->beginProgressSubSteps({ 1, 1, 1 });

		/// Assign tetrahedra to regions.
		if(!classifyTetrahedra(std::move(determineCellRegion), progress))
			return false;

		if(progress) progress->nextProgressSubStep();

		// Create triangle facets at interfaces between two different regions.
		if(!createInterfaceFacets(std::move(prepareMeshFaceFunc), progress))
			return false;

		if(progress) progress->nextProgressSubStep();

		// Connect triangles with one another to form a closed manifold.
		if(!linkHalfedges(progress))
			return false;

		if(progress) progress->endProgressSubSteps();

		return true;
	}

	/// Returns the region to which all tetrahedra belong (or -1 if they belong to multiple regions).
	int spaceFillingRegion() const { return _spaceFillingRegion; }

private:

	/// Assigns each tetrahedron to a region.
	template<typename CellRegionFunc>
	bool classifyTetrahedra(CellRegionFunc&& determineCellRegion, FutureInterfaceBase* progress)
	{
		if(progress)
			progress->setProgressRange(_tessellation.numberOfTetrahedra());

		_numSolidCells = 0;
		_spaceFillingRegion = -2;
		int progressCounter = 0;
		for(DelaunayTessellation::CellIterator cell = _tessellation.begin_cells(); cell != _tessellation.end_cells(); ++cell) {

			// Update progress indicator.
			if(progress && !progress->setProgressValueIntermittent(progressCounter++))
				return false;

			// Alpha shape criterion: This determines whether the Delaunay tetrahedron is part of the solid region.
			bool isSolid = _tessellation.isValidCell(cell) &&
					_tessellation.dt().geom_traits().compare_squared_radius_3_object()(
							cell->vertex(0)->point(),
							cell->vertex(1)->point(),
							cell->vertex(2)->point(),
							cell->vertex(3)->point(),
							_alpha) != CGAL::POSITIVE;

			if(!isSolid) {
				cell->info().userField = 0;
			}
			else {
				cell->info().userField = determineCellRegion(cell);
			}

			if(!cell->info().isGhost) {
				if(_spaceFillingRegion == -2) _spaceFillingRegion = cell->info().userField;
				else if(_spaceFillingRegion != cell->info().userField) _spaceFillingRegion = -1;
			}

			if(cell->info().userField != 0 && !cell->info().isGhost) {
				cell->info().index = _numSolidCells++;
			}
			else {
				cell->info().index = -1;
			}
		}
		if(_spaceFillingRegion == -2) _spaceFillingRegion = 0;

		return true;
	}

	/// Constructs the triangle facets that separate different regions in the tetrahedral mesh.
	template<typename PrepareMeshFaceFunc>
	bool createInterfaceFacets(PrepareMeshFaceFunc&& prepareMeshFaceFunc, FutureInterfaceBase* progress)
	{
		// Stores the triangle mesh vertices created for the vertices of the tetrahedral mesh.
		std::vector<typename HalfEdgeStructureType::Vertex*> vertexMap(_positions->size(), nullptr);
		_tetrahedraFaceList.clear();
		_faceLookupMap.clear();

		if(progress)
			progress->setProgressRange(_numSolidCells);

		for(DelaunayTessellation::CellIterator cell = _tessellation.begin_cells(); cell != _tessellation.end_cells(); ++cell) {

			// Look for solid and local tetrahedra.
			if(cell->info().index == -1) continue;
			int solidRegion = cell->info().userField;
			OVITO_ASSERT(solidRegion != 0);

			// Update progress indicator.
			if(progress && !progress->setProgressValueIntermittent(cell->info().index))
				return false;

			Point3 unwrappedVerts[4];
			for(size_t i = 0; i < 4; i++)
				unwrappedVerts[i] = cell->vertex(i)->point();

			// Check validity of tessellation.
			Vector3 ad = unwrappedVerts[0] - unwrappedVerts[3];
			Vector3 bd = unwrappedVerts[1] - unwrappedVerts[3];
			Vector3 cd = unwrappedVerts[2] - unwrappedVerts[3];
			if(_tessellation.simCell().isWrappedVector(ad) || _tessellation.simCell().isWrappedVector(bd) || _tessellation.simCell().isWrappedVector(cd))
				throw Exception("Cannot construct manifold. Simulation cell length is too small for the given probe sphere radius parameter.");

			// Iterate over the four faces of the tetrahedron cell.
			cell->info().index = -1;
			for(int f = 0; f < 4; f++) {

				// Check if the adjacent tetrahedron belongs to a different region.
				std::pair<DelaunayTessellation::CellHandle,int> mirrorFacet = _tessellation.mirrorFacet(cell, f);
				DelaunayTessellation::CellHandle adjacentCell = mirrorFacet.first;
				if(adjacentCell->info().userField == solidRegion) {
					continue;
				}

				// Create the three vertices of the face or use existing output vertices.
				std::array<typename HalfEdgeStructureType::Vertex*,3> facetVertices;
				std::array<DelaunayTessellation::VertexHandle,3> vertexHandles;
				std::array<int,3> vertexIndices;
				for(int v = 0; v < 3; v++) {
					vertexHandles[v] = cell->vertex(DelaunayTessellation::cellFacetVertexIndex(f, FlipOrientation ? (2-v) : v));
					int vertexIndex = vertexIndices[v] = vertexHandles[v]->point().index();
					OVITO_ASSERT(vertexIndex >= 0 && vertexIndex < vertexMap.size());
					if(vertexMap[vertexIndex] == nullptr)
						vertexMap[vertexIndex] = _mesh.createVertex(_positions->getPoint3(vertexIndex));
					facetVertices[v] = vertexMap[vertexIndex];
				}

				// Create a new triangle facet.
				typename HalfEdgeStructureType::Face* face = _mesh.createFace(facetVertices.begin(), facetVertices.end());

				// Tell client code about the new facet.
				prepareMeshFaceFunc(face, vertexIndices, vertexHandles, cell);

				// Create additional face for exerior region if requested.
				if(CreateTwoSidedMesh && adjacentCell->info().userField == 0) {

					// Build face vertex list.
					std::reverse(std::begin(vertexHandles), std::end(vertexHandles));
					std::array<int,3> reverseVertexIndices;
					for(int v = 0; v < 3; v++) {
						vertexHandles[v] = adjacentCell->vertex(DelaunayTessellation::cellFacetVertexIndex(mirrorFacet.second, FlipOrientation ? (2-v) : v));
						int vertexIndex = reverseVertexIndices[v] = vertexHandles[v]->point().index();
						OVITO_ASSERT(vertexIndex >= 0 && vertexIndex < vertexMap.size());
						OVITO_ASSERT(vertexMap[vertexIndex] != nullptr);
						facetVertices[v] = vertexMap[vertexIndex];
					}

					// Create a new triangle facet.
					typename HalfEdgeStructureType::Face* oppositeFace = _mesh.createFace(facetVertices.begin(), facetVertices.end());

					// Tell client code about the new facet.
					prepareMeshFaceFunc(oppositeFace, reverseVertexIndices, vertexHandles, adjacentCell);

					// Insert new facet into lookup map.
					reorderFaceVertices(reverseVertexIndices);
					_faceLookupMap.emplace(reverseVertexIndices, oppositeFace);
				}

				// Insert new facet into lookup map.
				reorderFaceVertices(vertexIndices);
				_faceLookupMap.emplace(vertexIndices, face);

				// Insert into contiguous list of tetrahedron faces.
				if(cell->info().index == -1) {
					cell->info().index = _tetrahedraFaceList.size();
					_tetrahedraFaceList.push_back(std::array<typename HalfEdgeStructureType::Face*, 4>({ nullptr, nullptr, nullptr, nullptr }));
				}
				_tetrahedraFaceList[cell->info().index][f] = face;
			}
		}

		return true;
	}

	typename HalfEdgeStructureType::Face* findAdjacentFace(DelaunayTessellation::CellHandle cell, int f, int e)
	{
		int vertexIndex1, vertexIndex2;
		if(!FlipOrientation) {
			vertexIndex1 = DelaunayTessellation::cellFacetVertexIndex(f, (e+1)%3);
			vertexIndex2 = DelaunayTessellation::cellFacetVertexIndex(f, e);
		}
		else {
			vertexIndex1 = DelaunayTessellation::cellFacetVertexIndex(f, 2-e);
			vertexIndex2 = DelaunayTessellation::cellFacetVertexIndex(f, (4-e)%3);
		}
		DelaunayTessellation::FacetCirculator circulator_start = _tessellation.incident_facets(cell, vertexIndex1, vertexIndex2, cell, f);
		DelaunayTessellation::FacetCirculator circulator = circulator_start;
		OVITO_ASSERT(circulator->first == cell);
		OVITO_ASSERT(circulator->second == f);
		--circulator;
		OVITO_ASSERT(circulator != circulator_start);
		do {
			// Look for the first cell while going around the edge that belongs to a different region.
			if(circulator->first->info().userField != cell->info().userField)
				break;
			--circulator;
		}
		while(circulator != circulator_start);
		OVITO_ASSERT(circulator != circulator_start);

		// Get the current adjacent cell, which is part of the same region as the first tet.
		std::pair<DelaunayTessellation::CellHandle,int> mirrorFacet = _tessellation.mirrorFacet(circulator);
		OVITO_ASSERT(mirrorFacet.first->info().userField == cell->info().userField);

		typename HalfEdgeStructureType::Face* adjacentFace = findCellFace(mirrorFacet);
		if(adjacentFace == nullptr)
			throw Exception("Cannot construct mesh for this input dataset. Adjacent cell face not found.");

		return adjacentFace;
	}

	bool linkHalfedges(FutureInterfaceBase* progress)
	{
		if(progress) progress->setProgressRange(_tetrahedraFaceList.size());

		auto tet = _tetrahedraFaceList.cbegin();
		for(DelaunayTessellation::CellIterator cell = _tessellation.begin_cells(); cell != _tessellation.end_cells(); ++cell) {

			// Look for tetrahedra with at least one face.
			if(cell->info().index == -1) continue;

			// Update progress indicator.
			if(progress && !progress->setProgressValueIntermittent(cell->info().index))
				return false;

			for(int f = 0; f < 4; f++) {
				typename HalfEdgeStructureType::Face* facet = (*tet)[f];
				if(facet == nullptr) continue;

				typename HalfEdgeStructureType::Edge* edge = facet->edges();
				for(int e = 0; e < 3; e++, edge = edge->nextFaceEdge()) {
					OVITO_CHECK_POINTER(edge);
					if(edge->oppositeEdge() != nullptr) continue;
					typename HalfEdgeStructureType::Face* oppositeFace = findAdjacentFace(cell, f, e);
					typename HalfEdgeStructureType::Edge* oppositeEdge = oppositeFace->findEdge(edge->vertex2(), edge->vertex1());
					if(oppositeEdge == nullptr)
						throw Exception("Cannot construct mesh for this input dataset. Opposite half-edge not found.");
					edge->linkToOppositeEdge(oppositeEdge);
				}

				if(CreateTwoSidedMesh) {
					std::pair<DelaunayTessellation::CellHandle,int> oppositeFacet = _tessellation.mirrorFacet(cell, f);
					OVITO_ASSERT(oppositeFacet.first->info().userField != cell->info().userField);
					if(oppositeFacet.first->info().userField == 0) {
						typename HalfEdgeStructureType::Face* outerFacet = findCellFace(oppositeFacet);
						OVITO_ASSERT(outerFacet != nullptr);

						typename HalfEdgeStructureType::Edge* edge = outerFacet->edges();
						for(int e = 0; e < 3; e++, edge = edge->nextFaceEdge()) {
							OVITO_CHECK_POINTER(edge);
							if(edge->oppositeEdge() != nullptr) continue;
							typename HalfEdgeStructureType::Face* oppositeFace = findAdjacentFace(oppositeFacet.first, oppositeFacet.second, e);
							typename HalfEdgeStructureType::Edge* oppositeEdge = oppositeFace->findEdge(edge->vertex2(), edge->vertex1());
							if(oppositeEdge == nullptr)
								throw Exception("Cannot construct mesh for this input dataset. Opposite half-edge1 not found.");
							edge->linkToOppositeEdge(oppositeEdge);
						}
					}
				}
			}

			++tet;
		}
		OVITO_ASSERT(tet == _tetrahedraFaceList.cend());
		OVITO_ASSERT(_mesh.isClosed());
		return true;
	}

	typename HalfEdgeStructureType::Face* findCellFace(const std::pair<DelaunayTessellation::CellHandle,int>& facet)
	{
		// If the cell is a ghost cell, find the corresponding real cell first.
		auto cell = facet.first;
		if(cell->info().index != -1) {
			OVITO_ASSERT(cell->info().index >= 0 && cell->info().index < _tetrahedraFaceList.size());
			return _tetrahedraFaceList[cell->info().index][facet.second];
		}
		else {
			std::array<int,3> faceVerts;
			for(size_t i = 0; i < 3; i++) {
				int vertexIndex = DelaunayTessellation::cellFacetVertexIndex(facet.second, FlipOrientation ? (2-i) : i);
				faceVerts[i] = cell->vertex(vertexIndex)->point().index();
			}
			reorderFaceVertices(faceVerts);
			auto iter = _faceLookupMap.find(faceVerts);
			if(iter != _faceLookupMap.end())
				return iter->second;
			else
				return nullptr;
		}
	}

	static void reorderFaceVertices(std::array<int,3>& vertexIndices) {
		// Shift the order of vertices so that the smallest index is at the front.
		std::rotate(std::begin(vertexIndices), std::min_element(std::begin(vertexIndices), std::end(vertexIndices)), std::end(vertexIndices));
	}

private:

	/// The tetrahedral tessellation.
	const DelaunayTessellation& _tessellation;

	/// The squared probe sphere radius used to classify tetrahedra as open or solid.
	FloatType _alpha;

	/// Counts the number of tetrehedral cells that belong to the solid region.
	int _numSolidCells = 0;

	/// Stores the region ID if all cells belong to the same region.
	int _spaceFillingRegion = -1;

	/// The input particle positions.
	ParticleProperty* _positions;

	/// The output triangle mesh.
	HalfEdgeStructureType& _mesh;

	/// Stores the faces of the local tetrahedra that have a least one facet for which a triangle has been created.
	std::vector<std::array<typename HalfEdgeStructureType::Face*, 4>> _tetrahedraFaceList;

	/// This map allows to lookup faces based on their vertices.
	std::unordered_map<std::array<int,3>, typename HalfEdgeStructureType::Face*, boost::hash<std::array<int,3>>> _faceLookupMap;
};

}	// End of namespace
}	// End of namespace
}	// End of namespace

#endif // __OVITO_MANIFOLD_CONSTRUCTION_HELPER_H
