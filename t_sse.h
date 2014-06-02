//
//

#if !defined(_T_SSE_H_)
#define _T_SSE_H_

/*----------------------------------------------------------------------------*/ 
__forceinline	__m128	operator+(__m128 l, __m128 r)	{ return _mm_add_ps(l,r);		}
__forceinline	__m128	operator-(__m128 l, __m128 r)	{ return _mm_sub_ps(l,r);		}
__forceinline	__m128	operator*(__m128 l, __m128 r)	{ return _mm_mul_ps(l,r);		}
__forceinline	__m128	operator/(__m128 l, __m128 r)	{ return _mm_div_ps(l,r);		}
__forceinline	__m128	operator&(__m128 l, __m128 r)	{ return _mm_and_ps(l,r);		}
__forceinline	__m128	operator|(__m128 l, __m128 r)	{ return _mm_or_ps(l,r);		}
__forceinline	__m128	operator<(__m128 l, __m128 r)	{ return _mm_cmplt_ps(l,r);		}
__forceinline	__m128	operator>(__m128 l, __m128 r)	{ return _mm_cmpgt_ps(l,r);		}
__forceinline	__m128	operator<=(__m128 l, __m128 r)	{ return _mm_cmple_ps(l,r);		}
__forceinline	__m128	operator>=(__m128 l, __m128 r)	{ return _mm_cmpge_ps(l,r);		}
__forceinline	__m128	operator!=(__m128 l, __m128 r)	{ return _mm_cmpneq_ps(l,r);	}
__forceinline	__m128	operator==(__m128 l, __m128 r)	{ return _mm_cmpeq_ps(l,r);		}

__forceinline	__m128	_mm_merge_ps(__m128 m, __m128 l, __m128 r)
{
	return _mm_andnot_ps(m, l) | (m & r);
}

/*----------------------------------------------------------------------------*/ 
extern __m128	g_zero4;
extern __m128	g_one4;
extern __m128	g_fltMax4;
extern __m128	g_mask4;
extern __m128	g_epsilon4;

/*----------------------------------------------------------------------------*/ 
__forceinline	__m256	operator+(__m256 l, __m256 r)	{ return _mm256_add_ps(l,r);				}
__forceinline	__m256	operator-(__m256 l, __m256 r)	{ return _mm256_sub_ps(l,r);				}
__forceinline	__m256	operator*(__m256 l, __m256 r)	{ return _mm256_mul_ps(l,r);				}
__forceinline	__m256	operator/(__m256 l, __m256 r)	{ return _mm256_div_ps(l,r);				}
__forceinline	__m256	operator&(__m256 l, __m256 r)	{ return _mm256_and_ps(l,r);				}
__forceinline	__m256	operator|(__m256 l, __m256 r)	{ return _mm256_or_ps(l,r);					}
__forceinline	__m256	operator<(__m256 l, __m256 r)	{ return _mm256_cmp_ps(l,r,_CMP_LT_OQ);		}
__forceinline	__m256	operator>(__m256 l, __m256 r)	{ return _mm256_cmp_ps(l,r,_CMP_GT_OQ);		}
__forceinline	__m256	operator<=(__m256 l, __m256 r)	{ return _mm256_cmp_ps(l,r,_CMP_LE_OQ);		}
__forceinline	__m256	operator>=(__m256 l, __m256 r)	{ return _mm256_cmp_ps(l,r,_CMP_GE_OQ);		}
__forceinline	__m256	operator!=(__m256 l, __m256 r)	{ return _mm256_cmp_ps(l,r,_CMP_NEQ_OS);	}
__forceinline	__m256	operator==(__m256 l, __m256 r)	{ return _mm256_cmp_ps(l,r,_CMP_EQ_OS);		}

__forceinline	__m256	_mm256_merge_ps(__m256 m, __m256 l, __m256 r)
{
	return _mm256_blendv_ps(l, r, m);
}

/*---------------------------------------------------------------------------*/ 
extern __m256	g_zero8;
extern __m256	g_one8;
extern __m256	g_fltMax8;
extern __m256	g_mask8;
extern __m256	g_epsilon8;

/*----------------------------------------------------------------------------*/ 

#endif //_T_SSE_H_