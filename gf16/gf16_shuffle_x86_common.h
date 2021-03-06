#ifndef _GF16_SHUFFLE_X86_COMMON_
#define _GF16_SHUFFLE_X86_COMMON_

#include "gf16_global.h"
#include "platform.h"

#define GF16_MULTBY_TWO_X2(p) ((((p) << 1) & 0xffffffff) ^ ((GF16_POLYNOMIAL ^ ((GF16_POLYNOMIAL&0xffff) << 16)) & -((p) >> 31)))
#ifdef __SSSE3__
static HEDLEY_ALWAYS_INLINE void initial_mul_vector(uint16_t val, __m128i* prod, __m128i* prod4) {
	uint32_t val1 = val << 16;
	uint32_t val2 = val1 | val;
	val2 = GF16_MULTBY_TWO_X2(val2);
	__m128i tmp = _mm_cvtsi32_si128(val1);
#if defined(_AVAILABLE_AVX) || MWORD_SIZE >= 32
	*prod = _mm_insert_epi32(tmp, val2 ^ val1, 1);
#else
	*prod = _mm_unpacklo_epi32(tmp, _mm_cvtsi32_si128(val2 ^ val1));
#endif
	*prod4 = _mm_set1_epi32(GF16_MULTBY_TWO_X2(val2));
}
#endif

#ifdef _AVAILABLE


#if MWORD_SIZE != 64
ALIGN_TO(64, static char load_mask[64]) = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#endif

// load part of a vector, zeroing out remaining bytes
static inline _mword partial_load(const void* ptr, size_t bytes) {
#if MWORD_SIZE == 64
	// AVX512 is easy - masked load does the trick
	return _mm512_maskz_loadu_epi8((1ULL<<bytes)-1, ptr);
#else
	uintptr_t alignedPtr = ((uintptr_t)ptr & ~(sizeof(_mword)-1));
	_mword result;
	// does the load straddle across alignment boundary? (could check page boundary, but we'll be safer and only use vector alignment boundary)
	if((((uintptr_t)ptr+bytes) & ~(sizeof(_mword)-1)) != alignedPtr)
		result = _MMI(loadu)(ptr); // if so, unaligned load is safe
	else {
		// a shift could work, but painful on AVX2, so just give up and go through memory
		ALIGN_TO(MWORD_SIZE, _mword tmp[2]);
		_MMI(store)(tmp, _MMI(load)((_mword*)alignedPtr));
		result = _MMI(loadu)((_mword*)((uint8_t*)tmp + ((uintptr_t)ptr & (sizeof(_mword)-1))));
	}
	// mask out junk
	result = _MMI(and)(result, _MMI(loadu)((_mword*)( load_mask + 32 - bytes )));
	return result;
#endif
}

static HEDLEY_ALWAYS_INLINE void shuf0_vector(uint16_t val, __m128i* prod0, __m128i* prod8) {
	__m128i tmp, vval4;
	initial_mul_vector(val, &tmp, &vval4);
	*prod0 = _mm_unpacklo_epi64(tmp, _mm_xor_si128(tmp, vval4));
	
	// multiply by 2 and add prod0 to give prod8
	__m128i poly = _mm_and_si128(_mm_set1_epi16(GF16_POLYNOMIAL & 0xffff), _mm_cmpgt_epi16(
		_mm_setzero_si128(), vval4
	));
#if MWORD_SIZE == 64
	*prod8 = _mm_ternarylogic_epi32(
		_mm_add_epi16(vval4, vval4), poly, *prod0, 0x96
	);
#else
	*prod8 = _mm_xor_si128(
		_mm_add_epi16(vval4, vval4),
		_mm_xor_si128(*prod0, poly)
	);
#endif
	
/* // although the following seems simpler, it doesn't actually seem to be faster, although I don't know why
		uint8_t* multbl = (uint8_t*)scratch + sizeof(__m128i)*2;
		
		__m128i factor0 = _mm_load_si128((__m128i*)multbl + (val & 0xf));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128((__m128i*)(multbl + (val & 0xf0)) + 16));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128((__m128i*)(multbl + ((val>>4) & 0xf0)) + 32));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128((__m128i*)(multbl + ((val>>8) & 0xf0)) + 48));
		
		__m128i factor8 = _mm_shuffle_epi8(factor0, _mm_set_epi32(0x0f0f0f0f, 0x0f0f0f0f, 0x07070707, 0x07070707));
		factor0 = _mm_slli_epi64(factor0, 8);
		factor8 = _mm_xor_si128(factor0, factor8);
		
		low0 = BCAST(_mm_unpacklo_epi64(factor0, factor8));
		high0 = BCAST(_mm_unpackhi_epi64(factor0, factor8));
*/
}


static HEDLEY_ALWAYS_INLINE _mword separate_low_high(_mword data) {
	return _MM(shuffle_epi8)(data, _MM(set_epi32)(
#if MWORD_SIZE >= 64
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
#endif
#if MWORD_SIZE >= 32
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
#endif
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	));
}


#if MWORD_SIZE == 16 && defined(_AVAILABLE_XOP)
	#define _MM_SRLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(-4))
	#define _MM_SLLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(4))
#else
	#define _MM_SRLI4_EPI8(v) _MMI(and)(_MM(srli_epi16)(v, 4), _MM(set1_epi8)(0xf))
	#define _MM_SLLI4_EPI8(v) _MM(slli_epi16)(_MMI(and)(v, _MM(set1_epi8)(0xf)), 4)
#endif


#if MWORD_SIZE >= 32
static HEDLEY_ALWAYS_INLINE void mul16_vec2x(__m256i mulLo, __m256i mulHi, __m256i srcLo, __m256i srcHi, __m256i* dstLo, __m256i *dstHi) {
	__m256i ti = _mm256_and_si256(_mm256_srli_epi16(srcHi, 4), _mm256_set1_epi8(0xf));
#if MWORD_SIZE == 64
	__m256i th = _mm256_ternarylogic_epi32(
		_mm256_srli_epi16(srcLo, 4),
		_mm256_set1_epi8(0xf),
		_mm256_slli_epi16(srcHi, 4),
		0xE2
	);
	*dstLo = _mm256_ternarylogic_epi32(
		_mm256_shuffle_epi8(mulLo, ti),
		_mm256_set1_epi8(0xf),
		_mm256_slli_epi16(srcLo, 4),
		0xD2
	);
#else
	__m256i tl = _mm256_slli_epi16(_mm256_and_si256(_mm256_set1_epi8(0xf), srcLo), 4);
	__m256i th = _mm256_slli_epi16(_mm256_and_si256(_mm256_set1_epi8(0xf), srcHi), 4);
	th = _mm256_or_si256(th, _mm256_and_si256(_mm256_srli_epi16(srcLo, 4), _mm256_set1_epi8(0xf)));
	*dstLo = _mm256_xor_si256(tl, _mm256_shuffle_epi8(mulLo, ti));
#endif
	*dstHi = _mm256_xor_si256(th, _mm256_shuffle_epi8(mulHi, ti));
}
#endif

#if defined(_AVAILABLE_XOP)
	#define _MM128_SRLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(-4))
	#define _MM128_SLLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(4))
#else
	#define _MM128_SRLI4_EPI8(v) _mm_and_si128(_mm_srli_epi16(v, 4), _mm_set1_epi8(0xf))
	#define _MM128_SLLI4_EPI8(v) _mm_slli_epi16(_mm_and_si128(v, _mm_set1_epi8(0xf)), 4)
#endif
static HEDLEY_ALWAYS_INLINE void mul16_vec128(__m128i mulLo, __m128i mulHi, __m128i srcLo, __m128i srcHi, __m128i* dstLo, __m128i *dstHi) {
	__m128i ti = _MM128_SRLI4_EPI8(srcHi);
#if MWORD_SIZE == 64
	__m128i th = _mm_ternarylogic_epi32(
		_mm_srli_epi16(srcLo, 4),
		_mm_set1_epi8(0xf),
		_mm_slli_epi16(srcHi, 4),
		0xE2
	);
	*dstLo = _mm_ternarylogic_epi32(
		_mm_shuffle_epi8(mulLo, ti),
		_mm_set1_epi8(0xf),
		_mm_slli_epi16(srcLo, 4),
		0xD2
	);
#else
	__m128i tl = _MM128_SLLI4_EPI8(srcLo);
	__m128i th = _MM128_SLLI4_EPI8(srcHi);
	th = _mm_or_si128(th, _MM128_SRLI4_EPI8(srcLo));
	*dstLo = _mm_xor_si128(tl, _mm_shuffle_epi8(mulLo, ti));
#endif
	*dstHi = _mm_xor_si128(th, _mm_shuffle_epi8(mulHi, ti));
}


#endif // defined(_AVAILABLE)

#ifdef __AVX512BW__
static HEDLEY_ALWAYS_INLINE void mul16_vec4x(__m512i mulLo, __m512i mulHi, __m512i srcLo, __m512i srcHi, __m512i* dstLo, __m512i *dstHi) {
	__m512i ti = _mm512_and_si512(_mm512_srli_epi16(srcHi, 4), _mm512_set1_epi8(0xf));
	__m512i th = _mm512_ternarylogic_epi32(
		_mm512_srli_epi16(srcLo, 4),
		_mm512_set1_epi8(0xf),
		_mm512_slli_epi16(srcHi, 4),
		0xE2
	);
	*dstLo = _mm512_ternarylogic_epi32(
		_mm512_shuffle_epi8(mulLo, ti),
		_mm512_set1_epi8(0xf),
		_mm512_slli_epi16(srcLo, 4),
		0xD2
	);
	*dstHi = _mm512_xor_si512(th, _mm512_shuffle_epi8(mulHi, ti));
}
static HEDLEY_ALWAYS_INLINE __m512i gf16_mm512_set_si128(__m128i a, __m128i b, __m128i c, __m128i d) {
	return _mm512_inserti64x4(
		_mm512_castsi256_si512(
			_mm256_inserti128_si256(_mm256_castsi128_si256(d), c, 1)
		),
		_mm256_inserti128_si256(_mm256_castsi128_si256(b), a, 1),
		1
	);
}
static HEDLEY_ALWAYS_INLINE __m512i gf16_mm512_set_3si128(__m128i a, __m128i b, __m128i c) {
	return _mm512_inserti32x4(
		zext256_512(
			_mm256_inserti128_si256(_mm256_castsi128_si256(c), b, 1)
		), a, 2
	);
}
static HEDLEY_ALWAYS_INLINE __m512i separate_low_high512(__m512i v) {
	return _mm512_shuffle_epi8(v, _mm512_set4_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
}
#endif

#endif // defined(_GF16_SHUFFLE_X86_COMMON_)
