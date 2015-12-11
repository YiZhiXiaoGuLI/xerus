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
 * @brief Implementation of the indexed tensor network operators.
 */

#include <xerus/indexedTensor_TN_operators.h>
#include <xerus/indexedTensor_tensor_operators.h>
#include <xerus/index.h>
#include <xerus/tensor.h>
#include <xerus/tensorNetwork.h>
#include <xerus/misc/check.h>
#include <xerus/misc/missingFunctions.h>

namespace xerus {
    
    template<> 
    void IndexedTensorWritable<Tensor>::operator=(IndexedTensorReadOnly<TensorNetwork>&& _rhs) {
		REQUIRE(_rhs.tensorObjectReadOnly->is_valid_network(), "Invald Network");
		_rhs.assign_indices();
		std::vector<Index> rightIndices = _rhs.indices;
		TensorNetwork cpy(*_rhs.tensorObjectReadOnly);
		TensorNetwork::trace_out_double_indices(rightIndices, cpy(rightIndices));
        
        std::set<size_t> all;
        for (size_t i=0; i < cpy.nodes.size(); ++i) {
            all.insert(i);
        }

		size_t res = cpy.contract(all);
		
		std::vector<Index> externalOrder;
		for(size_t i = 0; i < cpy.nodes[res].neighbors.size(); ++i) { externalOrder.emplace_back(); }
		
		std::vector<Index> internalOrder;
		for(const TensorNetwork::Link& link: cpy.nodes[res].neighbors) {
			REQUIRE(link.external, "Internal Error " << link.other << " " << link.indexPosition);
			internalOrder.emplace_back(externalOrder[link.indexPosition]);
		}
		
		assign_indices(get_eval_degree(rightIndices));
		std::vector<Index> outOrder;
		for (const Index &idx : indices) {
			REQUIRE(misc::contains(rightIndices, idx), "Every index on the LHS must appear somewhere on the RHS");
			size_t spanSum = 0;
			for (size_t j = 0; rightIndices[j] != idx; ++j) {
				spanSum += rightIndices[j].span;
			}
			
			for (size_t i=0; i < idx.span; ++i) {
				outOrder.push_back(externalOrder[spanSum+i]);
			}
		}
	
		(*tensorObject)(outOrder) = (*cpy.nodes[res].tensorObject)(internalOrder);
	}

    
    template<> void IndexedTensorWritable<TensorNetwork>::operator=(IndexedTensorReadOnly<Tensor>&& _rhs) {
		tensorObject->specialized_evaluation(std::move(*this), IndexedTensorMoveable<TensorNetwork>(new TensorNetwork(*_rhs.tensorObjectReadOnly), _rhs.indices)); // TODO change this to not casts
    }
    
    
    /// shuffles the external links of _lhs according to the indices of the indexedTensors
    /// lhs contains a copy of rhs, thus we have to swap the rhs.indices to resemble those of the lhs
    void TensorNetwork::shuffle_indices(std::vector<Index> &_currentIndices, IndexedTensorWritable<TensorNetwork>&& _lhs) {
		_lhs.assign_indices();
        // writeable copy of the left indices
        size_t passedDegree1=0;
        for (size_t i=0; i<_currentIndices.size(); passedDegree1+=_currentIndices[i].span, ++i) {
            if (_currentIndices[i]!= _lhs.indices[i]) {
				// find correct index
				size_t j=i+1;
				size_t passedDegree2=passedDegree1+_currentIndices[i].span;
				for (; j<_currentIndices.size(); passedDegree2+=_currentIndices[j].span, ++j) {
					if (_currentIndices[j] == _lhs.indices[i]) break;
				}
				if (j < _currentIndices.size()) {
					std::swap(_currentIndices[i],_currentIndices[j]);
					
					for (size_t n=0; n<_currentIndices[i].span; ++n) {
						_lhs.tensorObject->swap_external_links(passedDegree1+n,passedDegree2+n);
					}
				} else {
					// expand...
					LOG(fatal, "TN expansion marked as >won't fix<");
				}
			}
			REQUIRE(_currentIndices[i].span == _lhs.indices[i].span, "Index span mismatch");
        }
    }

    template<>
    void IndexedTensorWritable<TensorNetwork>::operator=(IndexedTensorReadOnly<TensorNetwork>&& _rhs) {
		tensorObject->specialized_evaluation(std::move(*this), std::move(_rhs));
    }

    
    IndexedTensorMoveable<TensorNetwork> operator*(IndexedTensorReadOnly<TensorNetwork>&& _lhs, IndexedTensorReadOnly<TensorNetwork>&& _rhs) {
        IndexedTensorMoveable<TensorNetwork> result;
		if(!_lhs.tensorObjectReadOnly->specialized_contraction(std::move(result), std::move(_lhs), std::move(_rhs)) && !_rhs.tensorObjectReadOnly->specialized_contraction(std::move(result), std::move(_rhs), std::move(_lhs))) {
            _lhs.assign_indices();
			result.tensorObject = new TensorNetwork(*_lhs.tensorObjectReadOnly);
            result.tensorObjectReadOnly = result.tensorObject;
            result.indices = _lhs.indices;
            result.deleteTensorObject = true;
            TensorNetwork::add_network_to_network(std::move(result), std::move(_rhs));
        }
        return result;
    }


    IndexedTensorMoveable<TensorNetwork> operator*(IndexedTensorMoveable<TensorNetwork>&&  _lhs, IndexedTensorReadOnly<TensorNetwork>&& _rhs) {
        IndexedTensorMoveable<TensorNetwork> result;
		if(!_lhs.tensorObjectReadOnly->specialized_contraction(std::move(result), std::move(_lhs), std::move(_rhs)) && !_rhs.tensorObjectReadOnly->specialized_contraction(std::move(result), std::move(_rhs), std::move(_lhs))) {
            _lhs.assign_indices();
			result.tensorObject = _lhs.tensorObject;
            result.tensorObjectReadOnly = _lhs.tensorObjectReadOnly;
            result.indices = _lhs.indices;
            result.deleteTensorObject = true;
            _lhs.deleteTensorObject = false;
            TensorNetwork::add_network_to_network(std::move(result), std::move(_rhs));
        }
        return result;
    }
    
    IndexedTensorMoveable<TensorNetwork> operator*(IndexedTensorReadOnly<TensorNetwork>&& _lhs, IndexedTensorMoveable<TensorNetwork>&& _rhs) {
		return operator*(std::move(_rhs), std::move(_lhs));
	}
	
	IndexedTensorMoveable<TensorNetwork> operator*(IndexedTensorMoveable<TensorNetwork>&& _lhs, IndexedTensorMoveable<TensorNetwork>&& _rhs) {
		return operator*(std::move(_lhs), static_cast<IndexedTensorReadOnly<TensorNetwork>&&>(_rhs));
	}

}