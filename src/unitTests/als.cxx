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


#include<xerus.h>

#include "../../include/xerus/misc/test.h"
using namespace xerus;


UNIT_TEST(ALS, identity,
    //Random numbers
    std::mt19937_64 rnd;
    rnd.seed(73);
	std::normal_distribution<value_t> dist (0.0, 1.0);
    
    Index k,l,m,n,o,p;
    
    FullTensor X({10, 10, 10});
    FullTensor B = FullTensor::construct_random({10, 10, 10}, rnd, dist);
    
    FullTensor I({10,10,10,10,10,10}, [](const std::vector<size_t> &_idx) {
		if (_idx[0]==_idx[3] && _idx[1] == _idx[4] && _idx[2] == _idx[5]) {
			return 1.0;
		} else {
			return 0.0;
		}
	});
    
    X(k^3) = I(k^3,l^3)*B(l^3);
    TEST(frob_norm(X(k^3) - B(k^3)) < 1e-13);
    
    TTTensor ttB(B, 0.001);
    TTTensor ttX(X, 0.001);
    TTOperator ttI(I, 0.001);
	
	ttX(k^3) = ttI(k^3,l^3)*ttB(l^3);
    TEST(frob_norm(ttI(k^3,l^3)*ttB(l^3) - ttB(k^3)) < 1e-13);
	TEST(frob_norm(ttI(k^3,l^3)*ttX(l^3) - ttX(k^3)) < 1e-13);
    
    std::vector<double> perfdata;
    
    TEST(ALS(ttI, ttX, ttB, 0.001, &perfdata) < 0.01);
    TEST(frob_norm(FullTensor(ttX)(k^3) - FullTensor(ttB)(k^3)) < 1e-13 * 1000);
	LOG(unit_test, "perf: " << perfdata);
    perfdata.clear();
	
    ttX = TTTensor::construct_random(ttX.dimensions, ttX.ranks(), rnd, dist);
    TEST(ALS(ttI, ttX, ttB, 0.001, &perfdata) < 0.01);
	LOG(unit_test, "perf: " << perfdata);
	LOG(unit_test, "norm: " << frob_norm(FullTensor(ttX)(k^3) - FullTensor(ttB)(k^3)));
    TEST(frob_norm(FullTensor(ttX)(k^3) - FullTensor(ttB)(k^3)) < 1e-9); // approx 1e-16 * dim * max_entry
)


UNIT_TEST(ALS, projectionALS,
    //Random numbers
    std::mt19937_64 rnd;
    rnd.seed(0x5EED);
	std::normal_distribution<value_t> dist (0.0, 1.0);
    
    Index k,l,m,n,o,p;
	
	
    
	TTTensor B = TTTensor::construct_random({4,4,4,4,4}, {4,8,8,4}, rnd, dist);
	value_t normB = frob_norm(B);
	TTTensor X = B;
	for (size_t r = 7; r > 0; --r) {
		X.round(r);
		value_t roundNorm = frob_norm(X-B);
		ALS(X,B,1e-4);
		value_t projNorm = frob_norm(X-B);
		LOG(unit_test, r << " : " << roundNorm << " > " << projNorm);
		TEST(projNorm < roundNorm);
	}
	TEST(misc::approx_equal(frob_norm(B), normB, 0.));
)

#include <iomanip>

UNIT_TEST(ALS, tutorial,
	std::mt19937_64 rnd;
	std::normal_distribution<double> dist (0.0, 1.0);
	xerus::Index i,j,k;
	
	const size_t d = 10;

	const std::vector<size_t> stateDims(d, 4);
	const std::vector<size_t> operatorDims(2*d, 4);
	
    xerus::TTTensor B = xerus::TTTensor::construct_random(stateDims, 2, rnd, dist);
	xerus::TTTensor X = xerus::TTTensor::construct_random(stateDims, 2, rnd, dist);
	
	xerus::TTOperator A = xerus::TTOperator::construct_identity(operatorDims);
	
	xerus::ALS(A, X, B);
	
	TEST(misc::approx_equal(frob_norm(X-B), 0., 1e-12));
	
	A = xerus::TTOperator::construct_random(operatorDims, 2, rnd, dist);
	
	A(i^d, k^d) = A(i^d, j^d) * A(k^d, j^d);
	
	TEST(A.ranks()==std::vector<size_t>(d-1,4));

	// TODO should also be in the tutorial
	
	value_t max = std::max(A.get_component(0)[0],A.get_component(0)[1]);
	max = std::max(A.get_component(0)[2], std::max(A.get_component(0)[3], max));
	A.set_component(0, static_cast<const FullTensor&>(A.get_component(0))/max);
	
	TTTensor C;
	C(i&0) = A(i/2, j/2) * B(j&0);
	X = xerus::TTTensor::construct_random(stateDims, 2, rnd, dist);
	
	xerus::ALSVariant ALSb(xerus::ALS);
	ALSb.printProgress = true;
	ALSb.useResidualForEndCriterion = true;
	std::vector<double> perfdata;
	
	ALSb(A, X, C, 1e-12, &perfdata);
	TEST(misc::approx_equal(frob_norm(A(i/2, j/2)*X(j&0) - C(i&0)), 0., 1e-4));
// 	LOG(HierKommenDieDaten, perfdata);
	std::cout << "Residual " << std::scientific << frob_norm(A(i/2, j/2)*X(j&0) - C(i&0)) << "     " << std::endl;
// 	for (value_t &e : perfdata) {
// 		e-=perfdata.back();
// 	}
	std::cout << std::scientific << perfdata << std::endl;
	
	
// 	perfdata.clear();
// 	ALSb(A, X, B, 1e-4, &perfdata);
// 	TEST(!misc::approx_equal(frob_norm(A(i^d, j^d)*X(j&0) - B(i&0)), 0., 1.));
// 	std::cout << perfdata << std::endl;
)
