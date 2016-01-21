// Xerus - A General Purpose Tensor Library
// Copyright (C) 2014-2016 Benjamin Huber and Sebastian Wolf. 
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
* @brief Header file for a large number of helper functions that should either be part of the standard library or are too small to warrant a new compilation unit.
*/

#pragma once

#include <vector>
#include <set>
#include <map>
#include <tuple>
#include <algorithm>
#include <iostream>
#include <memory>
#include <limits>

#include "standard.h"
#include "sfinae.h"
#include "namedLogger.h"
#include "check.h"

/**
* @def VLA(T, name)
* @brief Define a variable length array of type @a T and name @a name that can be used just as gnu++ VLAs but is created on the heap.
*/
#define VLA(T, name) auto name##_store = xerus::misc::make_unique_array(new T); const auto & name = name##_store.get();

namespace xerus {
	namespace misc {

		template<class T>
		std::unique_ptr<T> make_unique(T* _ptr) {
			return std::unique_ptr<T>(_ptr);
		}

		template<class T>
		std::unique_ptr<T[]> make_unique_array(T* _ptr) {
			return std::unique_ptr<T[]>(_ptr);
		}
		
		
		/**
		* @brief Execute a given command.
		* @param _cmd the command to execute
		* @return the cout return of the command.
		*/
		std::string exec(const std::string &_cmd);
		
		/**
		* @brief Execute a given command and pipe _stdin to its std input,
		* @param _cmd the command to execute
		* @param _stdin the input for the program
		*/
		void exec(const std::string & _cmd, const std::string &_stdin);

		/**
		* @brief Wrapper class to disallow implicit cast (e.g. from everything to bool).
		*/
		template<class T>
		struct NoCast{ 
			const T value;
			
			NoCast(const T _value) : value(_value) {}
			
			operator T() const { return value; }
		};
		
		template<class...> using void_t = void;
		
		
		#if __GNUC__ > 4 || defined(__clang__)
		
		GENERATE_HAS_FUNCTION(count)
		GENERATE_HAS_FUNCTION(find)
		
		
		///@brief: Counts how often an element is contained in an arbitary container
		template<template<class, class...> class container_t, class item_t, class... rest_t,
			typename std::enable_if<has_count<container_t<item_t, rest_t...>, item_t>::value, int>::type = 0>
		size_t count(const container_t<item_t, rest_t...> &_container, const item_t &_item) {
			return _container.count(_item);
		}
		
		///@brief: Counts how often an element is contained in an arbitary container
		template<template<class, class...> class container_t, class item_t, class... rest_t, 
		typename std::enable_if<!has_count<container_t<item_t, rest_t...>, item_t>::value, int>::type = 0>
		size_t count(const container_t<item_t, rest_t...> &_container, const item_t &_item) {
			size_t count = 0;
			for(const item_t& otherItem : _container) {
				if(otherItem == _item) { count++; }
			}
			return count;
		}
		
		
		///@brief: Checks whether an arbitary container contains a certain element.
		template<template<class, class...> class container_t, class item_t, class... rest_t,
			typename std::enable_if<has_find<container_t<item_t, rest_t...>, item_t>::value, int>::type = 0>
		bool contains(const container_t<item_t, rest_t...> &_container, const item_t &_item) {
			return _container.find(_item) != _container.end();
		}

		///@brief: Checks whether an arbitary container contains a certain element.
		template<template<class, class...> class container_t, class item_t, class... rest_t,
			typename std::enable_if<!has_find<container_t<item_t, rest_t...>, item_t>::value, int>::type = 0>
		bool contains(const container_t<item_t, rest_t...> &_container, const item_t &_item) {
			return std::find(_container.begin(), _container.end(), _item) != _container.end();
		}

		#else 
		
		GENERATE_HAS_MEMBER(count)
		
		///@brief: Counts how often an element is contained in an arbitary container
		template<template<class, class...> class container_t, class item_t, class... rest_t, typename std::enable_if<!has_member_count<container_t<item_t, rest_t...>>::value, int>::type = 0>
		size_t count(const container_t<item_t, rest_t...> &_container, const item_t &_item) {
			size_t count = 0;
			for(const item_t& otherItem : _container) {
				if(otherItem == _item) { count++; }
			}
			return count;
		}
		
		///@brief: Counts how often an element is contained in an arbitary container
		template<template<class, class...> class container_t, class item_t, class... rest_t, typename std::enable_if<has_member_count<container_t<item_t, rest_t...>>::value, int>::type = 0>
		size_t count(const container_t<item_t, rest_t...> &_container, const item_t &_item) {
			return _container.count(_item);
		}
		
		///@brief: Checks whether an arbitary container contains a certain element.
		template<template<class, class...> class container_t, class item_t, class... rest_t, typename std::enable_if<!has_member_count<container_t<item_t, rest_t...>>::value, int>::type = 0>
		bool contains(const container_t<item_t, rest_t...> &_container, const item_t &_item) {
			return std::find(_container.begin(), _container.end(), _item) != _container.end();
		}
		
		///@brief: Checks whether an arbitary container contains a certain element.
		template<template<class, class...> class container_t, class item_t, class... rest_t, typename std::enable_if<has_member_count<container_t<item_t, rest_t...>>::value, int>::type = 0>
		bool contains(const container_t<item_t, rest_t...> &_container, const item_t &_item) {
			return _container.find(_item) != _container.end();
		}
		
		#endif
		
		///@brief:  Check whether an arbitary container contains all elemets of another arbitary container.
		template<template<class, class...> class containerA_t, template<class, class...> class containerB_t, class item_t, class... restA_t, class... restB_t>
		bool contains(const containerA_t<item_t, restA_t...> &_largeContainer, const containerB_t<item_t, restB_t...> &_smallContainer) {
			for(const item_t &item : _smallContainer) {
				if(!contains(_largeContainer, item)) { return false; }
			}
			return true;
		}
		
		

		
		///@brief: Checks whether two arbitary containers are disjunct, i.e. share no object.
		template<template<class, class...> class container_t, class item_t, class... rest_t>
		bool disjunct(const container_t<item_t, rest_t...>& _containerA, const container_t<item_t, rest_t...>& _containerB) {
			for(const item_t& item : _containerA) {
				if(contains(_containerB, item)) { return false; }
			}
			return true;
		}

		///@brief: Checks whether all object in two iterator ranges coincide.
		template< class InputIt1, class InputIt2 >
		bool equal( InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2 ) 
		{
			while (first1 != last1) {
				if (first2 == last2 || *first1 != *first2) return false;
				++first1; ++ first2;
			}
			return first2 == last2;
		}

		///@brief: Selects the maximal element from an arbitary container.
		template<template<class, class...> class container_t, class item_t, class... rest_t>
		item_t max(const container_t<item_t, rest_t...>& _container) {
			REQUIRE(!_container.empty(), "max must not be invoked with empty container");
			item_t result = *_container.begin();
			for(const item_t &item : _container) {
				if(item > result) { result = item; }
			}
			return result;
		}

		///@brief: Selects the minimal element from an arbitary container.
		template<template<class, class...> class container_t, class item_t, class... rest_t>
		item_t min(const container_t<item_t, rest_t...>& _container) {
			REQUIRE(!_container.empty(), "min must not be invoked with empty container");
			item_t result = *_container.begin();
			for(const item_t &item : _container) {
				if(item < result) { result = item; }
			}
			return result;
		}


		///@brief: Calculates the sum of all entries of an arbitary container.
		template<template<class, class...> class container_t, class item_t, class... rest_t>
		_pure_ item_t sum(const container_t<item_t, rest_t...>& _container) {
			item_t sum = item_t(0);
			for(const item_t& item : _container){
				sum += item;
			}
			return sum;
		}

		///@brief: Calculates the floating point product of all entries of an arbitary container.
		template<template<class, class...> class container_t, class item_t, class... rest_t>
		_pure_ item_t product(const container_t<item_t, rest_t...>& _container) {
			item_t product = item_t(1);
			for(const item_t& item : _container){ product *= item; }
			return product;
		}
		
		///@brief: Calculates the product of all entries of an arbitary container.
		template<template<class, class...> class container_t, class item_t, class... rest_t>
		_pure_ double fp_product(const container_t<item_t, rest_t...>& _container) {
			double product = 1.0;
			for(const item_t& item : _container){ product *= double(item); }
			return product;
		}
		
		///@brief: Calculates the product the entries in the range [_first, _last).
		///@note: as this function takes indices it is only useful for vectors
		template<class item_t, class... rest_t>
		_pure_ item_t product(const std::vector<item_t, rest_t...>& _container, const size_t _first, const size_t _last) {
			REQUIRE(_first <= _last && _last <= _container.size(), "Invalid range " << _first << "-" << _last << " given (Container size " << _container.size() << ")"); 
			item_t product = item_t(1);
			for (size_t i=_first; i<_last; ++i) {
				product *= _container[i];
			}
			return product;
		}
		
		///@brief: Erases all elements specified by @a _rule from the container @a _container.
		template<class rule_t, template<class, class...> class container_t, class item_t, class... rest_t>
		void erase(container_t<item_t, rest_t...>& _container, const rule_t& _rule) {
			_container.erase(std::remove_if(_container.begin(), _container.end(), _rule), _container.end());
		}

		///@brief: Calculates the signum (-1, 0, 1) of a given value.
		template<class T> 
		int sgn(const T _value) {
			return (T(0) < _value) - (_value < T(0));
		}
		
		///@brief: Calculates _a*_a
		template<class T>
		T sqr(const T &_a) {
			return _a*_a;
		}

		///@brief: Calculates _base^_exp by binary exponentiation
		template<class T> 
		constexpr T pow(const T &_base, const uint64 _exp) {
			return _exp==0?1:(_exp%2==0?pow(_base*_base, _exp/2):_base*pow(_base, _exp-1));
		}
		
		///@brief: Calculates _base^_exp by binary exponentiation
		template<class T> 
		constexpr T pow(const T &_base, const int64 _exp) {
			return _exp==0 ? 
						1 :
						(
							_exp<0 ? (
								1/pow(_base, -_exp)
							) : (
								// _exp > 0
								_exp%2==0 ? pow(_base*_base, _exp/2) : _base*pow(_base, _exp-1)
							)
						);
		}
		
		///@brief: Calculates _base^_exp by binary exponentiation
		template<class T> 
		constexpr T pow(const T &_base, const int _exp) {
			return _exp==0 ? 
						1 :
						(
							_exp<0 ? (
								1/pow(_base, -_exp)
							) : (
								// _exp > 0
								_exp%2==0 ? pow(_base*_base, _exp/2) : _base*pow(_base, _exp-1)
							)
						);
		}

		///@brief: Checks whether the relative difference between @a _a and @a _b (i.e. |a-b|/(|a|/2+|b|/2)) is smaller than @a _eps.
		template<class T, typename std::enable_if<std::is_floating_point<T>::value, bool>::type = true>
		bool approx_equal(T _a, T _b, T _eps = 4*std::numeric_limits<T>::epsilon()) {
			return std::abs(_a-_b) <= _eps*0.5*(std::abs(_a)+std::abs(_b));
		}
		
		///@brief: Checks whether @a _a and @a _b are equal (for non floating point types).
		template<class T, typename std::enable_if<!std::is_floating_point<T>::value, bool>::type = true>
		bool approx_equal(T _a, T _b) {
			return _a == _b;
		}
		
		///@brief: Checks for hard equality ( == operator) without compiler warnings.
		template<class T>
		bool hard_equal(T _a, T _b) {
			#pragma GCC diagnostic push
				#pragma GCC diagnostic ignored "-Wfloat-equal"
				return _a == _b;
			#pragma GCC diagnostic pop
		}
		
		///@brief: Checks for hard equality ( == operator) without compiler warnings.
		template<class T>
		bool hard_not_equal(T _a, T _b) {
			#pragma GCC diagnostic push
				#pragma GCC diagnostic ignored "-Wfloat-equal"
				return _a != _b;
			#pragma GCC diagnostic pop
		}
		
		template<class T, class Comparator>
		std::vector<size_t> create_sort_permutation(const std::vector<T>& _vec, Comparator _comp) {
			std::vector<size_t> permutation(_vec.size());
			std::iota(permutation.begin(), permutation.end(), 0);
			std::sort(permutation.begin(), permutation.end(), [&](const size_t _i, const size_t _j){ return _comp(_vec[_i], _vec[_j]); });
			return permutation;
		}
		
		template<class T>
		void apply_permutation( std::vector<T>& _vec, const std::vector<size_t>& _permutation) {
			REQUIRE(_vec.size() == _permutation.size(), "Vector and permutation size must coincide.");
			std::vector<T> sorted_vec;
			sorted_vec.reserve(_permutation.size());
			for(size_t i = 0; i < _permutation.size(); ++i) {
				sorted_vec.emplace_back(std::move(_vec[_permutation[i]]));
			}
			_vec = std::move(sorted_vec);
		}
		
		template <class KeyType, class DataType, class Comparator>
		void simultaneous_sort( std::vector<KeyType>& _keyVector, std::vector<DataType>& _dataVector, Comparator _comp) {
			REQUIRE(_keyVector.size() == _dataVector.size(), "Vector sizes must coincide.");
			std::vector<size_t> permutation = create_sort_permutation(_keyVector, _comp);
			apply_permutation(_keyVector, permutation);
			apply_permutation(_dataVector, permutation);
		}
		
	}
}

namespace std {
	
	///@brief Add a + operator for iterators and size_t to avoid signed/unsigned errors
	template<class IteratorType, typename std::enable_if<std::is_same<typename std::iterator_traits<IteratorType>::difference_type, long>::value, bool>::type = true>
	IteratorType operator+(const IteratorType& _iterator, const size_t _add) {
		return _iterator + typename std::iterator_traits<IteratorType>::difference_type(_add);
	}
	
	///@brief: Concatenates two given cointainers.
	template<template<class, class...> class container_t, class item_t, class... rest_t>
	container_t<item_t, rest_t...> operator |(const container_t<item_t, rest_t...> & _left, const container_t<item_t, rest_t...> & _right) {
		container_t<item_t, rest_t...> both(_left);
		both.insert(both.end(), _right.begin(), _right.end());
		return both;
	}
		
		
	/// Pipe tuples
	template<size_t I = 0, typename... Tp>
	inline typename std::enable_if<I+1 == sizeof...(Tp), void>::type
	print(std::ostream& _out, const std::tuple<Tp...>& t) {
		_out << std::get<I>(t);
	}

	template<size_t I = 0, typename... Tp>
	inline typename std::enable_if<I+1 < sizeof...(Tp), void>::type
	print(std::ostream& _out, const std::tuple<Tp...>& t) {
		_out << std::get<I>(t) << ", ";
		print<I + 1, Tp...>(_out, t);
	} 
	
	template<class... Tp>
	std::ostream& operator<<(std::ostream& _out, const std::tuple<Tp...>& _tuple) {
		_out << "<";
		print<0, Tp...>(_out, _tuple);
		_out << ">";
		return _out;
	}
	
	/// Pipe normal containers to ostreams
	template<template<class, class...> class container_t, class item_t, class... rest_t, typename std::enable_if<
				std::is_base_of<std::vector<item_t>, typename std::decay<container_t<item_t>>::type>{} 
				|| std::is_base_of<std::set<item_t>, typename std::decay<container_t<item_t>>::type>{}, 
				int>::type = 0>
	std::ostream& operator<<(std::ostream& _out, const container_t<item_t, rest_t...>& _container) {
		if(_container.size() == 0) { _out << "{ }"; return _out; }
		_out << "{ ";
		for(const item_t& item : _container) { _out << item << ", "; }
		_out << "\b\b }";
		return _out;
	}

	template<class T, class U>
	std::ostream& operator<<(std::ostream& _out, const std::map<T,U>& _set) {
		if(_set.size() == 0) { _out << "{ }"; return _out; }
		_out << "{ ";
		for(const std::pair<T,U>& item : _set) {  _out << "(" << item.first << ", " << item.second << "), "; }
		_out << "\b\b }";
		return _out;
	}
	
}
