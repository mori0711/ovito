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

#include <plugins/crystalanalysis/CrystalAnalysis.h>
#include <core/utilities/concurrent/ParallelFor.h>
#include <plugins/particles/util/NearestNeighborFinder.h>
#include <plugins/crystalanalysis/util/DelaunayTessellation.h>
#include <plugins/crystalanalysis/util/ManifoldConstructionHelper.h>
#include "GrainSegmentationEngine.h"
#include "GrainSegmentationModifier.h"

#include <ptm/index_ptm.h>
#include <ptm/qcprot/quat.hpp>

#include <boost/range/algorithm/fill.hpp>
#include <boost/range/algorithm/replace.hpp>
#include <boost/range/algorithm/count.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm_ext/iota.hpp>
#include <boost/functional/hash.hpp>
#include <unordered_set>

namespace Ovito { namespace Plugins { namespace CrystalAnalysis {

/******************************************************************************
* Constructor.
******************************************************************************/
GrainSegmentationEngine::GrainSegmentationEngine(const TimeInterval& validityInterval,
		ParticleProperty* positions, const SimulationCell& simCell,
		const QVector<bool>& typesToIdentify, ParticleProperty* selection,
		int inputCrystalStructure, FloatType rmsdCutoff, int numOrientationSmoothingIterations,
		FloatType orientationSmoothingWeight, FloatType misorientationThreshold, int minGrainAtomCount,
		FloatType probeSphereRadius, int meshSmoothingLevel) :
	StructureIdentificationModifier::StructureIdentificationEngine(validityInterval, positions, simCell, typesToIdentify, selection),
	_atomClusters(new ParticleProperty(positions->size(), ParticleProperty::ClusterProperty, 0, false)),
	_rmsd(new ParticleProperty(positions->size(), qMetaTypeId<FloatType>(), 1, 0, GrainSegmentationModifier::tr("RMSD"), false)),
	_rmsdCutoff(rmsdCutoff),
	_inputCrystalStructure(inputCrystalStructure),
	_numOrientationSmoothingIterations(numOrientationSmoothingIterations),
	_orientationSmoothingWeight(orientationSmoothingWeight),
	_orientations(new ParticleProperty(positions->size(), ParticleProperty::OrientationProperty, 0, true)),
	_misorientationThreshold(misorientationThreshold),
	_minGrainAtomCount(std::max(minGrainAtomCount, 1)),
	_probeSphereRadius(probeSphereRadius),
	_meshSmoothingLevel(meshSmoothingLevel),
	_latticeNeighborBonds(new BondsStorage()),
	_neighborDisorientationAngles(new BondProperty(0, qMetaTypeId<FloatType>(), 1, 0, GrainSegmentationModifier::tr("Disorientation"), false)),
	_defectDistances(new ParticleProperty(positions->size(), qMetaTypeId<FloatType>(), 1, 0, GrainSegmentationModifier::tr("Defect distance"), true)),
	_defectDistanceBasins(new ParticleProperty(positions->size(), qMetaTypeId<int>(), 1, 0, GrainSegmentationModifier::tr("Distance transform basins"), true))
{
	// Allocate memory for neighbor lists.
	_neighborLists = new ParticleProperty(positions->size(), qMetaTypeId<int>(), PTM_MAX_NBRS, 0, QStringLiteral("Neighbors"), false);
	std::fill(_neighborLists->dataInt(), _neighborLists->dataInt() + _neighborLists->size() * _neighborLists->componentCount(), -1);
}

/******************************************************************************
* Performs the actual analysis. This method is executed in a worker thread.
******************************************************************************/
void GrainSegmentationEngine::perform()
{
	setProgressText(GrainSegmentationModifier::tr("Performing grain segmentation"));

	// Prepare the neighbor list.
	NearestNeighborFinder neighFinder(MAX_NEIGHBORS);
	if(!neighFinder.prepare(positions(), cell(), selection(), this))
		return;

	// Create output storage.
	ParticleProperty* output = structures();

	setProgressRange(positions()->size());
	setProgressValue(0);

	// Perform analysis on each particle.
	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - structure identification"));
	parallelForChunks(positions()->size(), *this, [this, &neighFinder, output](size_t startIndex, size_t count, FutureInterfaceBase& progress) {

		// Initialize thread-local storage for PTM routine.
		ptm_local_handle_t ptm_local_handle = ptm_initialize_local();

		size_t endIndex = startIndex + count;
		for(size_t index = startIndex; index < endIndex; index++) {

			// Update progress indicator.
			if((index % 256) == 0)
				progress.incrementProgressValue(256);

			// Break out of loop when operation was canceled.
			if(progress.isCanceled())
				break;

			// Skip particles that are not included in the analysis.
			if(selection() && !selection()->getInt(index)) {
				output->setInt(index, OTHER);
				_rmsd->setFloat(index, 0);
				continue;
			}

			// Find nearest neighbors.
			NearestNeighborFinder::Query<MAX_NEIGHBORS> neighQuery(neighFinder);
			neighQuery.findNeighbors(neighFinder.particlePos(index));
			int numNeighbors = neighQuery.results().size();
			OVITO_ASSERT(numNeighbors <= MAX_NEIGHBORS);

			// Bring neighbor coordinates into a form suitable for the PTM library.
			double points[(MAX_NEIGHBORS+1) * 3];
			points[0] = points[1] = points[2] = 0;
			for(int i = 0; i < numNeighbors; i++) {
				points[i*3 + 3] = neighQuery.results()[i].delta.x();
				points[i*3 + 4] = neighQuery.results()[i].delta.y();
				points[i*3 + 5] = neighQuery.results()[i].delta.z();
			}

			// Determine which structures to look for. This depends on how
			// much neighbors are present.
			int32_t flags = 0;
			if(numNeighbors >= 6 && typesToIdentify()[SC]) flags |= PTM_CHECK_SC;
			if(numNeighbors >= 12) {
				if(typesToIdentify()[FCC]) flags |= PTM_CHECK_FCC;
				if(typesToIdentify()[HCP]) flags |= PTM_CHECK_HCP;
				if(typesToIdentify()[ICO]) flags |= PTM_CHECK_ICO;
			}
			if(numNeighbors >= 14 && typesToIdentify()[BCC]) flags |= PTM_CHECK_BCC;

			// Call PTM library to identify local structure.
			int32_t type, alloy_type;
			double scale;
			double rmsd;
			double q[4];
			int8_t mapping[PTM_MAX_NBRS + 1];
			ptm_index(ptm_local_handle, numNeighbors + 1, points, nullptr, flags, true,
					&type, &alloy_type, &scale, &rmsd, q,
					nullptr, nullptr,
					nullptr, nullptr, mapping, nullptr, nullptr);

			// Convert PTM classification to our own scheme and store computed quantities.
			if(type == PTM_MATCH_NONE) {
				output->setInt(index, OTHER);
				_rmsd->setFloat(index, 0);

				// Store neighbor list.
				numNeighbors = std::min(numNeighbors, PTM_MAX_NBRS);
				OVITO_ASSERT(numNeighbors <= _neighborLists->componentCount());
				for(int j = 0; j < numNeighbors; j++) {
					_neighborLists->setIntComponent(index, j, neighQuery.results()[j].index);
				}

			}
			else {
				if(type == PTM_MATCH_SC) output->setInt(index, SC);
				else if(type == PTM_MATCH_FCC) output->setInt(index, FCC);
				else if(type == PTM_MATCH_HCP) output->setInt(index, HCP);
				else if(type == PTM_MATCH_ICO) output->setInt(index, ICO);
				else if(type == PTM_MATCH_BCC) output->setInt(index, BCC);
				else OVITO_ASSERT(false);
				_rmsd->setFloat(index, rmsd);
				_orientations->setQuaternion(index, Quaternion((FloatType)q[1], (FloatType)q[2], (FloatType)q[3], (FloatType)q[0]));

				// Store neighbor list.
				for(int j = 0; j < ptm_num_nbrs[type]; j++) {
					OVITO_ASSERT(j < _neighborLists->componentCount());
					OVITO_ASSERT(mapping[j + 1] >= 1);
					OVITO_ASSERT(mapping[j + 1] <= numNeighbors);
					_neighborLists->setIntComponent(index, j, neighQuery.results()[mapping[j + 1] - 1].index);

					const Vector3& neighborVector = neighQuery.results()[mapping[j + 1] - 1].delta;
					// Check if neighbor vector spans more than half of a periodic simulation cell.
					for(size_t dim = 0; dim < 3; dim++) {
						if(cell().pbcFlags()[dim]) {
							if(std::abs(cell().inverseMatrix().prodrow(neighborVector, dim)) >= FloatType(0.5)+FLOATTYPE_EPSILON) {
								static const QString axes[3] = { QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z") };
								throw Exception(GrainSegmentationModifier::tr("Simulation box is too short along cell vector %1 (%2) to perform analysis. "
										"Please extend it first using the 'Show periodic images' modifier.").arg(dim+1).arg(axes[dim]));
							}
						}
					}
				}
			}
		}

		// Release thread-local storage of PTM routine.
		ptm_uninitialize_local(ptm_local_handle);
	});
	if(isCanceled() || output->size() == 0)
		return;

	// Determine histogram bin size based on maximum RMSD value.
	_rmsdHistogramData.resize(100);
	_rmsdHistogramBinSize = *std::max_element(_rmsd->constDataFloat(), _rmsd->constDataFloat() + output->size()) * 1.01f;
	_rmsdHistogramBinSize /= _rmsdHistogramData.size();
	if(_rmsdHistogramBinSize <= 0) _rmsdHistogramBinSize = 1;

	// Build RMSD histogram.
	for(size_t index = 0; index < output->size(); index++) {
		if(output->getInt(index) != OTHER) {
			OVITO_ASSERT(_rmsd->getFloat(index) >= 0);
			int binIndex = _rmsd->getFloat(index) / _rmsdHistogramBinSize;
			if(binIndex < _rmsdHistogramData.size())
				_rmsdHistogramData[binIndex]++;
		}
	}

	// Apply RMSD cutoff.
	if(_rmsdCutoff > 0) {
		for(size_t index = 0; index < output->size(); index++) {
			if(output->getInt(index) != OTHER) {
				if(_rmsd->getFloat(index) > _rmsdCutoff)
					output->setInt(index, OTHER);
			}
		}
	}

	// Lattice orientation smoothing.
	if(_numOrientationSmoothingIterations > 0) {
		setProgressText(GrainSegmentationModifier::tr("Grain segmentation - orientation smoothing"));
		setProgressRange(_numOrientationSmoothingIterations);
		beginProgressSubSteps(_numOrientationSmoothingIterations);
		QExplicitlySharedDataPointer<ParticleProperty> newOrientations(new ParticleProperty(positions()->size(), ParticleProperty::OrientationProperty, 0, false));
		for(int iter = 0; iter < _numOrientationSmoothingIterations; iter++) {
			if(iter != 0) nextProgressSubStep();
			parallelFor(output->size(), *this, [this, output, &newOrientations](size_t index) {
				int structureType = output->getInt(index);
				if(structureType != OTHER) {

					Quaternion& qavg = newOrientations->dataQuaternion()[index];
					qavg = Quaternion(0,0,0,0);

					const Quaternion& orient0 = _orientations->getQuaternion(index);
					Quaternion qinv = orient0.inverse();

					int nnbr = 0;
					for(size_t c = 0; c < _neighborLists->componentCount(); c++) {
						int neighborIndex = _neighborLists->getIntComponent(index, c);
						if(neighborIndex == -1) break;

						if(output->getInt(neighborIndex) != structureType) continue;

						const Quaternion& orient_nbr = _orientations->getQuaternion(neighborIndex);
						Quaternion qrot = qinv * orient_nbr;
						double qrot_[4] = { qrot.w(), qrot.x(), qrot.y(), qrot.z() };

						if(structureType == SC || structureType == FCC || structureType == BCC)
							rotate_quaternion_into_cubic_fundamental_zone(qrot_);
						else if(structureType == HCP)
							rotate_quaternion_into_hcp_fundamental_zone(qrot_);

						Quaternion qclosest = orient0 * Quaternion(qrot_[1], qrot_[2], qrot_[3], qrot_[0]);
						FloatType t = orient0.dot(qclosest);
						if(t < FloatType(-1)) t = -1;
						else if(t > FloatType(1)) t = 1;
						FloatType theta = acos(2 * t * t - 1);
						if(theta < 10 * FLOATTYPE_PI / 180.0) {
							qavg += qclosest;
							nnbr++;
						}
					}

					if(nnbr != 0)
						qavg.normalize();
					for(size_t i = 0; i < 4; i++)
						qavg[i] = orient0[i] + _orientationSmoothingWeight * qavg[i];
					qavg.normalize();
				}
				else {
					newOrientations->setQuaternion(index, _orientations->getQuaternion(index));
				}
			});
			if(isCanceled()) return;
			newOrientations.swap(_orientations);
		}
		endProgressSubSteps();
	}

	// Generate bonds (edges) between neighboring lattice atoms.
	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - edge generation"));
	setProgressValue(0);
	setProgressRange(output->size());
	size_t numLatticeAtoms = 0;
	for(size_t index = 0; index < output->size(); index++) {
		if(!incrementProgressValue()) return;
		int structureType = output->getInt(index);
		if(structureType != OTHER) {
			numLatticeAtoms++;
			for(size_t c = 0; c < _neighborLists->componentCount(); c++) {
				int neighborIndex = _neighborLists->getIntComponent(index, c);
				if(neighborIndex == -1) break;

				// Only create bonds between likewise neighbors.
				if(output->getInt(neighborIndex) != structureType) {

					// Mark this atom as border atom for the distance transform calculation, because
					// it has a non-lattice atom as neighbor.
					defectDistances()->setFloat(index, 1);
					continue;
				}

				// Skip every other half-bond, because we create two half-bonds below.
				if(positions()->getPoint3(index) > positions()->getPoint3(neighborIndex))
					continue;

				// Determine PBC bond shift using minimum image convention.
				Vector3 delta = positions()->getPoint3(index) - positions()->getPoint3(neighborIndex);
				Vector_3<int8_t> pbcShift = Vector_3<int8_t>::Zero();
				for(size_t dim = 0; dim < 3; dim++) {
					if(cell().pbcFlags()[dim])
						pbcShift[dim] = (int8_t)floor(cell().inverseMatrix().prodrow(delta, dim) + FloatType(0.5));
				}

				// Create two half-bonds.
				_latticeNeighborBonds->push_back(Bond{ pbcShift, (unsigned int)index, (unsigned int)neighborIndex });
				_latticeNeighborBonds->push_back(Bond{ -pbcShift, (unsigned int)neighborIndex, (unsigned int)index });
			}
		}
	}

	// Compute disorientation angles of edges.
	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - misorientation calculation"));
	_neighborDisorientationAngles->resize(_latticeNeighborBonds->size(), false);
	parallelFor(_latticeNeighborBonds->size(), *this, [this,output](size_t bondIndex) {
		const Bond& bond = (*_latticeNeighborBonds)[bondIndex];
		FloatType& disorientationAngle = *(_neighborDisorientationAngles->dataFloat() + bondIndex);

		const Quaternion& qA = _orientations->getQuaternion(bond.index1);
		const Quaternion& qB = _orientations->getQuaternion(bond.index2);

		int structureType = output->getInt(bond.index1);
		double orientA[4] = { qA.w(), qA.x(), qA.y(), qA.z() };
		double orientB[4] = { qB.w(), qB.x(), qB.y(), qB.z() };
		if(structureType == SC || structureType == FCC || structureType == BCC)
			disorientationAngle = (FloatType)quat_disorientation_cubic(orientA, orientB);
		else if(structureType == HCP)
			disorientationAngle = (FloatType)quat_disorientation_hcp(orientA, orientB);
		else
			disorientationAngle = FLOATTYPE_MAX;

		// Lattice atoms that possess a high disorientation edge are treated like defects
		// when computing the distance transform.
		if(disorientationAngle > _misorientationThreshold * 4) {
			defectDistances()->setFloat(bond.index1, 1);
			defectDistances()->setFloat(bond.index2, 1);
		}
	});

	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - computing distance transform"));
	setProgressValue(0);
	setProgressRange(numLatticeAtoms);

	// This is used in the following for fast lookup of bonds incident on an atom.
	ParticleBondMap bondMap(*_latticeNeighborBonds);

	// Build initial list of border atoms (distance==1).
	std::vector<size_t> distanceSortedAtoms;
	for(size_t particleIndex = 0; particleIndex < output->size(); particleIndex++) {
		if(defectDistances()->getFloat(particleIndex) == 1)
			distanceSortedAtoms.push_back(particleIndex);
	}

	// Distance transform calculation.
	bool done;
	size_t lastCount = 0;
	for(int currentDistance = 2; ; currentDistance++) {
		size_t currentCount = distanceSortedAtoms.size();
		for(size_t i = lastCount; i < currentCount; i++) {
			if(!incrementProgressValue()) return;
			for(size_t bondIndex : bondMap.bondsOfParticle(distanceSortedAtoms[i])) {
				const Bond& bond = (*_latticeNeighborBonds)[bondIndex];
				if(defectDistances()->getFloat(bond.index2) == 0) {
					defectDistances()->setFloat(bond.index2, currentDistance);
					distanceSortedAtoms.push_back(bond.index2);
				}
			}
		}
		if(distanceSortedAtoms.size() == currentCount)
			break;
		lastCount = currentCount;
	}

	// Smoothing of distance transform.
	int numDistanceTransformSmoothingIterations = 10;
	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - smoothing distance transform"));
	setProgressRange(numDistanceTransformSmoothingIterations);
	beginProgressSubSteps(numDistanceTransformSmoothingIterations);
	for(int iter = 0; iter < numDistanceTransformSmoothingIterations; iter++) {
		if(iter != 0) nextProgressSubStep();

		QExplicitlySharedDataPointer<ParticleProperty> nextDistance(new ParticleProperty(*defectDistances()));
		parallelFor(output->size(), *this, [this, &nextDistance, &bondMap](size_t particleIndex) {
			FloatType d0 = defectDistances()->getFloat(particleIndex);
			FloatType d1 = 0;
			int numBonds = 0;
			for(size_t bondIndex : bondMap.bondsOfParticle(particleIndex)) {
				const Bond& bond = (*_latticeNeighborBonds)[bondIndex];
				d1 += defectDistances()->getFloat(bond.index2);
				numBonds++;
			}
			if(numBonds > 0) d1 /= numBonds;
			nextDistance->setFloat(particleIndex, d0 * FloatType(0.5) + d1 * FloatType(0.5));
		});
		if(isCanceled()) return;
		_defectDistances.swap(nextDistance);
	}
	endProgressSubSteps();

	std::sort(distanceSortedAtoms.begin(), distanceSortedAtoms.end(), [this](size_t a, size_t b) {
		return defectDistances()->getFloat(a) < defectDistances()->getFloat(b);
	});

	// This helper function is used below to sort atoms in the priority queue in descending order w.r.t. their distance transform value.
	auto distanceTransformCompare = [this](size_t a, size_t b) {
		return defectDistances()->getFloat(a) > defectDistances()->getFloat(b);
	};

	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - clustering"));
	setProgressValue(0);
	setProgressRange(distanceSortedAtoms.size());

	// Create clusters by gradually filling up the distance transform basins.
	int numBasins = 0;
	std::deque<size_t> queue;
	for(auto seedAtomIndex = distanceSortedAtoms.rbegin(); seedAtomIndex != distanceSortedAtoms.rend(); ++seedAtomIndex) {
		if(!incrementProgressValue()) return;

		// First check if atom is not already part of one of the clusters.
		if(defectDistanceBasins()->getInt(*seedAtomIndex) != 0)
			continue;
		FloatType currentDistance = defectDistances()->getFloat(*seedAtomIndex);

		// Expand existing clusters up to the current water level.
		while(!queue.empty()) {
			size_t currentParticle = queue.front();
			if(defectDistances()->getFloat(currentParticle) < currentDistance)
				break;
			queue.pop_front();

			int clusterID = defectDistanceBasins()->getInt(currentParticle);
			for(size_t bondIndex : bondMap.bondsOfParticle(currentParticle)) {
				const Bond& bond = (*_latticeNeighborBonds)[bondIndex];
				if(defectDistanceBasins()->getInt(bond.index2) != 0) continue;
				if(_neighborDisorientationAngles->getFloat(bondIndex) > _misorientationThreshold) continue;

				// Make neighbor part of the same cluster as the central atom.
				defectDistanceBasins()->setInt(bond.index2, clusterID);
				queue.insert(std::upper_bound(queue.begin(), queue.end(), bond.index2, distanceTransformCompare), bond.index2);
			}
		}

		// Start a new cluster, unless atom has already become part of an existing cluster in the meantime.
		if(defectDistanceBasins()->getInt(*seedAtomIndex) == 0) {
			queue.insert(std::upper_bound(queue.begin(), queue.end(), *seedAtomIndex, distanceTransformCompare), *seedAtomIndex);
			defectDistanceBasins()->setInt(*seedAtomIndex, ++numBasins);
	}
	}
	boost::copy(defectDistanceBasins()->constIntRange(), atomClusters()->dataInt());

	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - average cluster orientation"));
	setProgressValue(0);
	setProgressRange(output->size());

	// Calculate average orientation of each cluster.
	std::vector<Quaternion> clusterOrientations(numBasins, Quaternion(0,0,0,0));
	std::vector<int> firstClusterAtom(numBasins, -1);
	std::vector<int> clusterSizes(numBasins, 0);
	for(size_t particleIndex = 0; particleIndex < output->size(); particleIndex++) {
		if(!incrementProgressValue()) return;

		int clusterId = atomClusters()->getInt(particleIndex);
		if(clusterId == 0) continue;

		// Cluster IDs start at 1. Need to subtract 1 to get cluster index.
		int clusterIndex = clusterId - 1;

		clusterSizes[clusterIndex]++;
		if(firstClusterAtom[clusterIndex] == -1)
			firstClusterAtom[clusterIndex] = particleIndex;

		const Quaternion& orient0 = _orientations->getQuaternion(firstClusterAtom[clusterIndex]);
		const Quaternion& orient = _orientations->getQuaternion(particleIndex);

		Quaternion qrot = orient0.inverse() * orient;
		double qrot_[4] = { qrot.w(), qrot.x(), qrot.y(), qrot.z() };

		int structureType = output->getInt(particleIndex);
		if(structureType == SC || structureType == FCC || structureType == BCC)
			rotate_quaternion_into_cubic_fundamental_zone(qrot_);
		else if(structureType == HCP)
			rotate_quaternion_into_hcp_fundamental_zone(qrot_);

		Quaternion qclosest = orient0 * Quaternion(qrot_[1], qrot_[2], qrot_[3], qrot_[0]);
		clusterOrientations[clusterIndex] += qclosest;
	}
	for(auto& qavg : clusterOrientations) {
		OVITO_ASSERT(qavg != Quaternion(0,0,0,0));
		qavg.normalize();
	}

	// Disjoint sets data structures.
	std::vector<int> ranks(numBasins, 0);
	std::vector<int> parents(numBasins);
	boost::iota(parents, 0);

	// Disjoint-sets helper function. Find part of Union-Find
	auto findParentCluster = [&parents](int clusterIndex) {
	    // Find root and make root as parent of i (path compression)
		int parent = parents[clusterIndex];
	    while(parent != parents[parent]) {
	    	parent = parents[parent];
	    }
    	parents[clusterIndex] = parent;
	    return parent;
	};

	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - cluster merging"));
	setProgressValue(0);
	setProgressRange(output->size());

	// Merge clusters.
	std::unordered_set<std::pair<int,int>, boost::hash<std::pair<int,int>>> visitedClusterPairs;
	for(size_t particleIndex = 0; particleIndex < output->size(); particleIndex++) {
		if(!incrementProgressValue()) return;

		for(size_t bondIndex : bondMap.bondsOfParticle(particleIndex)) {
			const Bond& bond = (*_latticeNeighborBonds)[bondIndex];

			int clusterIdA = atomClusters()->getInt(bond.index1);
			int clusterIdB = atomClusters()->getInt(bond.index2);

			// Only need to test for merge if atoms are not in same cluster.
			// Also no need for double testing.
			if(clusterIdB <= clusterIdA) continue;

			// Skip further tests if the two clusters have already been merged.
			int clusterIndexA = clusterIdA - 1;
			int clusterIndexB = clusterIdB - 1;
			int parentClusterA = findParentCluster(clusterIndexA);
			int parentClusterB = findParentCluster(clusterIndexB);
			if(parentClusterA == parentClusterB) continue;

			// Skip high-angle edges.
			if(_neighborDisorientationAngles->getFloat(bondIndex) > _misorientationThreshold) continue;

			// Check if this cluster pair has been considered before to avoid calculating the disorientation angle more than once.
			if(visitedClusterPairs.insert(std::make_pair(clusterIdA, clusterIdB)).second == false)
				continue;

			// Calculate cluster-cluster misorientation angle.
			const Quaternion& orientA = clusterOrientations[clusterIndexA];
			const Quaternion& orientB = clusterOrientations[clusterIndexB];

			double qA[4] = { orientA.w(), orientA.x(), orientA.y(), orientA.z() };
			double qB[4] = { orientB.w(), orientB.x(), orientB.y(), orientB.z() };

			FloatType disorientation;
			int structureType = output->getInt(particleIndex);
			if(structureType == SC || structureType == FCC || structureType == BCC)
				disorientation = (FloatType)quat_disorientation_cubic(qA, qB);
			else if(structureType == HCP)
				disorientation = (FloatType)quat_disorientation_hcp(qA, qB);
			else
				continue;

			if(disorientation < _misorientationThreshold) {
				// Merge the two clusters.
				// Attach smaller rank tree under root of high rank tree (Union by Rank)
				if(ranks[parentClusterA] < ranks[parentClusterB]) {
					parents[parentClusterA] = parentClusterB;
					clusterSizes[parentClusterB] += clusterSizes[parentClusterA];
				}
				else {
					parents[parentClusterB] = parentClusterA;
					clusterSizes[parentClusterA] += clusterSizes[parentClusterB];
					// If ranks are same, then make one as root and increment its rank by one
					if(ranks[parentClusterA] == ranks[parentClusterB])
						ranks[parentClusterA]++;
				}
			}
		}
	}

	// Compress cluster IDs after merging to make them contiguous.
	std::vector<int> clusterRemapping(numBasins);
	int numClusters = 0;
	// Assign new consecutive IDs to root clusters.
	for(int i = 0; i < numBasins; i++) {
		if(findParentCluster(i) == i) {
			// If the cluster's size is below the threshold, dissolve the cluster.
			if(clusterSizes[i] < _minGrainAtomCount) {
				clusterRemapping[i] = 0;
			}
			else {
				clusterSizes[numClusters] = clusterSizes[i];
				clusterRemapping[i] = ++numClusters;
			}
		}
	}
	// Determine new IDs for non-root clusters.
	for(int i = 0; i < numBasins; i++) {
		clusterRemapping[i] = clusterRemapping[findParentCluster(i)];
	}

#if 1
	// Randomize cluster IDs for testing purposes (giving more color contrast).
	std::vector<int> clusterRandomMapping(numClusters);
	boost::iota(clusterRandomMapping, 1);
	std::mt19937 rng(1);
	std::shuffle(clusterRandomMapping.begin(), clusterRandomMapping.end(), rng);
	for(int i = 0; i < numBasins; i++)
		clusterRemapping[i] = clusterRandomMapping[clusterRemapping[i]-1];
#endif

	// Relabel atoms after cluster IDs have changed.
	clusterSizes.resize(numClusters);
	clusterOrientations.resize(numClusters);
	for(size_t particleIndex = 0; particleIndex < output->size(); particleIndex++) {
		int clusterId = atomClusters()->getInt(particleIndex);
		if(clusterId == 0) continue;
		clusterId = clusterRemapping[clusterId - 1];
		atomClusters()->setInt(particleIndex, clusterId);
	}

	// Build list of orphan atoms.
	std::vector<size_t> orphanAtoms;
	for(size_t i = 0; i < atomClusters()->size(); i++) {
		if(atomClusters()->getInt(i) == 0)
			orphanAtoms.push_back(i);
	}

	setProgressText(GrainSegmentationModifier::tr("Grain segmentation - merging orphan atoms"));
	setProgressValue(0);
	setProgressRange(orphanAtoms.size());

	// Add orphan atoms to the grains.
	size_t oldOrphanCount = orphanAtoms.size();
	for(;;) {
		std::vector<int> newlyAssignedClusters(orphanAtoms.size(), 0);
		for(size_t i = 0; i < orphanAtoms.size(); i++) {
			if(isCanceled()) return;

			// Find the closest cluster atom in the neighborhood.
			FloatType minDistSq = FLOATTYPE_MAX;
			for(size_t c = 0; c < _neighborLists->componentCount(); c++) {
				int neighborIndex = _neighborLists->getIntComponent(orphanAtoms[i], c);
				if(neighborIndex == -1) break;
				int clusterId = atomClusters()->getInt(neighborIndex);
				if(clusterId == 0) continue;

				// Determine interatomic vector using minimum image convention.
				Vector3 delta = cell().wrapVector(positions()->getPoint3(neighborIndex) - positions()->getPoint3(orphanAtoms[i]));
				FloatType distSq = delta.squaredLength();
				if(distSq < minDistSq) {
					minDistSq = distSq;
					newlyAssignedClusters[i] = clusterId;
				}
			}
		}

		// Assign atoms to closest cluster and compress orphan list.
		size_t newOrphanCount = 0;
		for(size_t i = 0; i < orphanAtoms.size(); i++) {
			atomClusters()->setInt(orphanAtoms[i], newlyAssignedClusters[i]);
			if(newlyAssignedClusters[i] == 0) {
				orphanAtoms[newOrphanCount++] = orphanAtoms[i];
			}
			else {
				clusterSizes[newlyAssignedClusters[i] - 1]++;
				if(!incrementProgressValue()) return;
			}
		}
		orphanAtoms.resize(newOrphanCount);
		if(newOrphanCount == oldOrphanCount)
			break;
		oldOrphanCount = newOrphanCount;
	}

	// For output, convert edge disorientation angles from radians to degrees.
	for(FloatType& angle : _neighborDisorientationAngles->floatRange())
		angle *= FloatType(180) / FLOATTYPE_PI;

	// Generate grain boundary mesh.

	// Some random grain colors.
	static const Color grainColorList[] = {
			Color(255.0f/255.0f,41.0f/255.0f,41.0f/255.0f), Color(153.0f/255.0f,218.0f/255.0f,224.0f/255.0f), Color(71.0f/255.0f,75.0f/255.0f,225.0f/255.0f),
			Color(104.0f/255.0f,224.0f/255.0f,115.0f/255.0f), Color(238.0f/255.0f,250.0f/255.0f,46.0f/255.0f), Color(34.0f/255.0f,255.0f/255.0f,223.0f/255.0f),
			Color(255.0f/255.0f,158.0f/255.0f,41.0f/255.0f), Color(255.0f/255.0f,17.0f/255.0f,235.0f/255.0f), Color(173.0f/255.0f,3.0f/255.0f,240.0f/255.0f),
			Color(180.0f/255.0f,78.0f/255.0f,0.0f/255.0f), Color(162.0f/255.0f,190.0f/255.0f,34.0f/255.0f), Color(0.0f/255.0f,166.0f/255.0f,252.0f/255.0f)
	};

	// Create output cluster graph.
	_outputClusterGraph = new ClusterGraph();
	for(int grain = 0; grain < numClusters; grain++) {
		Cluster* cluster = _outputClusterGraph->createCluster(_inputCrystalStructure, grain + 1);
		cluster->atomCount = clusterSizes[grain];
		//cluster->orientation = grain.orientation;
		cluster->color = grainColorList[grain % (sizeof(grainColorList)/sizeof(grainColorList[0]))];
	}

	if(_probeSphereRadius > 0) {
		setProgressText(GrainSegmentationModifier::tr("Building grain boundary mesh"));
		if(!buildPartitionMesh())
			return;
	}
}

/** Find the most common element in the [first, last) range.

	O(n) in time; O(1) in space.

	[first, last) must be valid sorted range.
	Elements must be equality comparable.
*/
template <class ForwardIterator>
ForwardIterator most_common(ForwardIterator first, ForwardIterator last)
{
	ForwardIterator it(first), max_it(first);
	size_t count = 0, max_count = 0;
	for( ; first != last; ++first) {
		if(*it == *first)
			count++;
		else {
			it = first;
			count = 1;
		}
		if(count > max_count) {
			max_count = count;
			max_it = it;
		}
	}
	return max_it;
}

/******************************************************************************
* Builds the triangle mesh for the grain boundaries.
******************************************************************************/
bool GrainSegmentationEngine::buildPartitionMesh()
{
	double alpha = _probeSphereRadius * _probeSphereRadius;
	FloatType ghostLayerSize = _probeSphereRadius * 3.0f;

	// Check if combination of radius parameter and simulation cell size is valid.
	for(size_t dim = 0; dim < 3; dim++) {
		if(cell().pbcFlags()[dim]) {
			int stencilCount = (int)ceil(ghostLayerSize / cell().matrix().column(dim).dot(cell().cellNormalVector(dim)));
			if(stencilCount > 1)
				throw Exception(GrainSegmentationModifier::tr("Cannot generate Delaunay tessellation. Simulation cell is too small or probe sphere radius parameter is too large."));
		}
	}

	_mesh = new PartitionMeshData();

	// If there are too few particles, don't build Delaunay tessellation.
	// It is going to be invalid anyway.
	size_t numInputParticles = positions()->size();
	if(selection())
		numInputParticles = positions()->size() - std::count(selection()->constDataInt(), selection()->constDataInt() + selection()->size(), 0);
	if(numInputParticles <= 3)
		return true;

	// The algorithm is divided into several sub-steps.
	// Assign weights to sub-steps according to estimated runtime.
	beginProgressSubSteps({ 20, 10, 1 });

	// Generate Delaunay tessellation.
	DelaunayTessellation tessellation;
	if(!tessellation.generateTessellation(cell(), positions()->constDataPoint3(), positions()->size(), ghostLayerSize,
			selection() ? selection()->constDataInt() : nullptr, this))
		return false;

	nextProgressSubStep();

	// Determines the grain a Delaunay cell belongs to.
	auto tetrahedronRegion = [this, &tessellation](DelaunayTessellation::CellHandle cell) {
		std::array<int,4> clusters;
		for(int v = 0; v < 4; v++)
			clusters[v] = atomClusters()->getInt(tessellation.vertexIndex(tessellation.cellVertex(cell, v)));
		std::sort(std::begin(clusters), std::end(clusters));
		return (*most_common(std::begin(clusters), std::end(clusters)) + 1);
	};

	// Assign triangle faces to grains.
	auto prepareMeshFace = [this, &tessellation](PartitionMeshData::Face* face, const std::array<int,3>& vertexIndices, const std::array<DelaunayTessellation::VertexHandle,3>& vertexHandles, DelaunayTessellation::CellHandle cell) {
		face->region = tessellation.getUserField(cell) - 1;
	};

	// Cross-links adjacent manifolds.
	auto linkManifolds = [](PartitionMeshData::Edge* edge1, PartitionMeshData::Edge* edge2) {
		OVITO_ASSERT(edge1->nextManifoldEdge == nullptr || edge1->nextManifoldEdge == edge2);
		OVITO_ASSERT(edge2->nextManifoldEdge == nullptr || edge2->nextManifoldEdge == edge1);
		OVITO_ASSERT(edge2->vertex2() == edge1->vertex1());
		OVITO_ASSERT(edge2->vertex1() == edge1->vertex2());
		OVITO_ASSERT(edge1->face()->oppositeFace == nullptr || edge1->face()->oppositeFace == edge2->face());
		OVITO_ASSERT(edge2->face()->oppositeFace == nullptr || edge2->face()->oppositeFace == edge1->face());
		edge1->nextManifoldEdge = edge2;
		edge2->nextManifoldEdge = edge1;
		edge1->face()->oppositeFace = edge2->face();
		edge2->face()->oppositeFace = edge1->face();
	};

	ManifoldConstructionHelper<PartitionMeshData, true, true> manifoldConstructor(tessellation, *_mesh, alpha, positions());
	if(!manifoldConstructor.construct(tetrahedronRegion, this, prepareMeshFace, linkManifolds))
		return false;
	_spaceFillingGrain = manifoldConstructor.spaceFillingRegion();

	nextProgressSubStep();

	std::vector<PartitionMeshData::Edge*> visitedEdges;
	std::vector<PartitionMeshData::Vertex*> visitedVertices;
	size_t oldVertexCount = _mesh->vertices().size();
	for(size_t vertexIndex = 0; vertexIndex < oldVertexCount; vertexIndex++) {
		if(isCanceled())
			return false;

		PartitionMeshData::Vertex* vertex = _mesh->vertices()[vertexIndex];
		visitedEdges.clear();
		// Visit all manifolds that this vertex is part of.
		for(PartitionMeshData::Edge* startEdge = vertex->edges(); startEdge != nullptr; startEdge = startEdge->nextVertexEdge()) {
			if(std::find(visitedEdges.cbegin(), visitedEdges.cend(), startEdge) != visitedEdges.cend()) continue;
			// Traverse the manifold around the current vertex edge by edge.
			// Detect if there are two edges connecting to the same neighbor vertex.
			visitedVertices.clear();
			PartitionMeshData::Edge* endEdge = startEdge;
			PartitionMeshData::Edge* currentEdge = startEdge;
			do {
				OVITO_ASSERT(currentEdge->vertex1() == vertex);
				OVITO_ASSERT(std::find(visitedEdges.cbegin(), visitedEdges.cend(), currentEdge) == visitedEdges.cend());

				if(std::find(visitedVertices.cbegin(), visitedVertices.cend(), currentEdge->vertex2()) != visitedVertices.cend()) {
					// Encountered the same neighbor vertex twice.
					// That means the manifold is self-intersecting and we should split the central vertex

					// Retrieve the other edge where the manifold intersects itself.
					auto iter = std::find_if(visitedEdges.rbegin(), visitedEdges.rend(), [currentEdge](PartitionMeshData::Edge* e) {
						return e->vertex2() == currentEdge->vertex2();
					});
					OVITO_ASSERT(iter != visitedEdges.rend());
					PartitionMeshData::Edge* otherEdge = *iter;

					// Rewire edges to produce two separate manifolds.
					PartitionMeshData::Edge* oppositeEdge1 = otherEdge->unlinkFromOppositeEdge();
					PartitionMeshData::Edge* oppositeEdge2 = currentEdge->unlinkFromOppositeEdge();
					currentEdge->linkToOppositeEdge(oppositeEdge1);
					otherEdge->linkToOppositeEdge(oppositeEdge2);

					// Split the vertex.
					PartitionMeshData::Vertex* newVertex = _mesh->createVertex(vertex->pos());

					// Transfer one group of manifolds to the new vertex.
					std::vector<PartitionMeshData::Edge*> transferredEdges;
					std::deque<PartitionMeshData::Edge*> edgesToBeVisited;
					edgesToBeVisited.push_back(otherEdge);
					do {
						PartitionMeshData::Edge* edge = edgesToBeVisited.front();
						edgesToBeVisited.pop_front();
						PartitionMeshData::Edge* iterEdge = edge;
						do {
							PartitionMeshData::Edge* iterEdge2 = iterEdge;
							do {
								if(std::find(transferredEdges.cbegin(), transferredEdges.cend(), iterEdge2) == transferredEdges.cend()) {
									vertex->transferEdgeToVertex(iterEdge2, newVertex);
									transferredEdges.push_back(iterEdge2);
									edgesToBeVisited.push_back(iterEdge2);
								}
								iterEdge2 = iterEdge2->oppositeEdge()->nextManifoldEdge;
								OVITO_ASSERT(iterEdge2 != nullptr);
							}
							while(iterEdge2 != iterEdge);
							iterEdge = iterEdge->prevFaceEdge()->oppositeEdge();
						}
						while(iterEdge != edge);
					}
					while(!edgesToBeVisited.empty());

					if(otherEdge == endEdge) {
						endEdge = currentEdge;
					}
				}
				visitedVertices.push_back(currentEdge->vertex2());
				visitedEdges.push_back(currentEdge);

				currentEdge = currentEdge->prevFaceEdge()->oppositeEdge();
			}
			while(currentEdge != endEdge);
		}
	}

	// Smooth the generated triangle mesh.
	PartitionMesh::smoothMesh(*_mesh, cell(), _meshSmoothingLevel, this);

	// Make sure every mesh vertex is only part of one surface manifold.
	_mesh->duplicateSharedVertices();

	endProgressSubSteps();

	return true;
}

}	// End of namespace
}	// End of namespace
}	// End of namespace
