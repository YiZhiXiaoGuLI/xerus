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
* @brief Implementation of the TensorNetwork class.
*/

#include <xerus/tensorNetwork.h>
#include <xerus/basic.h>
#include <xerus/index.h>
#include <xerus/tensor.h>
#include <xerus/indexedTensorList.h>
#include <xerus/indexedTensorMoveable.h>
#include <xerus/indexedTensor_tensor_factorisations.h>

#include <xerus/measurments.h>

#include <xerus/contractionHeuristic.h>
#include <xerus/cs_wrapper.h>

#include <xerus/misc/stringUtilities.h>

namespace xerus {
	const misc::NoCast<bool> TensorNetwork::NoZeroNode(false);
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Constructors - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	TensorNetwork::TensorNetwork(const misc::NoCast<bool> _addZeroNode) {
		if(_addZeroNode) {
			nodes.emplace_back(TensorNode(std::unique_ptr<Tensor>(new Tensor())));
		}
	}
	
	TensorNetwork::TensorNetwork(const Tensor& _other) : dimensions(_other.dimensions) {
		nodes.emplace_back(std::unique_ptr<Tensor>( new Tensor(_other)), init_from_dimension_array());
	}
	
	TensorNetwork::TensorNetwork(Tensor&& _other) : dimensions(_other.dimensions) { //NOTE don't use std::move here, because we need _other to be untouched to move it later
		nodes.emplace_back(std::unique_ptr<Tensor>(new Tensor(std::move(_other))), init_from_dimension_array());
	}
	
	TensorNetwork::TensorNetwork( std::unique_ptr<Tensor>&& _tensor) : dimensions(_tensor->dimensions) {
		nodes.emplace_back(std::move(_tensor), init_from_dimension_array());
	}
	
	/// Constructs the trivial network containing non-specified size-1 Tensor
	TensorNetwork::TensorNetwork(size_t _degree) : dimensions(std::vector<size_t>(_degree, 1)) {
		nodes.emplace_back(std::unique_ptr<Tensor>(new Tensor( std::vector<size_t>(_degree, 1))), init_from_dimension_array());
	}
	
	TensorNetwork* TensorNetwork::get_copy() const {
		return new TensorNetwork(*this);
	}
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Internal Helper functions - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	std::vector<TensorNetwork::Link> TensorNetwork::init_from_dimension_array() {
		std::vector<TensorNetwork::Link> newLinks;
		for (size_t d = 0; d < dimensions.size(); ++d) {
			externalLinks.emplace_back(0, d, dimensions[d], false);
			newLinks.emplace_back(-1, d, dimensions[d], true);
		}
		return newLinks;
	}
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Standard operators - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	
	
	TensorNetwork::operator Tensor() const {
		return *fully_contracted_tensor();
	}
	
	std::unique_ptr<Tensor> TensorNetwork::fully_contracted_tensor() const {
		require_valid_network();
		
		std::set<size_t> all;
		for(size_t i = 0; i < nodes.size(); ++i) { all.emplace_hint(all.end(), i); }
		
		TensorNetwork cpy(*this);
		size_t res = cpy.contract(all);
		
		std::vector<size_t> shuffle(degree());
		for(size_t i = 0; i < cpy.nodes[res].neighbors.size(); ++i) {
			REQUIRE(cpy.nodes[res].neighbors[i].external, "Internal Error");
			shuffle[i] = cpy.nodes[res].neighbors[i].indexPosition;
		}
		std::unique_ptr<Tensor> result(new Tensor(cpy.nodes[res].tensorObject->representation));
		
		reshuffle(*result, *cpy.nodes[res].tensorObject, shuffle);
		
		return result;
	}
	
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Access - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	value_t TensorNetwork::operator[](const size_t _position) const {
		require_valid_network();
		
		if (degree() == 0) {
			REQUIRE(_position == 0, "Tried to access non-existing entry of TN");
			value_t value = 1.0;
			for(const TensorNode& node : nodes) { value *= (*node.tensorObject)[0]; }
			return value;
		}
		
		std::vector<size_t> positions(degree());
		size_t remains = _position;
		for(size_t i = degree(); i > 1; --i) {
			positions[i-1] = remains%dimensions[i-1];
			remains /= dimensions[i-1];
		}
		positions[0] = remains;
		return operator[](positions);
	}
	
	value_t TensorNetwork::operator[](const Tensor::MultiIndex& _positions) const {
		require_valid_network();
		
		TensorNetwork partialCopy;
		partialCopy.nodes = nodes;
		
		// Set all external indices in copy to the fixed values and evaluate the tensorObject accordingly
		for(TensorNode& node : partialCopy.nodes) {
			// Fix slates in external links
			size_t killedDimensions = 0;
			for(size_t i = 0; i < node.neighbors.size(); ++i) {
				if(node.neighbors[i].external) {
					node.tensorObject->fix_slate(i-killedDimensions, _positions[node.neighbors[i].indexPosition]);
					killedDimensions++;
				}
			}
			
			// Remove all external links, because they don't exist anymore
			node.neighbors.erase(std::remove_if(node.neighbors.begin(), node.neighbors.end(), [](const TensorNetwork::Link& _test){return _test.external;}), node.neighbors.end());
			
			// Adjust the Links
			for(size_t i = 0; i < node.neighbors.size(); ++i) {
				partialCopy.nodes[node.neighbors[i].other].neighbors[node.neighbors[i].indexPosition].indexPosition = i;
			}
		}
		
		// Contract the complete network (there are not external Links)
		partialCopy.contract_unconnected_subnetworks();
		
		REQUIRE(partialCopy.nodes.size() == 1, "Internal Error.");
		
		return (*partialCopy.nodes[0].tensorObject)[0];
	}
	
	
	void TensorNetwork::measure(SinglePointMeasurmentSet& _measurments) const {
		std::vector<TensorNetwork> stack(degree()+1);
		stack[0] = *this;
		stack[0].reduce_representation();
		
		// Sort measurements
		sort(_measurments, degree()-1);
		
		// Handle first measurment
		for(size_t i = 0; i < degree(); ++i) {
			stack[i+1] = stack[i];
			stack[i+1].fix_slate(0, _measurments.positions[0][i]);
			stack[i+1].reduce_representation();
		}
		_measurments.measuredValues[0] = stack.back()[0];
		
		for(size_t j = 1; j < _measurments.size(); ++j) {
			REQUIRE(_measurments.positions[j-1] != _measurments.positions[j], "There were two identical measurements?");
			
			// Find the maximal recyclable stack position
			size_t rebuildIndex = 0;
			for(; rebuildIndex < degree(); ++rebuildIndex) {
				if(_measurments.positions[j-1][rebuildIndex] != _measurments.positions[j][rebuildIndex]) {
					break;
				}
			}
			
			// Rebuild stack
			for(size_t i = rebuildIndex; i < degree(); ++i) {
				stack[i+1] = stack[i];
				stack[i+1].fix_slate(0, _measurments.positions[j][i]);
				stack[i+1].reduce_representation();
			}
			
			_measurments.measuredValues[j] = stack.back()[0];
		}
	}
	

	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Basic arithmetics - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

	void TensorNetwork::operator*=(const value_t _factor) {
		REQUIRE(nodes.size() > 0, "There must not be a TTNetwork without any node");
		*nodes[0].tensorObject *= _factor;
	}
	
	void TensorNetwork::operator/=(const value_t _divisor) {
		REQUIRE(nodes.size() > 0, "There must not be a TTNetwork without any node");
		*nodes[0].tensorObject /= _divisor;
	}
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Indexing - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	IndexedTensor<TensorNetwork> TensorNetwork::operator()(const std::vector<Index> & _indices) {
		return IndexedTensor<TensorNetwork>(this, _indices, false);
	}
	
	IndexedTensor<TensorNetwork> TensorNetwork::operator()(	  std::vector<Index>&& _indices) {
		return IndexedTensor<TensorNetwork>(this, std::move(_indices), false);
	}
		
	IndexedTensorReadOnly<TensorNetwork> TensorNetwork::operator()(const std::vector<Index> & _indices) const {
		return IndexedTensorReadOnly<TensorNetwork>(this, _indices);
	}
	
	IndexedTensorReadOnly<TensorNetwork> TensorNetwork::operator()(	  std::vector<Index>&& _indices) const {
		return IndexedTensorReadOnly<TensorNetwork>(this, std::move(_indices));
	}
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Operator specializations - - - - - - - - - - - - - - - - - - - - - - - - - - */
	bool TensorNetwork::specialized_contraction(std::unique_ptr<IndexedTensorMoveable<TensorNetwork>>& _out, IndexedTensorReadOnly<TensorNetwork>&& _me , IndexedTensorReadOnly<TensorNetwork>&& _other ) const {
		return false; // A general tensor Network can't do anything specialized
	}
	
	bool TensorNetwork::specialized_sum(std::unique_ptr<IndexedTensorMoveable<TensorNetwork>>& _out, IndexedTensorReadOnly<TensorNetwork>&& _me, IndexedTensorReadOnly<TensorNetwork>&& _other) const {
		return false; // A general tensor Network can't do anything specialized
	}
	
	void TensorNetwork::specialized_evaluation(IndexedTensorWritable<TensorNetwork>&& _me, IndexedTensorReadOnly<TensorNetwork>&& _other) {
		// If tensorObject is not already identical copy it.
		if (_me.tensorObjectReadOnly != _other.tensorObjectReadOnly) {
			*_me.tensorObject = *_other.tensorObjectReadOnly;
		}
		
		_other.assign_indices();
		link_traces((*_me.tensorObject)(_other.indices));
		
		_me.assign_indices();

		// Swap indices accordingly
		size_t passedDegree1 = 0;
		for (size_t i = 0; i < _me.indices.size(); passedDegree1 += _me.indices[i].span, ++i) {
			if (_other.indices[i] != _me.indices[i]) {
				// Find the correct index in other
				size_t j = i+1;
				size_t passedDegree2 = passedDegree1+_other.indices[i].span;
				while(_other.indices[j] != _me.indices[i]) {
					passedDegree2 += _other.indices[j].span;
					++j;
					REQUIRE( j < _other.indices.size(), "RHS Index " << _other.indices[j] << " not found in LHS " << _me.indices);
				}
				
				std::swap(_other.indices[i], _other.indices[j]);
				
				for (size_t n = 0; n < _other.indices[i].span; ++n) {
					_me.tensorObject->swap_external_links(passedDegree1+n, passedDegree2+n);
				}
			}
			REQUIRE(_other.indices[i].span == _me.indices[i].span, "Index span mismatch");
		}
	}
	
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - Miscellaneous - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
	
	size_t TensorNetwork::degree() const {
		REQUIRE(externalLinks.size() == dimensions.size(), "invalid network, " << externalLinks.size() << " vs " << dimensions.size());
		return dimensions.size();
	}
	
	
	void TensorNetwork::reshuffle_nodes(const std::map<size_t, size_t> &_map) {
		reshuffle_nodes([&](size_t i){return _map.at(i);});
	}
	
	void TensorNetwork::reshuffle_nodes(const std::function<size_t(size_t)>& _f) {
		std::vector<TensorNode> newNodes(nodes.size());
		size_t newSize = 0;
		for (size_t i = 0; i < nodes.size(); ++i) {
			if (nodes[i].erased) { continue; }
			size_t newIndex = _f(i);
			newSize = std::max(newSize, newIndex+1);
			REQUIRE(newNodes[newIndex].erased, "Tried to shuffle two nodes to the same new position " << newIndex << " i= " << i);
			newNodes[newIndex] = nodes[i];
			for (TensorNetwork::Link &l : newNodes[newIndex].neighbors) {
				if (!l.external) { l.other = _f(l.other); }
			}
		}
		nodes = newNodes;
		nodes.resize(newSize);
		
		for (size_t i = 0; i < externalLinks.size(); ++i) {
			externalLinks[i].other = _f(externalLinks[i].other);
		}
	}
	
#ifndef DISABLE_RUNTIME_CHECKS_
	void TensorNetwork::require_valid_network(const bool _check_erased) const {
		REQUIRE(externalLinks.size() == dimensions.size(), "externalLinks.size() != dimensions.size()");
		REQUIRE(nodes.size() > 0, "There must always be at least one node!");
		
		// Per external link
		for (size_t n = 0; n < externalLinks.size(); ++n) {
			const TensorNetwork::Link &el = externalLinks[n];
			REQUIRE(el.other < nodes.size(), "n=" << n);
			REQUIRE(el.dimension > 0, "n=" << n);
			REQUIRE(el.dimension == dimensions[n], "n=" << n);
			REQUIRE(!el.external, "n=" << n);
			
			const TensorNode &other = nodes[el.other];
			REQUIRE(other.degree() > el.indexPosition, "n=" << n << " " << other.degree() << " vs " << el.indexPosition);
			REQUIRE(other.neighbors[el.indexPosition].external, "n=" << n);
			REQUIRE(other.neighbors[el.indexPosition].indexPosition == n, "n=" << n << " We have " << other.neighbors[el.indexPosition].indexPosition << " != " << n << " and el.other =" << el.other << " and el.indexPosition = " << el.indexPosition );
			REQUIRE(other.neighbors[el.indexPosition].dimension == el.dimension, "n=" << n << " " << other.neighbors[el.indexPosition].dimension << " vs " << el.dimension);
		}
		
		// Per node
		for (size_t n = 0; n < nodes.size(); ++n) {
			const TensorNode &currNode = nodes[n];
			REQUIRE(!_check_erased || !currNode.erased, "n=" << n);
			if (currNode.tensorObject) {
				REQUIRE(currNode.degree() == currNode.tensorObject->degree(), "n=" << n << " " << currNode.degree() << " vs " << currNode.tensorObject->degree());
			}
			// per neighbor
			for (size_t i=0; i<currNode.neighbors.size(); ++i) {
				const TensorNetwork::Link &el = currNode.neighbors[i];
				REQUIRE(el.dimension > 0, "n=" << n << " i=" << i);
				if (currNode.tensorObject) {
					REQUIRE(el.dimension==currNode.tensorObject->dimensions[i],  "n=" << n << " i=" << i << " " << el.dimension << " vs " << currNode.tensorObject->dimensions[i]);
				}
				
				if (!el.external) { // externals were already checked
					REQUIRE(el.other < nodes.size(), "n=" << n << " i=" << i << " " << el.other << " vs " << nodes.size());
					const TensorNode &other = nodes[el.other];
					REQUIRE(other.degree() > el.indexPosition, "n=" << n << " i=" << i << " " << other.degree() << " vs " << el.indexPosition);
					REQUIRE(!other.neighbors[el.indexPosition].external, "n=" << n << " i=" << i);
					REQUIRE(other.neighbors[el.indexPosition].other == n, "n=" << n << " i=" << i);
					REQUIRE(other.neighbors[el.indexPosition].indexPosition == i, "n=" << n << " i=" << i);
					REQUIRE(other.neighbors[el.indexPosition].dimension == el.dimension, "n=" << n << " i=" << i << " " << other.neighbors[el.indexPosition].dimension << " vs " << el.dimension);
				}
			}
		}
	}
#else
	/// No checks are performed with disabled checks... 
	void TensorNetwork::require_valid_network(bool _check_erased) const { }
#endif
	
	bool TensorNetwork::is_in_expected_format() const {
		require_valid_network();
		return true;
	}
	
	TensorNetwork TensorNetwork::stripped_subnet(const std::function<bool(size_t)>& _idF) const {
		TensorNetwork cpy(NoZeroNode);
		cpy.nodes.resize(nodes.size());
		cpy.dimensions = dimensions;
		cpy.externalLinks = externalLinks;
		for (size_t id = 0; id < nodes.size(); ++id) {
			if (!_idF(id)) continue;
			cpy.nodes[id] = nodes[id].strippped_copy();
			for (size_t i = 0; i < cpy.nodes[id].neighbors.size(); ++i) {
				TensorNetwork::Link &l = cpy.nodes[id].neighbors[i];
				if (!l.external) { // Link was not external before
					if (!_idF(l.other)) { // ...but is "external" to this subnet
						l.external = true;
						l.indexPosition = cpy.externalLinks.size();
						cpy.dimensions.emplace_back(l.dimension);
						cpy.externalLinks.emplace_back(id, i, l.dimension, false);
					} 
				}
			}
		}
		
		size_t correction = 0;
		std::vector<long> toErase;
		for (size_t eid = 0; eid < cpy.externalLinks.size(); ++eid) {
			TensorNetwork::Link &l = cpy.externalLinks[eid];
			if (!_idF(l.other)) {
				toErase.emplace_back(long(eid));
				correction++;
			} else {
				REQUIRE(cpy.nodes[l.other].neighbors[l.indexPosition].external, "ie");
				REQUIRE(cpy.nodes[l.other].neighbors[l.indexPosition].indexPosition == eid, "ie");
				cpy.nodes[l.other].neighbors[l.indexPosition].indexPosition -= correction;
			}
		}
		
		for (size_t i = toErase.size(); i > 0; --i) {
			cpy.dimensions.erase(cpy.dimensions.begin()+toErase[i-1]);
			cpy.externalLinks.erase(cpy.externalLinks.begin()+toErase[i-1]);
		}
		
		cpy.require_valid_network(false);
		return cpy;
	}
	
	TensorNetwork TensorNetwork::stripped_subnet(const std::set<size_t>& _ids) const {
		return stripped_subnet([&](size_t _id){ return misc::contains(_ids, _id); });
	}
	
	
	void TensorNetwork::swap_external_links(const size_t _i, const size_t _j) {
		TensorNetwork::Link &li = externalLinks[_i];
		TensorNetwork::Link &lj = externalLinks[_j];
		nodes[li.other].neighbors[li.indexPosition].indexPosition = _j;
		nodes[lj.other].neighbors[lj.indexPosition].indexPosition = _i;
		std::swap(externalLinks[_i], externalLinks[_j]);
		std::swap(dimensions[_i], dimensions[_j]);
	}
	
	
	void TensorNetwork::add_network_to_network(IndexedTensorWritable<TensorNetwork>&& _base, IndexedTensorReadOnly<TensorNetwork>&& _toInsert) {
		_toInsert.assign_indices();
		
		TensorNetwork &base = *_base.tensorObject;
		const TensorNetwork &toInsert = *_toInsert.tensorObjectReadOnly;
		
		const size_t firstNew = base.nodes.size();
		const size_t firstNewExternal = base.externalLinks.size();
		
		// Insert everything
		_base.indices.insert(_base.indices.end(), _toInsert.indices.begin(), _toInsert.indices.end());
		base.dimensions.insert(base.dimensions.end(), toInsert.dimensions.begin(), toInsert.dimensions.end());
		base.externalLinks.insert(base.externalLinks.end(), toInsert.externalLinks.begin(), toInsert.externalLinks.end());
		base.nodes.insert(base.nodes.end(), toInsert.nodes.begin(), toInsert.nodes.end());
		
		IF_CHECK (
			for (const Index &idx : _base.indices) {
				REQUIRE(misc::count(_base.indices, idx) < 3, "Index must not appear three (or more) times.");
			}
		)
		
		// Sanitize the externalLinks
		for (size_t i = firstNewExternal; i < base.externalLinks.size(); ++i) {
			base.externalLinks[i].other += firstNew;
		}
		
		// Sanitize the nodes (treating all external links as new external links)
		for (size_t i = 0; i < toInsert.nodes.size(); ++i) {
			for(TensorNetwork::Link &l : base.nodes[firstNew+i].neighbors) {
				if (!l.external) { // Link inside the added network
					l.other += firstNew;
				} else { // External link
					l.indexPosition += firstNewExternal;
				}
			}
		}
		
		// Find traces (former contractions have become traces due to the joining)
		link_traces(std::move(_base));
		
		_base.tensorObject->require_valid_network();
	}
	
	void TensorNetwork::link_traces(IndexedTensorWritable<TensorNetwork>&& _base) {
		TensorNetwork &base = *_base.tensorObject;
		_base.assign_indices();
		
		base.require_valid_network();
		
		IF_CHECK( std::set<Index> contractedIndices; )
		
		size_t passedDegree = 0;
		for(size_t i = 0; i < _base.indices.size(); ) {
			const Index& idx = _base.indices[i];
			
			// Search for a second occurance
			size_t j = i+1;
			size_t passedDegreeSecond = passedDegree + idx.span;
			for( ; j < _base.indices.size(); passedDegreeSecond += _base.indices[j].span, ++j) {
				if(idx == _base.indices[j]) { break; }
			}
			
			if(j < _base.indices.size()) { // There is a second occurance.
				REQUIRE(!misc::contains(contractedIndices, idx), "Indices must occur at most twice per contraction");
				REQUIRE(idx.span == _base.indices[j].span, "Index spans do not coincide " << idx << " vs " << _base.indices[j]);
				IF_CHECK( contractedIndices.insert(idx); )
				
				for (size_t n = 0; n < idx.span; ++n) {
					const TensorNetwork::Link &link1 = base.externalLinks[passedDegree+n];
					const TensorNetwork::Link &link2 = base.externalLinks[passedDegreeSecond+n];
					REQUIRE(link1.dimension == link2.dimension, "Index dimensions do not coincide: ["<<n<<"]" << link1.dimension << " vs " << link2.dimension << " Indices are " << idx << " and " << idx2 << " from " << _base.indices);
					
					base.nodes[link1.other].neighbors[link1.indexPosition] = link2;
					base.nodes[link2.other].neighbors[link2.indexPosition] = link1;
				}
				
				// Remove external links and dimensions from network
				base.externalLinks.erase(base.externalLinks.begin()+long(passedDegreeSecond), base.externalLinks.begin()+long(passedDegreeSecond+idx.span)); // note that passedDegreeSecond > passedDegree
				base.externalLinks.erase(base.externalLinks.begin()+long(passedDegree), base.externalLinks.begin()+long(passedDegree+idx.span));
				base.dimensions.erase(base.dimensions.begin()+long(passedDegreeSecond), base.dimensions.begin()+long(passedDegreeSecond+idx.span)); 
				base.dimensions.erase(base.dimensions.begin()+long(passedDegree), base.dimensions.begin()+long(passedDegree+idx.span));
				
				// Sanitize external links
				for(size_t k = passedDegree; k < passedDegreeSecond-idx.span; ++k) {
					base.nodes[base.externalLinks[k].other].neighbors[base.externalLinks[k].indexPosition].indexPosition -= idx.span;
				}
				
				for(size_t k = passedDegreeSecond-idx.span; k < base.externalLinks.size(); ++k) {
					base.nodes[base.externalLinks[k].other].neighbors[base.externalLinks[k].indexPosition].indexPosition -= 2*idx.span;
				}
				
				// Remove indices
				_base.indices.erase(_base.indices.begin()+long(j));
				_base.indices.erase(_base.indices.begin()+long(i));
			} else {
				passedDegree += idx.span;
				++i;
			}
		}
		
		base.contract_unconnected_subnetworks();
	}
	
	
	void TensorNetwork::identify_common_edge(size_t& _posA, size_t& _posB, Index& _ba, Index& _aa, Index& _bb, Index& _ab, const size_t _nodeA, const size_t _nodeB) const {
		// Find common Index in nodeA
		IF_CHECK(bool foundCommon = false;)
		for(size_t i = 0; i < nodes[_nodeA].neighbors.size(); ++i) {
			if(nodes[_nodeA].neighbors[i].other == _nodeB) {
				REQUIRE(!foundCommon, "TN round does not work if the two nodes share more than one link.");
				IF_CHECK(foundCommon = true;)
				_posA = i;
			}
		}
		REQUIRE(foundCommon, "TN round does not work if the two nodes share no link.");
		
		// Find common Index in nodeB
		IF_CHECK(foundCommon = false;)
		for(size_t i = 0; i < nodes[_nodeB].neighbors.size(); ++i) {
			if(nodes[_nodeB].neighbors[i].other == _nodeA) {
				REQUIRE(!foundCommon, "TN round does not work if the two nodes share more than one link.");
				IF_CHECK(foundCommon = true;)
				_posB = i;
			}
		}
		REQUIRE(foundCommon, "TN round does not work if the two nodes share no link.");
		
		// Set the spans of the indices
		_ba.span = _posA;
		_aa.span = nodes[_nodeA].degree() - _posA - 1;
		_bb.span = _posB;
		_ab.span = nodes[_nodeB].degree() - _posB - 1;
	}
	
	void TensorNetwork::round_edge(const size_t _nodeA, const size_t _nodeB, const size_t _maxRank, const double _eps, const double _softThreshold, const bool _preventZero) {
		size_t posA, posB;
		Index ba, aa, bb, ab, c1, c2, k, l;
		identify_common_edge(posA, posB, ba, aa, bb, ab, _nodeA, _nodeB);
		
		Tensor& tensorA = (*nodes[_nodeA].tensorObject);
		Tensor& tensorB = (*nodes[_nodeB].tensorObject);
		
		Tensor X; // TODO Sparse
		Tensor S;

		// TODO eventually use only one QC if only one is usefull.
		
		// Check whether prior QR makes sense
		if (tensorA.size > misc::sqr(tensorA.dimensions[posA]) 
		|| tensorB.size > misc::sqr(tensorB.dimensions[posB])) {
			// Calculate the cores
			Tensor coreA, coreB;
			(tensorA(ba, c1, aa), coreA(c1, k)) = QC(tensorA(ba, k, aa));
			(tensorB(bb, c2, ab), coreB(k, c2)) = QC(tensorB(bb, k, ab)); // TODO use CQ when available
			
			// Contract the cores
			X(c1, c2) = coreA(c1, k)*coreB(k, c2);
			
			// Calculate SVD
			(coreA(c1, k), S(k,l), coreB(l, c2)) = SVD(X(c1, c2), _maxRank, _eps, _softThreshold, _preventZero);
			
			// Contract diagnonal matrix to coreB
			coreB(l, c2) = S(l, k) * coreB(k, c2);
			
			// Contract the "cores" back to their sides
			tensorA(ba, k, aa) = tensorA(ba, c1, aa) * coreA(c1, k);
			tensorB(bb, k, ab) = coreB(k, c2) * tensorB(bb, c2, ab);
			
		} else {
			// Contract the two
			X(ba, aa, bb, ab) = (*nodes[_nodeA].tensorObject)(ba, c1, aa) * (*nodes[_nodeB].tensorObject)(bb, c1, ab);
			
			// Calculate SVD
			(((*nodes[_nodeA].tensorObject)(ba, c1, aa)), S(c1, c2), ((*nodes[_nodeB].tensorObject)(bb, c2, ab))) = SVD(X(ba, aa, bb, ab), _maxRank, _eps, _softThreshold, _preventZero);
			
			// Contract diagnonal matrix to NodeB
			(*nodes[_nodeB].tensorObject)(bb, c1, ab) = S(c1, c2) * ((*nodes[_nodeB].tensorObject)(bb, c2, ab));
		}
		
		// Set the new dimension in the nodes
		nodes[_nodeA].neighbors[posA].dimension = S.dimensions[0];
		nodes[_nodeB].neighbors[posB].dimension = S.dimensions[0];
	}
	
	
	void TensorNetwork::transfer_core(const size_t _nodeA, const size_t _nodeB, const bool _allowRankReduction) {
		size_t posA, posB;
		Index ba, aa, bb, ab, c1, c2;
		identify_common_edge(posA, posB, ba, aa, bb, ab, _nodeA, _nodeB);
		
		// Calculate QR
		Tensor X;
		if(_allowRankReduction) {
			((*nodes[_nodeA].tensorObject)(ba, c2, aa), X(c2, c1)) = QC((*nodes[_nodeA].tensorObject)(ba, c1, aa));
		} else {
			((*nodes[_nodeA].tensorObject)(ba, c2, aa), X(c2, c1)) = QR((*nodes[_nodeA].tensorObject)(ba, c1, aa));
		}
		
		// Contract diagnonal matrix to NodeB
		(*nodes[_nodeB].tensorObject)(bb, c1, ab) = X(c1, c2) * ((*nodes[_nodeB].tensorObject)(bb, c2, ab));
		
		// Set the new dimension in the nodes
		nodes[_nodeA].neighbors[posA].dimension = X.dimensions[0];
		nodes[_nodeB].neighbors[posB].dimension = X.dimensions[0];
	}
	
	
	void TensorNetwork::fix_slate(const size_t _dimension, const size_t _slatePosition) {
		require_valid_network();
		const size_t extNode = externalLinks[_dimension].other;
		const size_t extNodeIndexPos = externalLinks[_dimension].indexPosition;
		
		for(size_t i = _dimension+1; i < dimensions.size(); ++i) {
			nodes[externalLinks[i].other].neighbors[externalLinks[i].indexPosition].indexPosition--;
		}
		
		externalLinks.erase(externalLinks.begin()+(long)_dimension);
		dimensions.erase(dimensions.begin()+(long)_dimension);
		
		for(size_t i = extNodeIndexPos+1; i < nodes[extNode].neighbors.size(); ++i) {
			const Link& link = nodes[extNode].neighbors[i];
			if(link.external) {
				externalLinks[link.indexPosition].indexPosition--; 
			} else {
				nodes[link.other].neighbors[link.indexPosition].indexPosition--;
			}
		}
		
		nodes[extNode].tensorObject->fix_slate(extNodeIndexPos, _slatePosition);
		nodes[extNode].neighbors.erase(nodes[extNode].neighbors.begin() + (long)extNodeIndexPos);
		
		contract_unconnected_subnetworks();
		require_valid_network();
	}
	
	
	void TensorNetwork::contract_unconnected_subnetworks() {
		require_valid_network();

		std::vector<bool> seen(nodes.size(), false);
		std::vector<size_t> expansionStack;
		expansionStack.reserve(nodes.size());
		
		// Starting at every external link...
		for (TensorNetwork::Link &el : externalLinks) {
			if(!seen[el.other]) {
				seen[el.other] = true;
				expansionStack.push_back(el.other);
			}
		}
		
		
		// ...traverse the connected nodes in a depth-first manner.
		while (!expansionStack.empty()) {
			const size_t curr = expansionStack.back();
			expansionStack.pop_back();
			
			// Add unseen neighbors
			for (const TensorNetwork::Link &n : nodes[curr].neighbors) {
				if ( !n.external && !seen[n.other] ) {
					seen[n.other] = true;
					expansionStack.push_back(n.other);
				}
			}
		}
		
		// Construct set of all unseen nodes...
		std::set<size_t> toContract;
		for (size_t i=0; i < nodes.size(); ++i) {
			if (!seen[i]) {
				toContract.insert(i);
			}
		}
		
		const bool keepFinalNode = degree() == 0;
		
		// ...and contract them
		if (!toContract.empty()) {
			size_t remaining = contract(toContract);
			
			// remove contracted degree-0 tensor
			REQUIRE(nodes[remaining].degree() == 0, "Internal Error.");
			REQUIRE(nodes[remaining].neighbors.empty(), "Internal Error.");
			if(!keepFinalNode) {
				for(size_t i =0; i < nodes.size(); ++i) {
					if(i != remaining && !nodes[i].erased) {
						*nodes[i].tensorObject *= (*nodes[remaining].tensorObject.get())[0];
						break;
					}
					REQUIRE(i < nodes.size()-1, "Internal Error.");
				}
				nodes[remaining].erased = true;
			}
		}
		
		// Remove all erased nodes
		std::vector<size_t> idMap(nodes.size(), ~0ul);
		
		// Move nodes in vector
		size_t newId=0, oldId=0;
		for (; oldId < nodes.size(); ++oldId) {
			if (nodes[oldId].erased) { continue; }
			idMap[oldId] = newId;
			if (newId != oldId) { std::swap(nodes[newId], nodes[oldId]); }
			newId++;
		}
		
		// Update links
		nodes.resize(newId);
		for (TensorNode &n : nodes) {
			for (TensorNetwork::Link &l : n.neighbors) {
				if (!l.external) l.other = idMap[l.other];
			}
		}
		
		// Update external links
		for (TensorNetwork::Link &l : externalLinks) {
			l.other = idMap[l.other];
		}
		
		REQUIRE(nodes.size() > 0, "Internal error");
		REQUIRE(!keepFinalNode || nodes.size() == 1, "internal error!");
	}

	
	void TensorNetwork::reduce_representation() {
		require_valid_network();
		
		TensorNetwork strippedNet = stripped_subnet();
		std::vector<std::set<size_t>> contractions(strippedNet.nodes.size());
		for (size_t id1=0; id1 < strippedNet.nodes.size(); ++id1) {
			TensorNode &currNode = strippedNet.nodes[id1];
			if (currNode.erased) {
				continue;
			}
			for (Link &l : currNode.neighbors) {
				if (l.external) continue;
				size_t r=1;
				for (Link &l2 : currNode.neighbors) {
					if (l2.other == l.other) {
						r *= l2.dimension;
					}
				}
				if (r*r >= currNode.size() || r*r >= strippedNet.nodes[l.other].size()) {
					if (contractions[id1].empty()) {
						contractions[id1].insert(id1);
					}
					if (contractions[l.other].empty()) {
						contractions[id1].insert(l.other);
					} else {
						contractions[id1].insert(contractions[l.other].begin(), contractions[l.other].end());
						contractions[l.other].clear();
					}
					strippedNet.contract(id1, l.other);
					id1 -= 1; // check the same node again in the next iteration
					break; // for-each iterator is broken, so we have to break
				}
			}
		}
		
		// perform the collected contractions from above
		for (std::set<size_t> &ids : contractions) {
			if (ids.size() > 1) {
				contract(ids);
			}
		}
		
		sanitize();
		require_valid_network();
	}
	
	
	void TensorNetwork::sanitize() {
		size_t idCount = 0;
		std::map<size_t, size_t> idMap;
		for (size_t i = 0; i < nodes.size(); ++i) {
			if (!nodes[i].erased) {
				idMap[i] = idCount++;
			}
		}
		reshuffle_nodes(idMap);
	}
	
	
	//TODO do this without indices
	void TensorNetwork::trace_out_self_links(const size_t _nodeId) {
		std::vector<Index> idxIn, idxOut;
		std::vector<TensorNetwork::Link> newLinks;
		size_t correction = 0;
		for (size_t i = 0; i < nodes[_nodeId].neighbors.size(); ++i) {
			const TensorNetwork::Link &l = nodes[_nodeId].neighbors[i];
			if (!l.links(_nodeId)) {
				idxIn.emplace_back();
				idxOut.emplace_back(idxIn.back());
				newLinks.emplace_back(l);
				if (l.external) {
					externalLinks[l.indexPosition].indexPosition -= correction;
				} else {
					nodes[l.other].neighbors[l.indexPosition].indexPosition -= correction;
				}
			} else if (l.indexPosition > i) {
				idxIn.emplace_back();
				correction += 1;
			} else {
				idxIn.emplace_back(idxIn[l.indexPosition]);
				correction += 1;
			}
		}
		
		// Perform the trace
		if (nodes[_nodeId].tensorObject) {
			std::unique_ptr<Tensor> newTensor(new Tensor(nodes[_nodeId].tensorObject->representation));
			(*newTensor)(idxOut) = (*nodes[_nodeId].tensorObject)(idxIn);
			nodes[_nodeId].tensorObject = std::move(newTensor);
		}
		
		nodes[_nodeId].neighbors = std::move(newLinks);
	}

	//TODO testcase A(i,j)*B(k,k) of TTtensors
	/**
	* contracts the nodes with indices @a _node1 and @a _node2
	* replaces node1 with the contraction and node2 with an degree-0 tensor
	*/
	void TensorNetwork::contract(const size_t _nodeId1, const size_t _nodeId2) {
		TensorNode &node1 = nodes[_nodeId1];
		TensorNode &node2 = nodes[_nodeId2];
		
		REQUIRE(!node1.erased, "It appears node1 = " << _nodeId1 << "  was already contracted?");
		REQUIRE(!node2.erased, "It appears node2 = " << _nodeId2 << "  was already contracted?");
		REQUIRE(externalLinks.size() == degree(), "Internal Error: " << externalLinks.size() << " != " << degree());
		
		std::vector<TensorNetwork::Link> newLinks;
		newLinks.reserve(node1.degree() + node2.degree());
		
		if (!node1.tensorObject) {
			REQUIRE(!node2.tensorObject, "Internal Error.");
			
			// Determine the links of the resulting tensor (first half)
			for ( const Link& l : node1.neighbors ) {
				if (!l.links(_nodeId1) && !l.links(_nodeId2)) {
					newLinks.emplace_back(l);
				}
			}
			// Determine the links of the resulting tensor (second half)
			for ( const Link& l : node2.neighbors ) {
				if (!l.links(_nodeId2) && !l.links(_nodeId1)) {
					newLinks.emplace_back(l);
				}
			}
		} else {
			REQUIRE(node2.tensorObject, "Internal Error.");
			
			size_t contractedDimCount = 0;
			bool separated1;
			bool separated2;
			bool matchingOrder;
			
			// first pass of the links of node1 to determine
			//   1. the number of links between the two nodes,
			//   2. determine whether node1 is separated (ownlinks-commonlinks) or transposed separated (commonlinks-ownlinks)
			//   3. determine the links of the resulting tensor (first half)
			if(node1.degree() > 1) {
				uint_fast8_t switches = 0;
				bool previous = node1.neighbors[0].links(_nodeId2);
				for (const Link& l : node1.neighbors) {
					if (l.links(_nodeId2)) {
						contractedDimCount++;
						if (!previous) {
							switches++;
							previous = true;
						}
					} else {
						newLinks.emplace_back(l);
						if (previous) {
							switches++;
							previous = false;
						}
					}
				}
				separated1 = (switches < 2);
			} else {
				if(!node1.neighbors.empty()) {
					if(node1.neighbors[0].links(_nodeId2)) {
						contractedDimCount = 1;
					} else {
						newLinks.emplace_back(node1.neighbors[0]);
					}
				}
				separated1 = true;
			}
			
			// first pass of the links of node2 to determine
			//   1. whether the order of common links is correct
			//   2. whether any self-links exist
			//   3. whether the second node is separated
			//   4. determine the links of the resulting tensor (second half)
			if(node2.degree() > 1 && contractedDimCount > 0) {
				bool previous = node2.neighbors[0].links(_nodeId1);
				uint_fast8_t switches = 0;
				size_t lastPosOfCommon = 0;
				matchingOrder = true;
				for (const Link& l : node2.neighbors) {
					if (l.links(_nodeId1)) {
						if (l.indexPosition < lastPosOfCommon) {
							matchingOrder = false;
						}
						lastPosOfCommon = l.indexPosition;
						if (!previous) {
							switches++;
							previous = true;
						}
					} else {
						newLinks.emplace_back(l);
						if (previous) {
							switches++;
							previous = false;
						}
					}
				}
				separated2 = (switches < 2);
			} else {
				if(contractedDimCount == 0) {
					newLinks.insert(newLinks.end(), node2.neighbors.begin(), node2.neighbors.end());
				}
				
				separated2 = true;
				matchingOrder = true;
			}
			
			// Determine which (if any) node should be reshuffled
			// if order of common links does not match, reshuffle the smaller one
			if (!matchingOrder && separated1 && separated2) {
				if (node1.size() < node2.size()) {
					separated1 = false;
				} else {
					separated2 = false;
				}
			}
			
			// reshuffle first node
			if (!separated1) {
				std::vector<size_t> shuffle(node1.degree());
				size_t pos = 0;
				
				for (size_t d = 0; d < node1.degree(); ++d) {
					if (!node1.neighbors[d].links(_nodeId2)) {
						shuffle[d] = pos++;
					}
				}
				
				for (const Link& l : node2.neighbors) {
					if (l.links(_nodeId1)) {
						shuffle[l.indexPosition] = pos++;
					}
				}
				
				REQUIRE(pos == node1.degree(), "IE");
				reshuffle(*node1.tensorObject, *node1.tensorObject, shuffle);
				
				matchingOrder = true;
			}
			
			// reshuffle second node
			if (!separated2) {
				std::vector<size_t> shuffle(node2.degree());
				size_t pos = 0;
				
				if (matchingOrder) {
					// Add common links in order as they appear in node2 to avoid both nodes changing to the opposite link order
					for (size_t d = 0; d < node2.degree(); ++d) {
						if (node2.neighbors[d].links(_nodeId1)) {
							shuffle[d] = pos++;
						}
					}
				} else {
					for (const Link& l : node1.neighbors) {
						if (l.links(_nodeId2)) {
							shuffle[l.indexPosition] = pos++;
						}
					}
				}
				
				for (size_t d = 0; d < node2.degree(); ++d) {
					if (!node2.neighbors[d].links(_nodeId1)) {
						shuffle[d] = pos++;
					}
				}
				
				REQUIRE(pos == node2.degree(), "IE");
				reshuffle(*node2.tensorObject, *node2.tensorObject, shuffle);
			}
			
			const bool trans1 = separated1 && !node1.neighbors.empty() && node1.neighbors[0].links(_nodeId2);
			const bool trans2 = separated2 &&!node2.neighbors.empty() &&!(node2.neighbors[0].links(_nodeId1));
			
			xerus::contract(*node1.tensorObject, *node1.tensorObject, trans1, *node2.tensorObject, trans2, contractedDimCount);
		}
		
		// Set Nodes
		nodes[_nodeId1].neighbors = std::move(newLinks);
		nodes[_nodeId2].erase();
		
		// Fix indices of other nodes // note that the indices that were previously part of node1 might also have changed
		for (size_t d = 0; d < nodes[_nodeId1].neighbors.size(); ++d) {
			const Link& l = nodes[_nodeId1].neighbors[d];
			if (l.external) {
				externalLinks[l.indexPosition].other = _nodeId1;
				externalLinks[l.indexPosition].indexPosition = d;
			} else {
				nodes[l.other].neighbors[l.indexPosition].other = _nodeId1;
				nodes[l.other].neighbors[l.indexPosition].indexPosition = d;
			}
		}
		
		require_valid_network(false);
	}

	double TensorNetwork::contraction_cost(const size_t _nodeId1, const size_t _nodeId2) const {  
		REQUIRE(!nodes[_nodeId1].erased, "It appears node1 = " << _nodeId1 << " was already contracted?");
		REQUIRE(!nodes[_nodeId2].erased, "It appears node2 = " << _nodeId2 << " was already contracted?");
		
		if (_nodeId1 == _nodeId2) {
			return (double) nodes[_nodeId1].size(); // Costs of a trace
		}
		
		// Assume cost of mxr * rxn = m*n*r (which is a rough approximation of the actual cost for openBlas/Atlas)
		//TODO add correct calculation for sparse matrices
		size_t cost = nodes[_nodeId1].size();
		for(const Link& neighbor : nodes[_nodeId2].neighbors) {
			if(!neighbor.links(_nodeId1)) {
				cost *= neighbor.dimension;
			}
		}
		return (double) (cost);
	}


	size_t TensorNetwork::contract(const std::set<size_t>& _ids) {
		// Trace out all single-node traces
		for ( const size_t id : _ids ) {
			for ( const TensorNetwork::Link &l : nodes[id].neighbors ) {
				if (l.links(id)) {
					trace_out_self_links(id);
					break;
				}
			}
		}
		
		if (_ids.size() == 0) { return ~0ul; }
		
		if (_ids.size() == 1) { return *_ids.begin(); }

		if (_ids.size() == 2) {
			auto secItr = _ids.begin(); ++secItr;
			contract(*_ids.begin(), *secItr);
			return *_ids.begin();
		}
		
		if (_ids.size() == 3) {
			auto idItr = _ids.begin();
			const size_t a = *idItr; TensorNode &na = nodes[a]; ++idItr;
			const size_t b = *idItr; TensorNode &nb = nodes[b]; ++idItr;
			const size_t c = *idItr; TensorNode &nc = nodes[c];
			double sa=1, sb=1, sc=1; // sizes devided by the link dimensions between a,b,c
			double sab=1, sbc=1, sac=1; // link dimensions
			for (size_t d = 0; d < na.degree(); ++d) {
				if (na.neighbors[d].links(b)) {
					sab *= (double) na.neighbors[d].dimension;
				} else if (na.neighbors[d].links(c)) {
					sac *= (double) na.neighbors[d].dimension;
				} else {
					sa *= (double) na.neighbors[d].dimension;
				}
			}
			for (size_t d = 0; d < nb.degree(); ++d) {
				if (nb.neighbors[d].links(c)) {
					sbc *= (double) nb.neighbors[d].dimension;
				} else if (!nb.neighbors[d].links(a)) {
					sb *= (double) nb.neighbors[d].dimension;
				}
			}
			for (size_t d = 0; d < nc.degree(); ++d) {
				if (!nc.neighbors[d].links(a) && !nc.neighbors[d].links(b)) {
					sc *= (double)nc.neighbors[d].dimension;
				}
			}
			// cost of contraction a-b first etc.
			double costAB = sa*sb*sac*sbc*(sab+sc); // (sa*sac)*sab*(sb*sbc) + sa*sb*sac*sbc*sc;
			double costAC = sa*sc*sab*sbc*(sac+sb); 
			double costBC = sb*sc*sab*sac*(sbc+sa);
			
			if (costAB < costAC && costAB < costBC) {
				LOG(TNContract, "contraction of ab first " << sa << " " << sb << " " << sc << " " << sab << " " << sbc << " " << sac);
				contract(a, b); contract(a, c); 
			} else if (costAC < costBC) {
				LOG(TNContract, "contraction of ac first " << sa << " " << sb << " " << sc << " " << sab << " " << sbc << " " << sac);
				contract(a, c); contract(a, b);
			} else {
				LOG(TNContract, "contraction of bc first " << sa << " " << sb << " " << sc << " " << sab << " " << sbc << " " << sac);
				contract(b, c); contract(a, b);
			}
			return a;
		}
		
		
		TensorNetwork strippedNetwork = stripped_subnet(_ids); 
		double bestCost=std::numeric_limits<double>::max();
		std::vector<std::pair<size_t, size_t>> bestOrder;
		
		// Ask the heuristics
		for (const internal::ContractionHeuristic &c : internal::contractionHeuristics) {
			c(bestCost, bestOrder, strippedNetwork);
		}
		
		REQUIRE(bestCost < std::numeric_limits<double>::max() && !bestOrder.empty(), "Internal Error.");
		
		for (const std::pair<size_t,size_t> &c : bestOrder) {
			contract(c.first, c.second);
		}
		
		// Note: no sanitization as eg. TTStacks require the indices not to change after calling this function
		return bestOrder.back().first;
	}

	value_t TensorNetwork::frob_norm() const {
		Index i;
		Tensor res;
		res() = (*this)(i&0) * (*this)(i&0);
		return std::sqrt(res[0]);
	}
	
	
	void TensorNetwork::draw(const std::string& _filename) const {
		std::stringstream graphLayout;
				
		graphLayout << "graph G {" << std::endl;
		graphLayout << "graph [mclimit=1000, maxiter=1000, overlap = false, splines = true]" << std::endl;
		
		for(size_t i = 0; i < nodes.size(); ++i) {
			// Create the Nodes
			if(nodes[i].erased) {
				graphLayout << "\tN"<<i<<" [label=\"N"<<i<<"\", shape=circle, fixedsize=shape, height=0.45];" << std::endl;
			} else {
				graphLayout << "\tN"<<i<<" [label=\"";
				for(size_t k=0; k+1 < nodes[i].degree(); ++k) {
					if(nodes[i].degree()/2 == k) {
						if(nodes[i].degree()%2 == 0) {
						graphLayout << "<i"<<k<<"> "<<i<<"| ";
						} else {
							graphLayout << "<i"<<k<<"> N"<<i<<"| ";
						}
					} else if(nodes[i].degree()%2 == 0 && nodes[i].degree()/2 == k+1) {
						graphLayout << "<i"<<k<<"> N| "; 
					} else {
						graphLayout << "<i"<<k<<"> | ";
					}
				}
				if(nodes[i].degree() <= 2) {
					graphLayout << "<i"<<nodes[i].degree()-1<<"> N"<<i<<"\", shape=record, fixedsize=shape, height=0.45, style=\"rounded,filled\"];" << std::endl;
				} else {
					graphLayout << "<i"<<nodes[i].degree()-1<<">\", shape=record, fixedsize=shape, height=0.45, style=\"rounded,filled\"];" << std::endl;
				}
				
				// Add all links to nodes with smaller index and externals
				for(size_t j = 0; j < nodes[i].neighbors.size(); ++j) {
					if(nodes[i].neighbors[j].external) {
						graphLayout << "\t"<<nodes[i].neighbors[j].indexPosition<<" [shape=diamond, fixedsize=shape, height=0.38, width=0.38, style=filled];" << std::endl;
						graphLayout << "\tN"<<i<<":i"<<j<<" -- " << nodes[i].neighbors[j].indexPosition << " [len=1, label=\""<<nodes[i].neighbors[j].dimension<<"\"];" << std::endl;
					} else if(nodes[i].neighbors[j].other < i) {
						graphLayout << "\tN"<<i<<":i"<<j<<" -- " << "N"<<nodes[i].neighbors[j].other << ":i"<< nodes[i].neighbors[j].indexPosition<<" [label=\""<<nodes[i].neighbors[j].dimension<<"\"];" << std::endl;
					}
				}
			}
		}
		graphLayout << "}" << std::endl;
		misc::exec(std::string("dot -Tsvg > ") + _filename+".svg", graphLayout.str());
	}
}
