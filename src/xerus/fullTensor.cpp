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
 * @brief Implementation of the FullTensor class.
 */

#include <xerus/fullTensor.h>
#include <xerus/sparseTensor.h>
#include <xerus/tensorNetwork.h>
#include <xerus/blasLapackWrapper.h>
#include <xerus/selectedFunctions.h>
#include <cstring>

namespace xerus {
    /*- - - - - - - - - - - - - - - - - - - - - - - - - - Constructors - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    FullTensor::FullTensor() : FullTensor(std::vector<size_t>({})) { }

    FullTensor::FullTensor(const FullTensor&  _other) : Tensor(_other), data(_other.data) { }

    FullTensor::FullTensor(      FullTensor&& _other) : Tensor(std::move(_other)), data(_other.data) { }
    
    FullTensor::FullTensor(const Tensor&  _other) : Tensor(_other), data(_other.is_sparse() ? std::shared_ptr<value_t>(new value_t[size], internal::array_deleter_vt) : static_cast<const FullTensor&>(_other).data) {
		if(_other.is_sparse()) {
			misc::array_set_zero(data.get(), size);
			for(const std::pair<size_t, value_t>& entry : *static_cast<const SparseTensor&>(_other).entries) {
				data.get()[entry.first] = entry.second;
			}
		}
	}

    FullTensor::FullTensor(      Tensor&& _other) : Tensor(std::move(_other)), data(_other.is_sparse() ? std::shared_ptr<value_t>(new value_t[size], internal::array_deleter_vt) : static_cast<FullTensor&>(_other).data) {
		if(_other.is_sparse()) {
			misc::array_set_zero(data.get(), size);
			for(const std::pair<size_t, value_t>& entry : *static_cast<const SparseTensor&>(_other).entries) {
				data.get()[entry.first] = entry.second;
			}
		}
	}
    
    FullTensor::FullTensor(const TensorNetwork& _other) : FullTensor(*_other.fully_contracted_tensor()) { }

    FullTensor::FullTensor(const size_t _degree) : FullTensor(std::vector<size_t>(_degree, 1)) { }
    
    FullTensor::FullTensor(const std::vector<size_t>&  _dimensions, _unused_ DONT_SET_ZERO) : Tensor(_dimensions),            data(new value_t[size], internal::array_deleter_vt) { }
        
    FullTensor::FullTensor(      std::vector<size_t>&& _dimensions, _unused_ DONT_SET_ZERO) : Tensor(std::move(_dimensions)), data(new value_t[size], internal::array_deleter_vt) { }
    
    FullTensor::FullTensor(const std::vector<size_t>&  _dimensions) : FullTensor(_dimensions, DONT_SET_ZERO()) {
        misc::array_set_zero(data.get(), size);
    }
    
    FullTensor::FullTensor(      std::vector<size_t>&& _dimensions) : FullTensor(std::move(_dimensions), DONT_SET_ZERO()) {
        misc::array_set_zero(data.get(), size);
    }
    
    FullTensor::FullTensor(const std::vector<size_t> & _dimensions, std::unique_ptr<value_t[]>&& _data) : Tensor(_dimensions), data(_data.release(), internal::array_deleter_vt) { }
        
    FullTensor::FullTensor(      std::vector<size_t>&& _dimensions, std::unique_ptr<value_t[]>&& _data) : Tensor(std::move(_dimensions)), data(_data.release(), internal::array_deleter_vt) { }
    
    
    
    Tensor* FullTensor::get_copy() const {
        return new FullTensor(*this);
    }
    
    Tensor* FullTensor::get_moved_copy() {
        return new FullTensor(std::move(*this));
    }
    
    Tensor* FullTensor::construct_new() const {
        return new FullTensor();
    }
    
    Tensor* FullTensor::construct_new(const std::vector<size_t>&  _dimensions) const {
        return new FullTensor(_dimensions);
    }
    
    Tensor* FullTensor::construct_new(      std::vector<size_t>&& _dimensions) const {
        return new FullTensor(std::move(_dimensions));
    }
    
    Tensor* FullTensor::construct_new(const std::vector<size_t>&  _dimensions, DONT_SET_ZERO) const {
        return new FullTensor(_dimensions, DONT_SET_ZERO());
    }
    
    Tensor* FullTensor::construct_new(      std::vector<size_t>&& _dimensions, DONT_SET_ZERO) const {
        return new FullTensor(std::move(_dimensions), DONT_SET_ZERO());
    }

    /*- - - - - - - - - - - - - - - - - - - - - - - - - - Internal Helper functions - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

    void FullTensor::ensure_own_data() {
        if(!data.unique()) {
            value_t* const oldDataPtr = data.get();
            data.reset(new value_t[size], internal::array_deleter_vt);
            misc::array_copy(data.get(), oldDataPtr, size);
        }
    }


    void FullTensor::ensure_own_data_no_copy() {
        if(!data.unique()) {
            data.reset(new value_t[size], internal::array_deleter_vt);
        }
    }

    void FullTensor::apply_factor() {
        if(has_factor()) {
            ensure_own_data();
            misc::array_scale(data.get(), factor, size);
            factor = 1.0;
        }
    }

    void FullTensor::ensure_own_data_and_apply_factor() {
        ensure_own_data();
        if(has_factor()) {
            misc::array_scale(data.get(), factor, size);
            factor = 1.0;
        }
    }
        
    /*- - - - - - - - - - - - - - - - - - - - - - - - - - Standard operators - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    FullTensor& FullTensor::operator=(const FullTensor& _other) {
        dimensions = _other.dimensions;
        size = _other.size;
        factor = _other.factor;
        data = _other.data;
        return *this;
    }

    FullTensor& FullTensor::operator=(FullTensor&& _other) {
        std::swap(dimensions, _other.dimensions);
        std::swap(size, _other.size);
        factor = _other.factor;
        std::swap(data, _other.data);
        return *this;
    }

    /*- - - - - - - - - - - - - - - - - - - - - - - - - - Basic arithmetics - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    FullTensor& FullTensor::operator+=(const Tensor& _other) {
        REQUIRE(dimensions == _other.dimensions, "In FullTensor sum the dimensions must conincde");
        ensure_own_data();
		value_t* const dataPtr = data.get();
		
		PA_START;
		if(_other.is_sparse()) {
			if(has_factor() || _other.has_factor()) {
				const value_t factorToApply = _other.factor/factor;
				for(const std::pair<size_t, value_t>& entry : *static_cast<const SparseTensor&>(_other).entries) {
					dataPtr[entry.first] += factorToApply*entry.second;
				}
			} else {
				for(const std::pair<size_t, value_t>& entry : *static_cast<const SparseTensor&>(_other).entries) {
					dataPtr[entry.first] += entry.second;
				}
			}
		} else {
			if(has_factor()) {
				misc::array_scale_add(factor, dataPtr, _other.factor, static_cast<const FullTensor&>(_other).data.get(), size);
				factor = 1.0;
			} else {
				misc::array_add(dataPtr, _other.factor, static_cast<const FullTensor&>(_other).data.get(), size);
			}
		}
        PA_END("ADD/SUB", "FullTensor ADD", misc::to_string(size));
        return *this;
    }

    FullTensor FullTensor::operator+(const Tensor& _other) const {
        FullTensor result(*this);
        result += _other;
        return result;
    }

    FullTensor& FullTensor::operator-=(const Tensor& _other) {
        REQUIRE(dimensions == _other.dimensions, "In FullTensor subtraction the dimensions must conincde");
		ensure_own_data();
		value_t* const dataPtr = data.get();
		
		PA_START;
		if(_other.is_sparse()) {
			if(has_factor() || _other.has_factor()) {
				const value_t factorToApply = _other.factor/factor;
				for(const std::pair<size_t, value_t>& entry : *static_cast<const SparseTensor&>(_other).entries) {
					dataPtr[entry.first] -= factorToApply*entry.second;
				}
			} else {
				for(const std::pair<size_t, value_t>& entry : *static_cast<const SparseTensor&>(_other).entries) {
					dataPtr[entry.first] -= entry.second;
				}
			}
		} else {
			if(has_factor()) {
				misc::array_scale_add(factor, data.get(), -1.0*_other.factor, static_cast<const FullTensor&>(_other).data.get(), size);
				factor = 1.0;
			} else {
				misc::array_add(data.get(), -1.0*_other.factor, static_cast<const FullTensor&>(_other).data.get(), size);
			}
		}
        PA_END("ADD/SUB", "FullTensor SUB", misc::to_string(size));
        return *this;
    }

    FullTensor FullTensor::operator-(const Tensor& _other) const {
        FullTensor result(*this);
        result -= _other;
        return result;
    }

    FullTensor FullTensor::operator*(const value_t _factor) const {
        FullTensor result(*this);
        result *= _factor;
        return result;
    }

    FullTensor FullTensor::operator/(const value_t _divisor) const {
        FullTensor result(*this);
        result /= _divisor;
        return result;
    }

    
    /*- - - - - - - - - - - - - - - - - - - - - - - - - - Access - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    value_t& FullTensor::operator[](const size_t _i) {
        ensure_own_data_and_apply_factor();
        return data.get()[_i];
    }

    value_t FullTensor:: operator[](const size_t _i) const {
        return factor*data.get()[_i];
    }


    value_t& FullTensor::operator[](const std::vector<size_t>& _indices) {
        REQUIRE(_indices.size() == dimensions.size(), "Wrong number of indices given " << _indices.size() << " != " << dimensions.size());
        ensure_own_data_and_apply_factor();
        size_t finalIndex = 0;
        for(size_t i = 0; i < _indices.size(); ++i) {
            REQUIRE(_indices[i] < dimensions[i], "Index "<< i <<" out of bounds " << _indices[i] << " >=! " << dimensions[i]);
            finalIndex *= dimensions[i];
            finalIndex += _indices[i];
        }
        return data.get()[finalIndex];
    }

    value_t FullTensor::operator[](const std::vector<size_t>& _indices) const {
        REQUIRE(_indices.size() == dimensions.size(), "Wrong number of indices given " << _indices.size() << " != " << dimensions.size());
        size_t finalIndex = 0;
        for(size_t i = 0; i < _indices.size(); ++i) {
            REQUIRE(_indices[i] < dimensions[i], "Index "<< i <<" out of bounds " << _indices[i] << " >=! " << dimensions[i]);
            finalIndex *= dimensions[i];
            finalIndex += _indices[i];
        }
        return factor*data.get()[finalIndex];
    }

        
    /*- - - - - - - - - - - - - - - - - - - - - - - - - - Modififiers - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    
    void FullTensor::reset(const std::vector<size_t>&  _newDim, DONT_SET_ZERO) {
        size_t oldDataSize = size;
        change_dimensions(_newDim);
        factor = 1.0;
        if(oldDataSize != size) {
            data.reset(new value_t[size], internal::array_deleter_vt);
        }
    }
    
    void FullTensor::reset(      std::vector<size_t>&& _newDim, DONT_SET_ZERO) {
        size_t oldDataSize = size;
        change_dimensions(std::move(_newDim));
        factor = 1.0;
        if(oldDataSize != size) {
            data.reset(new value_t[size], internal::array_deleter_vt);
        }
    }
    
    void FullTensor::reset(const std::vector<size_t>&  _newDim) {
        size_t oldDataSize = size;
        change_dimensions(_newDim);
        factor = 1.0;
        if(oldDataSize != size) {
            data.reset(new value_t[size], internal::array_deleter_vt);
        } else {
            ensure_own_data_no_copy();
        }
        memset(data.get(), 0, size*sizeof(value_t));
    }
    
    void FullTensor::reset(      std::vector<size_t>&& _newDim) {
        size_t oldDataSize = size;
        change_dimensions(std::move(_newDim));
        factor = 1.0;
        if(oldDataSize != size) {
            data.reset(new value_t[size], internal::array_deleter_vt);
        } else {
            ensure_own_data_no_copy();
        }
        memset(data.get(), 0, size*sizeof(value_t));
    }

    void FullTensor::resize_dimension(const size_t _n, const size_t _newDim, size_t _cutPos) {
        REQUIRE(_n < degree(), "Can't resize dimension " << _n << " as the tensor is only order " << degree());
        REQUIRE(_newDim > 0, "Dimension must be larger than 0! Is " << _newDim);
        
        if (dimensions[_n] == _newDim) { return; }  // Trivial case: Nothing to do
        
        size_t newStepSize = 1;
        size_t blockCount = 1;
        size_t oldStepSize = 1;
        for (size_t i=degree()-1; i>_n; --i) {
            oldStepSize *= dimensions[i];
        }
        newStepSize = oldStepSize * _newDim;
        oldStepSize = oldStepSize * dimensions[_n];
        blockCount = size / oldStepSize; //  == product of dim[i] for i=0 to _n-1
        
        size_t newsize = blockCount*newStepSize;
        REQUIRE(newsize == size/dimensions[_n] * _newDim, 
                dimensions[_n] << " " << _newDim << " " << oldStepSize << " " << newStepSize << " " << blockCount
                << size << " " << newsize);
        value_t *tmp = new value_t[newsize];
        if (newStepSize > oldStepSize) {
            if (_cutPos < _newDim) {
                size_t numInsert = (newStepSize-oldStepSize);
                _cutPos *= oldStepSize / dimensions[_n];
                for (size_t i=0; i<blockCount; ++i) {
                    memcpy(tmp+i*newStepSize, data.get()+i*oldStepSize, _cutPos*sizeof(value_t)); // TODO use array_copy
                    memset(tmp+i*newStepSize+_cutPos, 0, numInsert*sizeof(double));
                    memcpy(tmp+i*newStepSize+_cutPos+numInsert, data.get()+i*oldStepSize+_cutPos, (oldStepSize-_cutPos)*sizeof(value_t));
                }
            } else {
                for (size_t i=0; i<blockCount; ++i) {
                    memcpy(tmp+i*newStepSize, data.get()+i*oldStepSize, oldStepSize*sizeof(value_t));
                    memset(tmp+i*newStepSize+oldStepSize, 0, (newStepSize-oldStepSize)*sizeof(double));
                }
            }
        } else { // newStepSize <= oldStepSize
            if (_cutPos < _newDim) {
                _cutPos *= oldStepSize / dimensions[_n];
                size_t diffSize = newStepSize - _cutPos;
                for (size_t i=0; i<blockCount; ++i) {
                    memcpy(tmp+i*newStepSize, data.get()+i*oldStepSize, _cutPos*sizeof(value_t));
                    memcpy(tmp+i*newStepSize+_cutPos, data.get()+(i+1)*oldStepSize-diffSize, diffSize*sizeof(value_t));
                }
            } else {
                for (size_t i=0; i<blockCount; ++i) {
                    memcpy(tmp+i*newStepSize, data.get()+i*oldStepSize, newStepSize*sizeof(value_t));
                }
            }
        }
        dimensions[_n] = _newDim;
        size = newsize;
        data.reset(tmp, internal::array_deleter_vt);

        REQUIRE(size == misc::product(dimensions), "");
    }

    void FullTensor::remove_slate(uint _indexNb, uint _pos) {
        REQUIRE(_indexNb < degree(), "");
        REQUIRE(_pos < dimensions[_indexNb], _pos << " " << dimensions[_indexNb]);
        REQUIRE(dimensions[_indexNb] > 1, "");
        
        resize_dimension(_indexNb, dimensions[_indexNb]-1, _pos);
    }
    
    
	void FullTensor::fix_slate(const size_t _dimension, const size_t _slatePosition) {
		REQUIRE(_slatePosition < dimensions[_dimension], "The given slatePosition must be smaller than the corresponding dimension. Here " << _slatePosition << " >= " << dimensions[_dimension]);
		
		size_t stepCount = 1, blockSize = 1;
		for(size_t i = 0; i < _dimension; ++i) { stepCount *= dimensions[i]; }
		for(size_t i = _dimension+1; i < dimensions.size(); ++i) { blockSize *= dimensions[i]; }
		
		const size_t stepSize = dimensions[_dimension]*blockSize;
		size_t inputPosition = _slatePosition*blockSize;
		
		value_t * const newData = new value_t[stepCount*blockSize];
		
		// Copy data
		for(size_t i = 0; i < stepCount; ++i) {
			misc::array_copy(newData+i*blockSize, data.get()+inputPosition, blockSize);
			inputPosition += stepSize;
		}
		
		// Set data
		data.reset(newData, &internal::array_deleter_vt);
		
		// Adjust dimensions
		dimensions.erase(dimensions.begin()+_dimension);
		size = stepCount*blockSize;
	}

    //TODO Allow more 2d
    void FullTensor::modify_diag_elements(const std::function<void(value_t&)>& _f) {
        REQUIRE(degree() == 2, "Diagonal elements are only well defined if degree equals two. Here: "  << degree());
        ensure_own_data_and_apply_factor();
        value_t* const realData = data.get();
        const size_t numDiags = std::min(dimensions[0], dimensions[1]);
        const size_t N = dimensions[1];
        for(size_t i=0; i<numDiags; ++i){
            _f(realData[i+i*N]);
        }
    }
    
    //TODO Allow more 2d
    void FullTensor::modify_diag_elements(const std::function<void(value_t&, const size_t)>& _f) {
        REQUIRE(degree() == 2, "Diagonal elements are only well defined if degree equals two. Here: "  << degree());
        ensure_own_data_and_apply_factor();
        value_t* const realData = data.get();
        const size_t numDiags = std::min(dimensions[0], dimensions[1]);
        const size_t N = dimensions[1];
        for(size_t i=0; i<numDiags; ++i){
            _f(realData[i+i*N], i);
        }
    }

    void FullTensor::modify_elements(const std::function<void(value_t&)>& _f) {
        ensure_own_data_and_apply_factor();
        value_t* const realData = data.get();
        for(size_t i=0; i<size; ++i){ _f(realData[i]); }
    }
    
    #if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 7) || defined(__clang__)
    void FullTensor::modify_elements(const std::function<void(value_t&, const size_t)>& _f) {
        ensure_own_data_and_apply_factor();
        value_t* const realData = data.get();
        for(size_t i=0; i<size; ++i){ _f(realData[i], i); }
    }
    
    void FullTensor::modify_elements(const std::function<void(value_t&, const std::vector<size_t>&)>& _f) {
        ensure_own_data_and_apply_factor();
        value_t* const realData = data.get();
        
        std::vector<size_t> multIdx(degree(), 0);
        size_t idx = 0;
        while (true) {
            _f(realData[idx], multIdx);
            // increasing indices
            idx++;
            size_t changingIndex = degree()-1;
            multIdx[changingIndex]++;
            while(multIdx[changingIndex] == dimensions[changingIndex]) {
                multIdx[changingIndex] = 0;
                changingIndex--;
                // Return on overflow 
                if(changingIndex >= degree()) { return; }
                multIdx[changingIndex]++;
            }
        }
    }
    #endif


    FullTensor FullTensor::entrywise_product(const FullTensor &_A, const FullTensor &_B) {
		REQUIRE(_A.dimensions == _B.dimensions, "entrywise product ill-defined for non-equal dimensions");
		FullTensor result(_A);
		for (size_t i=0; i<result.size; ++i) {
			result[i] *= _B[i];
		}
		return result;
	}

    /*- - - - - - - - - - - - - - - - - - - - - - - - - - Higher functions - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

    size_t FullTensor::count_non_zero_entries(const value_t _eps) const {
        size_t count = 0;
        for(size_t i = 0; i < size; ++i) {
            if(std::abs(data.get()[i]) > _eps ) { count++; } 
        }
        return count;
    }
    
    
	bool FullTensor::all_entries_valid() const {
		for(size_t i = 0; i < size; ++i) {
            if(!std::isfinite(data.get()[i])) { return false; } 
        }
        return true;
	}
    
    value_t FullTensor::frob_norm() const {
        return std::abs(factor)*blasWrapper::two_norm(data.get(), size);
    }


    /*- - - - - - - - - - - - - - - - - - - - - - - - - - Miscellaneous - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    
    bool FullTensor::is_sparse() const {
        return false;
    }

    std::string FullTensor::to_string() const {
        if (degree() == 0) return xerus::misc::to_string(data.get()[0]);
        std::string result;
        for (size_t i=0; i<size; ++i) {
            result += xerus::misc::to_string(factor*data.get()[i]) + " ";
            if ((i+1) % (size / dimensions[0]) == 0) {
                result += '\n';
            } else if (degree() > 1 && (i+1) % (size / dimensions[0] / dimensions[1]) == 0) {
                result += '\t';
            } else if (degree() > 2 && (i+1) % (size / dimensions[0] / dimensions[1] / dimensions[2]) == 0) {
                result += "/ ";
            }
        }
        return result;
    }


    bool FullTensor::compare_to_data(std::vector<value_t> _values, const double _eps) const {
        if(size != _values.size()) { return false; }
        for(size_t i=0; i < size; ++i) {
            if(std::abs(factor*data.get()[i]-_values[i]) > _eps) { return false; }
        }
        return true;
    }

    bool FullTensor::compare_to_data(const value_t* _values, const double _eps) const {
        for(size_t i=0; i < size; ++i) {
            if(std::abs(factor*data.get()[i]-_values[i]) > _eps) { return false; }
        }
        return true;
    }
    
    /*- - - - - - - - - - - - - - - - - - - - - - - - - - External functions - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    
	FullTensor operator+(const SparseTensor& _lhs, const FullTensor& _rhs) { return _rhs +_lhs; }
    
    FullTensor operator-(const SparseTensor& _lhs, const FullTensor& _rhs) {
        FullTensor result = _rhs-_lhs;
        result.factor *= -1;
        return result;
    }
}


