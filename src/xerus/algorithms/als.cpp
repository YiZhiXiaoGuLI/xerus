// Xerus - A General Purpose Tensor Library
// Copyright (C) 2014-2015 Benjamin Huber and Sebastian Wolf. 
// 
// Xerus is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
// 
// Xerus is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Affero General Public License for more details.
// 
// You should have received a copy of the GNU Affero General Public License
// along with Xerus. If not, see <http://www.gnu.org/licenses/>.
//
// For further information on Xerus visit https://libXerus.org 
// or contact us at contact@libXerus.org.

/**
* @file
* @brief Implementation of the ALS variants.
*/

#include <xerus/algorithms/als.h>
#include <xerus/basic.h>
#include <xerus/index.h>
#include <xerus/indexedTensorList.h>
#include <xerus/tensorNetwork.h>

#include <xerus/indexedTensorMoveable.h>
#include <xerus/indexedTensor_tensor_factorisations.h>

namespace xerus {

	// -------------------------------------------------------------------------------------------------------------------------
	//                                       local solvers
	// -------------------------------------------------------------------------------------------------------------------------
	
	void ALSVariant::lapack_solver(const TensorNetwork &_A, TensorNetwork &_x, const TensorNetwork &_b, const ALSAlgorithmicData &_data) {
		Tensor A(_A);
		Tensor b(_b);
		Tensor x;
		Index i,j;
		x(i&0) = b(j&0) / A(j/2, i/2);
		
//         REQUIRE(_x.degree() <= 3, "dmrg not yet implemented in lapack_solver");// TODO split result into d-2 tensors -> choose correct tensor as core!
	}
	
	// -------------------------------------------------------------------------------------------------------------------------
	//                                       helper functions
	// -------------------------------------------------------------------------------------------------------------------------
	
	/**
	 * @brief Finds the range of notes that need to be optimized and orthogonalizes @a _x properly
	 * @details finds full-rank nodes (these can wlog be set to identity and need not be optimized)
	 * requires cannonicalizeAtTheEnd and corePosAtTheEnd to be set
	 * sets optimizedRange
	 * modifies x
	 */
	void ALSVariant::ALSAlgorithmicData::prepare_x_for_als() {
		const size_t d = x.degree();
		Index r1,r2,n1,cr1;
		
		size_t firstOptimizedIndex = 0;
		size_t dimensionProd = 1;
		while (firstOptimizedIndex + 1 < d) {
			const size_t localDim = x.dimensions[firstOptimizedIndex];
			size_t newDimensionProd = dimensionProd * localDim;
			if (x.rank(firstOptimizedIndex) < newDimensionProd) {
				break;
			}
			
			Tensor& curComponent = x.component(firstOptimizedIndex);
			curComponent.reinterpret_dimensions({curComponent.dimensions[0]*curComponent.dimensions[1], curComponent.dimensions[2]});
			curComponent(r1,n1,r2) = curComponent(r1,cr1) * x.get_component(firstOptimizedIndex+1)(cr1,n1,r2);
			x.set_component(firstOptimizedIndex+1, std::move(curComponent));
			
			//TODO sparse
			x.set_component(firstOptimizedIndex, Tensor(
				{dimensionProd, localDim, newDimensionProd},
				[&](const std::vector<size_t> &_idx){
					if (_idx[0]*localDim + _idx[1] == _idx[2]) {
						return 1.0;
					} else {
						return 0.0;
					}
				})
			);
			
			x.require_correct_format();
			
			firstOptimizedIndex += 1;
			dimensionProd = newDimensionProd;
		}
		
		size_t firstNotOptimizedIndex = d;
		dimensionProd = 1;
		while (firstNotOptimizedIndex > firstOptimizedIndex+1) {
			const size_t localDim = x.dimensions[firstNotOptimizedIndex-1];
			size_t newDimensionProd = dimensionProd * localDim;
			if (x.rank(firstNotOptimizedIndex-2) < newDimensionProd) {
				break;
			}
			
			Tensor& curComponent = x.component(firstNotOptimizedIndex-1);
			curComponent.reinterpret_dimensions({curComponent.dimensions[0], curComponent.dimensions[1] * curComponent.dimensions[2]});
			curComponent(r1,n1,r2) = x.get_component(firstNotOptimizedIndex-2)(r1,n1,cr1) * curComponent(cr1,r2);
			x.set_component(firstNotOptimizedIndex-2, std::move(curComponent));
			
			//TODO sparse
			x.set_component(firstNotOptimizedIndex-1, Tensor(
				{newDimensionProd, localDim, dimensionProd},
				[&](const std::vector<size_t> &_idx){
					if (_idx[0] == _idx[1]*dimensionProd + _idx[2]) {
						return 1.0;
					} else {
						return 0.0;
					}
				})
			);

			x.require_correct_format();
			
			firstNotOptimizedIndex -= 1;
			dimensionProd = newDimensionProd;
		}
		
		if (cannonicalizeAtTheEnd && corePosAtTheEnd < firstOptimizedIndex) {
			x.assume_core_position(firstOptimizedIndex);
		} else {
			if (cannonicalizeAtTheEnd && corePosAtTheEnd >= firstNotOptimizedIndex) {
				x.assume_core_position(firstNotOptimizedIndex-1);
			}
			
			x.move_core(firstOptimizedIndex, true);
		}
		
		optimizedRange = std::pair<size_t, size_t>(firstOptimizedIndex, firstNotOptimizedIndex);
	}

	void ALSVariant::ALSAlgorithmicData::prepare_stacks() {
		const size_t d=x.degree();
		Index cr1, cr2, cr3, r1, r2, r3, n1, n2;
		
		Tensor tmpA({1,1,1}, [](){return 1.0;});
		Tensor tmpB({1,1}, [](){return 1.0;});
		
		xAxL.emplace_back(tmpA);
		bxL.emplace_back(tmpB);
		xAxR.emplace_back(tmpA);
		bxR.emplace_back(tmpB);
		
		for (size_t i = d-1; i > optimizedRange.first + ALS.sites - 1; --i) {
			if (&A) {
				tmpA(r1, r2, r3) = xAxR.back()(cr1, cr2, cr3) 
									* x.get_component(i)(r1, n1, cr1) 
									* A.get_component(i)(r2, n1, n2, cr2) 
									* x.get_component(i)(r3, n2, cr3);
				xAxR.emplace_back(std::move(tmpA));
			}
			tmpB(r1, r2) = bxR.back()(cr1, cr2) 
							* b.get_component(i)(r1, n1, cr1) 
							* x.get_component(i)(r2, n1, cr2);
			bxR.emplace_back(std::move(tmpB));
		}
		for (size_t i = 0; i < optimizedRange.first; ++i) {
			if (&A) {
				tmpA(r1, r2, r3) = xAxL.back()(cr1, cr2, cr3) 
									* x.get_component(i)(cr1,n1,r1) 
									* A.get_component(i)(cr2, n1, n2, r2) 
									* x.get_component(i)(cr3,n2,r3);
				xAxL.emplace_back(std::move(tmpA));
			}
			tmpB(r1, r2) = bxL.back()(cr1, cr2) 
							* b.get_component(i)(cr1, n1, r1) 
							* x.get_component(i)(cr2, n1, r2);
			bxL.emplace_back(std::move(tmpB));
		}
	}
	
	void ALSVariant::ALSAlgorithmicData::choose_energy_functional() {
		Index cr1, cr2, cr3, r1, r2, r3, n1, n2;
		if (&A) {
			residual_f = [&](){
				return frob_norm(A(n1/2,n2/2)*x(n2&0) - b(n1&0));
			};
			if (ALS.useResidualForEndCriterion) {
				energy_f = residual_f;
			} else {
				energy_f = [&](){
					Tensor res;
					// 0.5*<x,Ax> - <x,b>
					res() = 0.5*xAxR.back()(cr1, cr2, cr3) 
					* x.get_component(currIndex)(r1, n1, cr1) * A.get_component(currIndex)(r2, n1, n2, cr2) * x.get_component(currIndex)(r3, n2, cr3) 
					* xAxL.back()(r1, r2, r3)
					- bxR.back()(cr1, cr2) 
					* b.get_component(currIndex)(r1, n1, cr1) * x.get_component(currIndex)(r2, n1, cr2)
					* bxL.back()(r1, r2);
					return res[0];
				};
			}
		} else {
			residual_f = [&](){
				return frob_norm(x(n1&0) - b(n1&0));
			};
			if (ALS.useResidualForEndCriterion) {
				energy_f = residual_f;
			} else {
				energy_f = [&](){
					Tensor res;
					// 0.5*<x,Ax> - <x,b> = 0.5*|x_i|^2 - <x,b>
					res() = 0.5*x.get_component(currIndex)(r1, n1, cr1) * x.get_component(currIndex)(r1, n1, cr1) 
					- bxR.back()(cr1, cr2) 
					* b.get_component(currIndex)(r1, n1, cr1) * x.get_component(currIndex)(r2, n1, cr2)
					* bxL.back()(r1, r2);
					return res[0];
				};
			}
		}
	}
	
	ALSVariant::ALSAlgorithmicData::ALSAlgorithmicData(const ALSVariant &_ALS, const TTOperator &_A, TTTensor &_x, const TTTensor &_b) 
		: ALS(_ALS), A(_A), x(_x), b(_b)
	{
		targetRank = _x.ranks();
		cannonicalizeAtTheEnd = _x.cannonicalized;
		corePosAtTheEnd = _x.corePosition;
		prepare_x_for_als();
		prepare_stacks();
		currIndex = optimizedRange.first;
		direction = Increasing;
		choose_energy_functional();
	}

	void ALSVariant::ALSAlgorithmicData::move_to_next_index() {
		Index cr1, cr2, cr3, r1, r2, r3, n1, n2;
		Tensor tmpA, tmpB;
		if (direction == Increasing) {
			REQUIRE(currIndex+1 < optimizedRange.second, "ie");
			// Move core to next position
			x.move_core(currIndex+1, true);
			
			// Move one site to the right
			if (&A) {
				xAxR.pop_back();
				tmpA(r1, r2, r3) = xAxL.back()(cr1,cr2,cr3) 
									* x.get_component(currIndex)(cr1,n1,r1) 
									* A.get_component(currIndex)(cr2, n1, n2, r2) 
									* x.get_component(currIndex)(cr3,n2,r3);
				xAxL.emplace_back(std::move(tmpA));
			}
			
			bxR.pop_back();
			tmpB(r1, r2) = bxL.back()(cr1,cr2) 
							* b.get_component(currIndex)(cr1, n1, r1) 
							* x.get_component(currIndex)(cr2, n1, r2);
			bxL.emplace_back(std::move(tmpB));
			currIndex++;
		} else {
			REQUIRE(currIndex > optimizedRange.first, "ie");
			// Move core to next position
			x.move_core(currIndex-1, true);
			
			// move one site to the left
			if (&A) {
				xAxL.pop_back();
				tmpA(r1, r2, r3) = xAxR.back()(cr1,cr2,cr3) 
									* x.get_component(currIndex)(r1,n1,cr1) 
									* A.get_component(currIndex)(r2, n1, n2, cr2) 
									* x.get_component(currIndex)(r3,n2,cr3);
				xAxR.emplace_back(std::move(tmpA));
			}
			
			bxL.pop_back();
			tmpB(r1, r2) = bxR.back()(cr1,cr2) 
							* b.get_component(currIndex)(r1, n1, cr1) 
							* x.get_component(currIndex)(r2, n1, cr2);
			bxR.emplace_back(std::move(tmpB));
			currIndex--;
		}
	}
	
	
	// -------------------------------------------------------------------------------------------------------------------------
	//                                   the actual algorithm
	// -------------------------------------------------------------------------------------------------------------------------
	
	
	double ALSVariant::solve(const TTOperator *_Ap, TTTensor &_x, const TTTensor &_b, size_t _numHalfSweeps, value_t _convergenceEpsilon, PerformanceData &_perfData) const {
		const TTOperator &_A = *_Ap;
		LOG(ALS, "ALS("<< sites << ", " << minimumLocalResidual <<") called");
		#ifndef DISABLE_RUNTIME_CHECKS_
			_x.require_correct_format();
			_b.require_correct_format();
			REQUIRE(_x.degree() > 0, "");
			REQUIRE(_x.dimensions == _b.dimensions, "");
			REQUIRE(sites == 1, "dmrg not yet implemented");
			
			if (_Ap) {
				_A.require_correct_format();
				REQUIRE(_A.dimensions.size() == _b.dimensions.size()*2, "");
				for (size_t i=0; i<_x.dimensions.size(); ++i) {
					REQUIRE(_A.dimensions[i] == _x.dimensions[i], "");
					REQUIRE(_A.dimensions[i+_A.degree()/2] == _x.dimensions[i], "");
				}
			}
		#endif
		
		if (_Ap) {
			_perfData << "ALS for ||A*x - b||^2, x.dimensions: " << _x.dimensions << '\n'
					<< "A.ranks: " << _A.ranks() << '\n'
					<< "x.ranks: " << _x.ranks() << '\n'
					<< "b.ranks: " << _b.ranks() << '\n'
					<< "maximum number of half sweeps: " << _numHalfSweeps << '\n'
					<< "convergence epsilon: " << _convergenceEpsilon << '\n';
		} else {
			_perfData << "ALS for ||x - b||^2, x.dimensions: " << _x.dimensions << '\n'
					<< "x.ranks: " << _x.ranks() << '\n'
					<< "b.ranks: " << _b.ranks() << '\n'
					<< "maximum number of half sweeps: " << _numHalfSweeps << '\n'
					<< "convergence epsilon: " << _convergenceEpsilon << '\n';
		}
		_perfData.start();
		
		const size_t FLAG_FINISHED_HALFSWEEP = 1;
		const size_t FLAG_FINISHED_FULLSWEEP = 3; // contains the flag for a new halfsweep!
		ALSAlgorithmicData data(*this, _A, _x, _b);
		Index cr1, cr2, cr3, r1, r2, r3, n1, n2;
		
		TensorNetwork ATilde;
		TensorNetwork BTilde;
		value_t lastEnergy2 = 1e102;
		value_t lastEnergy = 1e101;
		value_t energy = 1e100;
		bool changedSmth = false;
		size_t halfSweepCount = 0;
		
		energy = data.energy_f();
		
		if (_perfData) {
			_perfData.stop_timer();
			value_t residual = data.residual_f();
			_perfData.continue_timer();
			_perfData.add(residual, _x, FLAG_FINISHED_FULLSWEEP);
		}
		
		while (true) {
			LOG(ALS, "Starting to optimize index " << data.currIndex);
			
			// Calculate Atilde and Btilde
			if (_Ap) {
				ATilde(r1,n1,cr1, r3,n2,cr3) = data.xAxL.back()(r1,r2,r3) * _A.get_component(data.currIndex)(r2, n1, n2, cr2) * data.xAxR.back()(cr1, cr2, cr3);
			}
			BTilde(r2,n1,cr2) = data.bxL.back()(r1,r2) * _b.get_component(data.currIndex)(r1, n1, cr1) * data.bxR.back()(cr1,cr2);
			
			// Change component tensor if the local residual is large enough
			if (_Ap) {
				if (minimumLocalResidual <= 0 || frob_norm(ATilde(r1^3, r2^3)*_x.get_component(data.currIndex)(r2^3) - BTilde(r1^3)) > minimumLocalResidual) {
					// TODO create subNetwork method
					// TODO dmrg...
					TensorNetwork tmpX;
					tmpX(n1&0) = _x.get_component(data.currIndex)(n1&0);
					localSolver(ATilde, tmpX, BTilde, data);
					_x.set_component(data.currIndex, std::move(tmpX.nodes[0].tensorObject));
					changedSmth = true;
				}
			} else {
				_x.component(data.currIndex) = Tensor(BTilde);
				changedSmth = true;
			}
			
			// Are we done with the sweep?
			if ((data.direction == Decreasing && data.currIndex==data.optimizedRange.first) 
				|| (data.direction == Increasing && data.currIndex==data.optimizedRange.second-sites)) 
			{
				LOG(ALS, "Sweep Done");
				halfSweepCount += 1;
				
				lastEnergy2 = lastEnergy;
				lastEnergy = energy;
				energy = data.energy_f();
				
				if (_perfData) {
					size_t flags = data.direction == Increasing ? FLAG_FINISHED_HALFSWEEP : FLAG_FINISHED_FULLSWEEP;
					if (!useResidualForEndCriterion) {
						_perfData.stop_timer();
						value_t residual = data.residual_f();
						_perfData.continue_timer();
						_perfData.add(residual, _x, flags);
					} else {
						_perfData.add(energy, _x, flags);
					}
				}
				
				// Conditions for loop termination
				if (!changedSmth || halfSweepCount == _numHalfSweeps 
						|| std::abs(lastEnergy-energy) < _convergenceEpsilon 
						|| std::abs(lastEnergy2-energy) < _convergenceEpsilon 
						|| (data.optimizedRange.second - data.optimizedRange.first<=sites)) {
					// we are done! yay
					LOG(ALS, "ALS done, " << energy << " " << lastEnergy << " " << std::abs(lastEnergy2-energy) << " " << std::abs(lastEnergy-energy) << " < " << _convergenceEpsilon);
					if (data.cannonicalizeAtTheEnd && preserveCorePosition) {
						_x.move_core(data.corePosAtTheEnd, true);
					}
					return energy;
				}
				
				// change walk direction
				data.direction = data.direction == Increasing ? Decreasing : Increasing;
				changedSmth = false;
			} else {
				// we are not done with the sweep, just calculate data for the perfdata
				if (_perfData) {
					_perfData.stop_timer();
					value_t residual = data.residual_f();
					_perfData.continue_timer();
					_perfData.add(residual, _x, 0);
				}
			}
			
			data.move_to_next_index();
		}
	}
	
	
	const ALSVariant ALS(1, 0, EPSILON, ALSVariant::lapack_solver);
	
	const ALSVariant DMRG(2, 0, EPSILON, ALSVariant::lapack_solver);
}
