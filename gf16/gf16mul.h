#include <assert.h>
#include "../src/stdint.h"
#include "../src/hedley.h"
#include <vector>
#include <cstring>

typedef void(*Galois16MulTransform) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen);
typedef void(*Galois16MulUntransform) (void *HEDLEY_RESTRICT dst, size_t len);
typedef void(*Galois16MulFunc) (const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
typedef void(*Galois16PowFunc) (const void *HEDLEY_RESTRICT scratch, unsigned outputs, size_t offset, void **HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
typedef unsigned(*Galois16MulMultiFunc) (const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);
typedef void(*Galois16AddFunc) (void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len);



enum Galois16Methods {
	GF16_AUTO,
	GF16_LOOKUP,
	GF16_LOOKUP_SSE2,
	GF16_LOOKUP3,
	GF16_SHUFFLE_NEON,
	GF16_SHUFFLE_SSSE3,
	GF16_SHUFFLE_AVX,
	GF16_SHUFFLE_AVX2,
	GF16_SHUFFLE_AVX512,
	GF16_SHUFFLE_VBMI,
	GF16_SHUFFLE2X_AVX2,
	GF16_SHUFFLE2X_AVX512,
	GF16_XOR_SSE2,
	GF16_XOR_JIT_SSE2,
	GF16_XOR_JIT_AVX2,
	GF16_XOR_JIT_AVX512,
	GF16_AFFINE_GFNI,
	GF16_AFFINE_AVX512,
	GF16_AFFINE2X_GFNI,
	GF16_AFFINE2X_AVX512
	// TODO: consider non-transforming shuffle/affine
};
static const char* Galois16MethodsText[] = {
	"Auto",
	"LH Lookup",
	"LH Lookup (SSE2)",
	"3-part Lookup",
	"Shuffle (NEON)",
	"Shuffle (SSSE3)",
	"Shuffle (AVX)",
	"Shuffle (AVX2)",
	"Shuffle (AVX512)",
	"Shuffle (VBMI)",
	"Shuffle2x (AVX2)",
	"Shuffle2x (AVX512)",
	"Xor (SSE2)",
	"Xor-Jit (SSE2)",
	"Xor-Jit (AVX2)",
	"Xor-Jit (AVX512)",
	"Affine (GFNI)",
	"Affine (GFNI+AVX512)",
	"Affine2x (GFNI)",
	"Affine2x (GFNI+AVX512)"
};

typedef struct {
	Galois16Methods id;
	const char* name;
	size_t alignment;
	size_t stride;
	size_t idealChunkSize;
} Galois16MethodInfo;

class Galois16Mul {
private:
	void* scratch;
	Galois16MethodInfo _info;
	
	static void addGeneric(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len);
	Galois16MulFunc _mul;
	Galois16MulFunc _mul_add;
	Galois16AddFunc _add;
	Galois16PowFunc _pow;
	Galois16PowFunc _pow_add;
	Galois16MulMultiFunc _mul_add_multi;
	
	static unsigned _mul_add_multi_none(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);
	static void _prepare_none(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
		memcpy(dst, src, srcLen);
	}
	static void _finish_none(void *HEDLEY_RESTRICT, size_t) {}
	
	Galois16Methods _method;
	void setupMethod(Galois16Methods method);
	
	// disable copy constructor
	Galois16Mul(const Galois16Mul&);
	Galois16Mul& operator=(const Galois16Mul&);
	
#if __cplusplus >= 201100
	void move(Galois16Mul& other);
#endif
	
public:
	static Galois16Methods default_method(size_t regionSizeHint = 0, unsigned outputs = 0, unsigned threadCountHint = 0);
	Galois16Mul(Galois16Methods method = GF16_AUTO);
	~Galois16Mul();
	
#if __cplusplus >= 201100
	Galois16Mul(Galois16Mul&& other) noexcept {
		move(other);
	}
	Galois16Mul& operator=(Galois16Mul&& other) noexcept {
		move(other);
		return *this;
	}
#endif
	
	inline bool needPrepare() const {
		return prepare != &Galois16Mul::_prepare_none;
	};
	inline bool hasMultiMulAdd() const {
		return _mul_add_multi != &Galois16Mul::_mul_add_multi_none;
	};
	inline bool hasPowAdd() const {
		return _pow_add != NULL;
	};
	
	static std::vector<Galois16Methods> availableMethods(bool checkCpuid);
	static inline const char* methodToText(Galois16Methods m) {
		return Galois16MethodsText[(int)m];
	}
	
	inline const Galois16MethodInfo& info() const {
		return _info;
	}
	
	Galois16MulTransform prepare;
	Galois16MulUntransform finish;
	
	void* mutScratch_alloc() const;
	void mutScratch_free(void* mutScratch) const;
	
	inline void mul(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) const {
		assert(((uintptr_t)dst & (_info.alignment-1)) == 0);
		assert(((uintptr_t)src & (_info.alignment-1)) == 0);
		assert((len & (_info.stride-1)) == 0);
		assert(len > 0);
		
		if(!(coefficient & 0xfffe)) {
			if(coefficient == 0)
				memset(dst, 0, len);
			else
				memcpy(dst, src, len);
		}
		else if(_mul)
			_mul(scratch, dst, src, len, coefficient, mutScratch);
		else {
			memset(dst, 0, len);
			_mul_add(scratch, dst, src, len, coefficient, mutScratch);
		}
	}
	inline void mul_add(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) const {
		assert(((uintptr_t)dst & (_info.alignment-1)) == 0);
		assert(((uintptr_t)src & (_info.alignment-1)) == 0);
		assert((len & (_info.stride-1)) == 0);
		assert(len > 0);
		
		if(!(coefficient & 0xfffe)) {
			if(coefficient == 0)
				return;
			else
				_add(dst, src, len);
		}
		else
			_mul_add(scratch, dst, src, len, coefficient, mutScratch);
	}
	
	inline void pow(unsigned outputs, size_t offset, void **HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) const {
		assert((len & (_info.stride-1)) == 0);
		assert(len > 0);
		assert(outputs > 0);
		
		if(!(coefficient & 0xfffe)) {
			if(coefficient == 0) {
				for(unsigned output = 0; output < outputs; output++)
					memset((uint8_t*)dst[output] + offset, 0, len);
			} else {
				for(unsigned output = 0; output < outputs; output++)
					memcpy((uint8_t*)dst[output] + offset, (uint8_t*)src + offset, len);
			}
		}
		else if(_pow)
			_pow(scratch, outputs, offset, dst, src, len, coefficient, mutScratch);
		else if(_pow_add) {
			for(unsigned output = 0; output < outputs; output++)
				memset((uint8_t*)dst[output] + offset, 0, len);
			_pow_add(scratch, outputs, offset, dst, src, len, coefficient, mutScratch);
		}
		else if(_mul) {
			void* prev = (uint8_t*)src + offset;
			for(unsigned output = 0; output < outputs; output++) {
				void* cur = (uint8_t*)dst[output] + offset;
				_mul(scratch, cur, prev, len, coefficient, mutScratch);
				prev = cur;
			}
		}
		else {
			void* prev = (uint8_t*)src + offset;
			for(unsigned output = 0; output < outputs; output++) {
				void* cur = (uint8_t*)dst[output] + offset;
				memset(cur, 0, len);
				_mul_add(scratch, cur, prev, len, coefficient, mutScratch);
				prev = cur;
			}
		}
	}
	inline void pow_add(unsigned outputs, size_t offset, void **HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) const {
		assert((len & (_info.stride-1)) == 0);
		assert(len > 0);
		assert(outputs > 0);
		
		if(!(coefficient & 0xfffe)) {
			if(coefficient == 0)
				return;
			else
				for(unsigned output = 0; output < outputs; output++)
					_add((uint8_t*)dst[output] + offset, (uint8_t*)src + offset, len);
		}
		else
			_pow_add(scratch, outputs, offset, dst, src, len, coefficient, mutScratch);
	}
	
	inline void mul_add_multi(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) const {
		assert((len & (_info.stride-1)) == 0);
		assert(len > 0);
		assert(regions > 0);
		
		unsigned region;
		
		region = _mul_add_multi(scratch, regions, offset, dst, src, len, coefficients, mutScratch);
		
		// process remaining regions
		for(; region<regions; region++) {
			mul_add((uint8_t*)dst+offset, ((uint8_t*)src[region])+offset, len, coefficients[region], mutScratch);
		}
	}
	
};
