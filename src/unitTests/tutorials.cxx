#include<xerus.h>

#include "../../include/xerus/test/test.h"
using namespace xerus;

static misc::UnitTest tut_quick("Tutorials", "quick_start", [](){
	xerus::Tensor A({512,512}, [](const std::vector<size_t> &idx){
		if (idx[0] == idx[1]) {
			return 2.0;
		} else if (idx[1] == idx[0]+1 || idx[1]+1 == idx[0]) {
			return -1.0;
		} else {
			return 0.0;
		}
	});
	
	A *= 512*512;
	
	A.reinterpret_dimensions(std::vector<size_t>(18, 2));
	xerus::TTOperator ttA(A);
	
// 	std::cout << "ttA ranks: " << ttA.ranks() << std::endl;
	
	xerus::Tensor b({512}, []() {
		return 1.0;
	});
	
	b.reinterpret_dimensions(std::vector<size_t>(9, 2));
	xerus::TTTensor ttb(b);
	
	xerus::TTTensor ttx = xerus::TTTensor::random(std::vector<size_t>(9, 2), std::vector<size_t>(8, 3));
	
	xerus::ALS_SPD(ttA, ttx, ttb);
	
	xerus::Index i,j,k;
	
	double residual = frob_norm( ttA(i^9,j^9)*ttx(j^9) - ttb(i^9) );
	MTEST(residual < 3.5e-9, residual);
// 	std::cout << "residual: " << residual << std::endl;
	
	xerus::Tensor x;
	x(j^9) = b(i^9) / A(i^9, j^9);
	MTEST(frob_norm(x(i&0) - ttx(i&0)) < 3.5e-9, frob_norm(x(i&0) - ttx(i&0)));
// 	std::cout << Tensor(ttx).to_string() << std::endl;
// 	std::cout << "error: " << frob_norm(x(i&0) - ttx(i&0)) << std::endl;
});
