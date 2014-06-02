// Bicubic CS Test.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "Bicubic CS Test.h"
#include "t_sse.h"

#include <vector>
#include <atlbase.h>
#include <d3d11.h>
#include <wincodec.h>

#pragma comment(lib, "d3d11.lib")

/*---------------------------------------------------------------------------*/
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);

void ResizeCPU(HWND hWnd, enum ResizeType resizeType);
void ResizeCS(HWND hWnd, enum ResizeType resizeType);

#pragma pack(push)
#pragma pack(1)
struct BGRA
{
	BGRA(BYTE r, BYTE g, BYTE b, BYTE a) : R(r), G(g), B(b), A(a) {}

	struct fBGRA operator*(float v) const;
	struct iBGRA operator*(int v) const;

	BYTE R, G, B, A;
};

BGRA g_black(0, 0, 0, 0);

struct fBGRA
{
	fBGRA() {}
	fBGRA(float r, float g, float b, float a) : R(r), G(g), B(b), A(a) {}

	struct fBGRA operator+(const fBGRA& v) const;
	struct fBGRA operator*(float v) const;
	operator struct BGRA();

	float R, G, B, A;
};

fBGRA g_fBlack(0, 0, 0, 0);

struct iBGRA
{
	iBGRA(int r, int g, int b, int a) : R(r), G(g), B(b), A(a) {}

	struct iBGRA operator+(const iBGRA& v) const;
	struct iBGRA operator*(int v) const;
	struct iBGRA operator/(int v) const;
	struct iBGRA operator>>(int v) const;
	operator struct BGRA();

	int R, G, B, A;
};

fBGRA BGRA::operator*(float v) const
{
	return fBGRA(R*v, G*v, B*v, A*v);
}

iBGRA BGRA::operator*(int v) const
{
	return iBGRA(R*v, G*v, B*v, A*v);
}

fBGRA fBGRA::operator+(const fBGRA& v) const
{
	return fBGRA(R + v.R, G + v.G, B + v.B, A + v.A);
}

fBGRA fBGRA::operator*(float v) const
{
	return fBGRA(R*v, G*v, B*v, A*v);
}

fBGRA::operator struct BGRA()
{
	return BGRA((BYTE)R, (BYTE)G, (BYTE)B, (BYTE)A);
}

iBGRA iBGRA::operator+(const iBGRA& v) const
{
	return iBGRA(R + v.R, G + v.G, B + v.B, A + v.A);
}

iBGRA iBGRA::operator*(int v) const
{
	return iBGRA(R*v, G*v, B*v, A*v);
}

iBGRA iBGRA::operator/(int v) const
{
	return iBGRA(R / v, G / v, B / v, A / v);
}

iBGRA iBGRA::operator>>(int v) const
{
	return iBGRA(R >> v, G >> v, B >> v, A >> v);
}

iBGRA::operator struct BGRA()
{
	return BGRA((BYTE)R, (BYTE)G, (BYTE)B, (BYTE)A);
}

#pragma pack(pop)

/*---------------------------------------------------------------------------*/

CComPtr<ID3D11Device>				g_device;
CComPtr<ID3D11DeviceContext>		g_context;

CComPtr<ID3D11ComputeShader>		g_BruteForceCS;
CComPtr<ID3D11ComputeShader>		g_OptimizedCS;
CComPtr<ID3D11Buffer>				g_constBuffer;
CComPtr<ID3D11Texture2D>			g_sourceBuffer;
CComPtr<ID3D11ShaderResourceView>	g_sourceView;
CComPtr<ID3D11Texture2D>			g_targetBuffer;
CComPtr<ID3D11UnorderedAccessView>	g_targetView;
CComPtr<ID3D11Texture2D>			g_copyBuffer;

CComPtr<ID3D11Query>				g_query;

__int64 start, end, freq;
DWORD64 rdClock;
DWORD64 rdStart, rdEnd;
DWORD64 rdMin = UINT64_MAX;

int g_image_width = 256;
int g_image_height = 256;
BGRA* g_source;

const int g_screen_width = 1024;
const int g_screen_height = 1024;
BGRA* g_screen = (BGRA*)_aligned_malloc(sizeof(BGRA)* g_screen_width * g_screen_height, 16);

__m128* g_image_data = NULL;
__m128* g_temp_data = (__m128*)_aligned_malloc(sizeof(__m128) * (g_screen_width * 4), 16);

int g_thread_width = 16;
int g_thread_height = 16;

float zoom = 0.5f;
float ox = -(g_screen_width)* 0.5f;
float oy = -(g_screen_height)* 0.5f;

BITMAPINFO g_bitmapInfo;

enum ResizeType
{
	ResizeType_BruteForce, 
	ResizeType_Optimized, 
	ResizeType_OptimizedSSE,
	ResizeType_BruteForceCS,
	ResizeType_OptimizedCS,
};

ResizeType g_resizeType = ResizeType_BruteForce;

/*---------------------------------------------------------------------------*/
std::vector<BYTE> LoadShader(LPCTSTR filename)
{
	TCHAR moduleName[MAX_PATH];
	GetModuleFileName(NULL, moduleName, MAX_PATH);

	TCHAR fullpath[MAX_PATH];
	LPTSTR filePart;
	GetFullPathName(moduleName, MAX_PATH, fullpath, &filePart);

	_tcscpy_s(filePart, MAX_PATH - (filePart - fullpath), filename);

	std::vector<BYTE> shader;

	FILE* file;
	if (_tfopen_s(&file, fullpath, _T("rb")) == 0)
	{
		fseek(file, 0, SEEK_END);
		size_t size = ftell(file);
		fseek(file, 0, SEEK_SET);

		shader.resize(size);
		fread(&shader[0], size, 1, file);
		fclose(file);
	}

	return shader;
}

std::vector<BYTE> LoadImage(LPCWSTR imageFile, int* imageWidth, int* imageHeight)
{
	std::vector<BYTE> source;

	CComPtr<IWICImagingFactory> m_pIWICFactory;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pIWICFactory));
	if (SUCCEEDED(hr))
	{
		CComPtr<IWICBitmapDecoder> pIDecoder;
		hr = m_pIWICFactory->CreateDecoderFromFilename(imageFile, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pIDecoder);
		if (SUCCEEDED(hr))
		{
			CComPtr<IWICBitmapFrameDecode> pIDecoderFrame;
			hr = pIDecoder->GetFrame(0, &pIDecoderFrame);
			if (SUCCEEDED(hr))
			{
				CComPtr<IWICFormatConverter> pIFormatConverter;
				hr = m_pIWICFactory->CreateFormatConverter(&pIFormatConverter);
				if (SUCCEEDED(hr))
				{
					WICPixelFormatGUID pixelFormat;
					pIDecoderFrame->GetPixelFormat(&pixelFormat);

					BOOL canConvert;
					if (SUCCEEDED(pIFormatConverter->CanConvert(pixelFormat, GUID_WICPixelFormat32bppRGBA, &canConvert)) && canConvert)
					{
						pIFormatConverter->Initialize(pIDecoderFrame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeErrorDiffusion, 0, 0, WICBitmapPaletteTypeCustom);

						UINT width, height;
						pIDecoderFrame->GetSize(&width, &height);

						*imageWidth = width;
						*imageHeight = height;

						source.resize(width * height * 4);

						pIFormatConverter->CopyPixels(NULL, width * 4, width * height * 4, &source[0]);
					}
				}
			}
		}
	}

	return source;
}

/*---------------------------------------------------------------------------*/
const BGRA& p(int x, int y)
{
	if (x >= 0 && x < g_image_width && y >= 0 && y < g_image_height)
	{
		return g_source[g_image_width*y + x];
	}
	else
	{
		return g_black;
	}
}

float w0(float t)
{
	return (1.0f / 6.0f)*(-t*t*t + 3.0f*t*t - 3.0f*t + 1.0f);
}

float w1(float t)
{
	return (1.0f / 6.0f)*(3.0f*t*t*t - 6.0f*t*t + 4.0f);
}

float w2(float t)
{
	return (1.0f / 6.0f)*(-3.0f*t*t*t + 3.0f*t*t + 3.0f*t + 1.0f);
}

float w3(float t)
{
	return (1.0f / 6.0f)*(t*t*t);
}

fBGRA px(int x, int y, float dx)
{
	return p(x - 1, y)*w0(dx) + p(x, y)*w1(dx) + p(x + 1, y)*w2(dx) + p(x + 2, y)*w3(dx);
}

fBGRA pxy(int x, int y, float dx, float dy)
{
	return px(x, y - 1, dx)*w0(dy) + px(x, y, dx)*w1(dy) + px(x, y + 1, dx)*w2(dy) + px(x, y + 2, dx)*w3(dy);
}

/*---------------------------------------------------------------------------*/
void ResizeBruteForce()
{
	if (g_source)
	{
		float sx = ox + (g_image_width / zoom) / 2;
		float sy = oy + (g_image_height / zoom) / 2;

		for (int y = 0; y<g_screen_height; ++y)
		{
			int v = (int)floor((y + sy) * zoom);
			float dv = ((y + sy) * zoom) - v;

			for (int x = 0; x<g_screen_width; ++x)
			{
				int u = (int)floor((x + sx) * zoom);
				float du = ((x + sx) * zoom) - u;

				g_screen[g_screen_width*y + x] = pxy(u, v, du, dv);
			}
		}
	}
}

/*---------------------------------------------------------------------------*/
void px_opt(int y, fBGRA result[g_screen_width], int u[g_screen_width], float wx[g_screen_width][4], int x_loop[9])
{
	BGRA* p = g_source + g_image_width*y;

	if (y >= 0 && y<g_image_height)
	{
		int x = 0;
		for (; x<x_loop[0]; ++x)
		{
			result[x] = g_fBlack;
		}
		for (; x<x_loop[1]; ++x)
		{
			result[x] = p[u[x] + 2] * wx[x][3];
		}
		for (; x<x_loop[2]; ++x)
		{
			result[x] = p[u[x] + 1] * wx[x][2] + p[u[x] + 2] * wx[x][3];
		}
		for (; x<x_loop[3]; ++x)
		{
			result[x] = p[u[x] + 0] * wx[x][1] + p[u[x] + 1] * wx[x][2] + p[u[x] + 2] * wx[x][3];
		}

		for (; x<x_loop[4]; ++x)
		{
			result[x] = p[u[x] - 1] * wx[x][0] + p[u[x] + 0] * wx[x][1] + p[u[x] + 1] * wx[x][2] + p[u[x] + 2] * wx[x][3];
		}

		for (; x<x_loop[5]; ++x)
		{
			result[x] = p[u[x] - 1] * wx[x][0] + p[u[x] + 0] * wx[x][1] + p[u[x] + 1] * wx[x][2];
		}

		for (; x<x_loop[6]; ++x)
		{
			result[x] = p[u[x] - 1] * wx[x][0] + p[u[x] + 0] * wx[x][1];
		}

		for (; x<x_loop[7]; ++x)
		{
			result[x] = p[u[x] - 1] * wx[x][0];
		}

		for (; x<x_loop[8]; ++x)
		{
			result[x] = g_fBlack;
		}
	}
	else
	{
		memset(result, 0, sizeof(fBGRA)*g_screen_width);
	}
}

/*---------------------------------------------------------------------------*/
void ResizeOptimized()
{
	if (g_source)
	{
		float sx = ox + (g_image_width / zoom) / 2 + 0.5f;
		float sy = oy + (g_image_height / zoom) / 2 + 0.5f;

		int v_last = INT_MIN;

		int u[g_screen_width];
		float wx[g_screen_width][4];

		int x_loop[9] = { 0, 0, 0, 0, g_screen_width, g_screen_width, g_screen_width, g_screen_width, g_screen_width };
		int last = (int)floor((sx)* zoom - 0.5f) - 4;

		for (int x = 0; x<g_screen_width; ++x)
		{
			float fu = (x + sx) * zoom - 0.5f;
			u[x] = (int)floor(fu);

			//u[x]-1 >= 0; 
			if (u[x] + 2 < 0) x_loop[0] = x + 1;
			if (u[x] + 1 < 0) x_loop[1] = x + 1;
			if (u[x] + 0 < 0) x_loop[2] = x + 1;
			if (u[x] - 1 < 0) x_loop[3] = x + 1;
			//u[x]+2 < g_image_width;
			if (u[x] + 2 < g_image_width) x_loop[4] = x + 1;
			if (u[x] + 1 < g_image_width) x_loop[5] = x + 1;
			if (u[x] + 0 < g_image_width) x_loop[6] = x + 1;
			if (u[x] - 1 < g_image_width) x_loop[7] = x + 1;

			float t = fu - u[x];
			float t2 = t * t;
			float t3 = t2 * t;

			wx[x][0] = (1.0f / 6.0f)*(-t3 + 3.0f*t2 - 3.0f*t + 1.0f);
			wx[x][1] = (1.0f / 6.0f)*(3.0f*t3 - 6.0f*t2 + 4.0f);
			wx[x][2] = (1.0f / 6.0f)*(-3.0f*t3 + 3.0f*t2 + 3.0f*t + 1.0f);
			wx[x][3] = (1.0f / 6.0f)*(t3);
		}

		fBGRA data[g_screen_width * 4];
		fBGRA* temp[4] = { data + g_screen_width * 0, data + g_screen_width * 1, data + g_screen_width * 2, data + g_screen_width * 3 };
		fBGRA* swap[3];

		for (int y = 0; y<g_screen_height; ++y)
		{
			float fv = (y + sy) * zoom - 0.5f;
			int v = (int)floor(fv);

			switch (v - v_last)
			{
			case 0:
				break;
			case 1:
				swap[0] = temp[0];

				temp[0] = temp[1];
				temp[1] = temp[2];
				temp[2] = temp[3];
				temp[3] = swap[0];

				px_opt(v + 2, temp[3], u, wx, x_loop);
				break;
			case 2:
				swap[0] = temp[0];
				swap[1] = temp[1];

				temp[0] = temp[2];
				temp[1] = temp[3];
				temp[2] = swap[0];
				temp[3] = swap[1];

				px_opt(v + 1, temp[2], u, wx, x_loop);
				px_opt(v + 2, temp[3], u, wx, x_loop);
				break;
			case 3:
				swap[0] = temp[0];
				swap[1] = temp[1];
				swap[2] = temp[2];

				temp[0] = temp[3];
				temp[1] = temp[0];
				temp[2] = swap[1];
				temp[3] = swap[2];

				px_opt(v + 0, temp[1], u, wx, x_loop);
				px_opt(v + 1, temp[2], u, wx, x_loop);
				px_opt(v + 2, temp[3], u, wx, x_loop);
				break;
			default:
				px_opt(v - 1, temp[0], u, wx, x_loop);
				px_opt(v + 0, temp[1], u, wx, x_loop);
				px_opt(v + 1, temp[2], u, wx, x_loop);
				px_opt(v + 2, temp[3], u, wx, x_loop);
				break;
			}

			v_last = v;

			float t = fv - v;
			float t2 = t * t;
			float t3 = t2 * t;

			float wy[4];
			wy[0] = (1.0f / 6.0f)*(-t3 + 3.0f*t2 - 3.0f*t + 1.0f);
			wy[1] = (1.0f / 6.0f)*(3.0f*t3 - 6.0f*t2 + 4.0f);
			wy[2] = (1.0f / 6.0f)*(-3.0f*t3 + 3.0f*t2 + 3.0f*t + 1.0f);
			wy[3] = (1.0f / 6.0f)*(t3);

			for (int x = 0; x<g_screen_width; ++x)
			{
				fBGRA value = temp[0][x] * wy[0] + temp[1][x] * wy[1] + temp[2][x] * wy[2] + temp[3][x] * wy[3];

				g_screen[g_screen_width*y + x] = value;
			}
		}
	}
}

/*---------------------------------------------------------------------------*/
void px_sse(int y, BGRA* g_source, __m128* image_sse, __m128 result_sse[g_screen_width], int u[g_screen_width], __m128 wx_sse[g_screen_width][4], int x_loop[9])
{
	if (y >= 0 && y<g_image_height)
	{
		BGRA* image = g_source + g_image_width * y;

		__m128* iter = image_sse;
		for (int x = 0; x<g_image_width / 4; ++x)
		{
			__m128i current = _mm_load_si128((__m128i*)(image));
			__m128i low = _mm_unpacklo_epi8(current, _mm_setzero_si128());
			__m128i high = _mm_unpackhi_epi8(current, _mm_setzero_si128());
			image += 4;
			*iter++ = _mm_cvtepi32_ps(_mm_unpacklo_epi16(low, _mm_setzero_si128()));
			*iter++ = _mm_cvtepi32_ps(_mm_unpackhi_epi16(low, _mm_setzero_si128()));
			*iter++ = _mm_cvtepi32_ps(_mm_unpacklo_epi16(high, _mm_setzero_si128()));
			*iter++ = _mm_cvtepi32_ps(_mm_unpackhi_epi16(high, _mm_setzero_si128()));
		}

		int x = 0;
		for (; x<x_loop[0]; ++x)
		{
			result_sse[x] = g_zero4;
		}
		for (; x<x_loop[1]; ++x)
		{
			result_sse[x] = wx_sse[x][3] * image_sse[u[x] + 2];
		}
		for (; x<x_loop[2]; ++x)
		{
			result_sse[x] = wx_sse[x][2] * image_sse[u[x] + 1] + wx_sse[x][3] * image_sse[u[x] + 2];
		}
		for (; x<x_loop[3]; ++x)
		{
			result_sse[x] = wx_sse[x][1] * image_sse[u[x] + 0] + wx_sse[x][2] * image_sse[u[x] + 1] + wx_sse[x][3] * image_sse[u[x] + 2];
		}
		for (; x<x_loop[4]; ++x)
		{
			result_sse[x] = wx_sse[x][0] * image_sse[u[x] - 1] + wx_sse[x][1] * image_sse[u[x] + 0] + wx_sse[x][2] * image_sse[u[x] + 1] + wx_sse[x][3] * image_sse[u[x] + 2];
		}
		for (; x<x_loop[5]; ++x)
		{
			result_sse[x] = wx_sse[x][0] * image_sse[u[x] - 1] + wx_sse[x][1] * image_sse[u[x] + 0] + wx_sse[x][2] * image_sse[u[x] + 1];
		}
		for (; x<x_loop[6]; ++x)
		{
			result_sse[x] = wx_sse[x][0] * image_sse[u[x] - 1] + wx_sse[x][1] * image_sse[u[x] + 0];
		}
		for (; x<x_loop[7]; ++x)
		{
			result_sse[x] = wx_sse[x][0] * image_sse[u[x] - 1];
		}
		for (; x<x_loop[8]; ++x)
		{
			result_sse[x] = g_zero4;
		}
	}
	else
	{
		for (int x = 0; x<g_screen_width; ++x)
		{
			result_sse[x] = g_zero4;
		}
	}
}

/*---------------------------------------------------------------------------*/
void ResizeOptimized_sse()
{
	if (g_source)
	{
		float sx = ox + (g_image_width / zoom) / 2 + 0.5f;
		float sy = oy + (g_image_height / zoom) / 2 + 0.5f;

		const __m128 c0_sse = _mm_set_ps(1.0f / 6.0f, 4.0f / 6.0f, 1.0f / 6.0f, 0.0f / 6.0f);
		const __m128 c1_sse = _mm_set_ps(-3.0f / 6.0f, 0.0f / 6.0f, 3.0f / 6.0f, 0.0f / 6.0f);
		const __m128 c2_sse = _mm_set_ps(3.0f / 6.0f, -6.0f / 6.0f, 3.0f / 6.0f, 0.0f / 6.0f);
		const __m128 c3_sse = _mm_set_ps(-1.0f / 6.0f, 3.0f / 6.0f, -3.0f / 6.0f, 1.0f / 6.0f);

		int u[g_screen_width];
		int x_loop[9] = { 0, 0, 0, 0, g_screen_width, g_screen_width, g_screen_width, g_screen_width, g_screen_width };
		__m128 wx_sse[g_screen_width][4];

		for (int x = 0; x<g_screen_width; ++x)
		{
			float fu = (x + sx) * zoom - 0.5f;
			u[x] = (int)floor(fu);

			//u[x]-1 >= 0; 
			if (u[x] + 2 < 0) x_loop[0] = x + 1;
			if (u[x] + 1 < 0) x_loop[1] = x + 1;
			if (u[x] + 0 < 0) x_loop[2] = x + 1;
			if (u[x] - 1 < 0) x_loop[3] = x + 1;
			//u[x]+2 < g_image_width;
			if (u[x] + 2 < g_image_width) x_loop[4] = x + 1;
			if (u[x] + 1 < g_image_width) x_loop[5] = x + 1;
			if (u[x] + 0 < g_image_width) x_loop[6] = x + 1;
			if (u[x] - 1 < g_image_width) x_loop[7] = x + 1;

			__m128 t1_sse = _mm_set1_ps(fu - u[x]);

			__m128 temp = c3_sse*t1_sse*t1_sse*t1_sse + c2_sse*t1_sse*t1_sse + c1_sse*t1_sse + c0_sse;

			wx_sse[x][0] = _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(3, 3, 3, 3));
			wx_sse[x][1] = _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(2, 2, 2, 2));
			wx_sse[x][2] = _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(1, 1, 1, 1));
			wx_sse[x][3] = _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(0, 0, 0, 0));
		}

		__m128* image_sse = g_image_data;
		__m128* temp_sse[4] = { g_temp_data + g_screen_width * 0, g_temp_data + g_screen_width * 1, g_temp_data + g_screen_width * 2, g_temp_data + g_screen_width * 3 };
		__m128* swap_sse[3];

		int v_last = INT_MIN;
		for (int y = 0; y<g_screen_height; ++y)
		{
			float fv = (y + sy) * zoom - 0.5f;
			int v = (int)floor(fv);

			switch (v - v_last)
			{
			case 0:
				break;
			case 1:
				swap_sse[0] = temp_sse[0];

				temp_sse[0] = temp_sse[1];
				temp_sse[1] = temp_sse[2];
				temp_sse[2] = temp_sse[3];
				temp_sse[3] = swap_sse[0];

				px_sse(v + 2, g_source, image_sse, temp_sse[3], u, wx_sse, x_loop);
				break;
			case 2:
				swap_sse[0] = temp_sse[0];
				swap_sse[1] = temp_sse[1];

				temp_sse[0] = temp_sse[2];
				temp_sse[1] = temp_sse[3];
				temp_sse[2] = swap_sse[0];
				temp_sse[3] = swap_sse[1];

				px_sse(v + 1, g_source, image_sse, temp_sse[2], u, wx_sse, x_loop);
				px_sse(v + 2, g_source, image_sse, temp_sse[3], u, wx_sse, x_loop);

				break;
			case 3:
				swap_sse[0] = temp_sse[0];
				swap_sse[1] = temp_sse[1];
				swap_sse[2] = temp_sse[2];

				temp_sse[0] = temp_sse[3];
				temp_sse[1] = swap_sse[0];
				temp_sse[2] = swap_sse[1];
				temp_sse[3] = swap_sse[2];

				px_sse(v + 0, g_source, image_sse, temp_sse[1], u, wx_sse, x_loop);
				px_sse(v + 1, g_source, image_sse, temp_sse[2], u, wx_sse, x_loop);
				px_sse(v + 2, g_source, image_sse, temp_sse[3], u, wx_sse, x_loop);
				break;
			default:
				px_sse(v - 1, g_source, image_sse, temp_sse[0], u, wx_sse, x_loop);
				px_sse(v + 0, g_source, image_sse, temp_sse[1], u, wx_sse, x_loop);
				px_sse(v + 1, g_source, image_sse, temp_sse[2], u, wx_sse, x_loop);
				px_sse(v + 2, g_source, image_sse, temp_sse[3], u, wx_sse, x_loop);
				break;
			}

			v_last = v;

			__m128 t1_sse = _mm_set1_ps(fv - v);
			__m128 temp = c3_sse*t1_sse*t1_sse*t1_sse + c2_sse*t1_sse*t1_sse + c1_sse*t1_sse + c0_sse;

			__m128 wy0 = _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(3, 3, 3, 3));
			__m128 wy1 = _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(2, 2, 2, 2));
			__m128 wy2 = _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(1, 1, 1, 1));
			__m128 wy3 = _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(0, 0, 0, 0));

			__m128* src0 = temp_sse[0];
			__m128* src1 = temp_sse[1];
			__m128* src2 = temp_sse[2];
			__m128* src3 = temp_sse[3];
			__m128i* dest = (__m128i*)(g_screen + g_screen_width*y);
			BGRA* destTemp = g_screen + g_screen_width*y;

			for (int x = 0; x<g_screen_width / 4; ++x)
			{
				int prefetch = 8;
				_mm_prefetch((char*)(src0 + prefetch), _MM_HINT_NTA);
				_mm_prefetch((char*)(src1 + prefetch), _MM_HINT_NTA);
				_mm_prefetch((char*)(src2 + prefetch), _MM_HINT_NTA);
				_mm_prefetch((char*)(src3 + prefetch), _MM_HINT_NTA);

				_mm_stream_si128(
					dest++,
					_mm_packus_epi16(
					_mm_packs_epi32(
					_mm_cvtps_epi32(_mm_load_ps((const float*)(src0 + 0))*wy0 + _mm_load_ps((const float*)(src1 + 0))*wy1 + _mm_load_ps((const float*)(src2 + 0))*wy2 + _mm_load_ps((const float*)(src3 + 0))*wy3),
					_mm_cvtps_epi32(_mm_load_ps((const float*)(src0 + 1))*wy0 + _mm_load_ps((const float*)(src1 + 1))*wy1 + _mm_load_ps((const float*)(src2 + 1))*wy2 + _mm_load_ps((const float*)(src3 + 1))*wy3)
					),
					_mm_packs_epi32(
					_mm_cvtps_epi32(_mm_load_ps((const float*)(src0 + 2))*wy0 + _mm_load_ps((const float*)(src1 + 2))*wy1 + _mm_load_ps((const float*)(src2 + 2))*wy2 + _mm_load_ps((const float*)(src3 + 2))*wy3),
					_mm_cvtps_epi32(_mm_load_ps((const float*)(src0 + 3))*wy0 + _mm_load_ps((const float*)(src1 + 3))*wy1 + _mm_load_ps((const float*)(src2 + 3))*wy2 + _mm_load_ps((const float*)(src3 + 3))*wy3)
					)
					)
					);
				src0 += 4;
				src1 += 4;
				src2 += 4;
				src3 += 4;
			}
		}
	}
}


/*---------------------------------------------------------------------------*/
int GetMSB(DWORD_PTR dwordPtr)
{
	if (dwordPtr)
	{
		int result = 1;
#if defined(_WIN64)
		if (dwordPtr & 0xFFFFFFFF00000000) { result += 32; dwordPtr &= 0xFFFFFFFF00000000; }
		if (dwordPtr & 0xFFFF0000FFFF0000) { result += 16; dwordPtr &= 0xFFFF0000FFFF0000; }
		if (dwordPtr & 0xFF00FF00FF00FF00) { result += 8;  dwordPtr &= 0xFF00FF00FF00FF00; }
		if (dwordPtr & 0xF0F0F0F0F0F0F0F0) { result += 4;  dwordPtr &= 0xF0F0F0F0F0F0F0F0; }
		if (dwordPtr & 0xCCCCCCCCCCCCCCCC) { result += 2;  dwordPtr &= 0xCCCCCCCCCCCCCCCC; }
		if (dwordPtr & 0xAAAAAAAAAAAAAAAA) { result += 1; }
#else
		if (dwordPtr & 0xFFFF0000) { result += 16; dwordPtr &= 0xFFFF0000; }
		if (dwordPtr & 0xFF00FF00) { result += 8;  dwordPtr &= 0xFF00FF00; }
		if (dwordPtr & 0xF0F0F0F0) { result += 4;  dwordPtr &= 0xF0F0F0F0; }
		if (dwordPtr & 0xCCCCCCCC) { result += 2;  dwordPtr &= 0xCCCCCCCC; }
		if (dwordPtr & 0xAAAAAAAA) { result += 1; }
#endif
		return result;
	}
	else
	{
		return 0;
	}
}

/*---------------------------------------------------------------------------*/
int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPTSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	DWORD_PTR processMask, systemMask;
	GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
	SetProcessAffinityMask(GetCurrentProcess(), 1 << (GetMSB(processMask) - 1));

	g_bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	g_bitmapInfo.bmiHeader.biWidth = g_screen_width;
	g_bitmapInfo.bmiHeader.biHeight = -g_screen_height;
	g_bitmapInfo.bmiHeader.biPlanes = 1;
	g_bitmapInfo.bmiHeader.biBitCount = 32;
	g_bitmapInfo.bmiHeader.biCompression = BI_RGB;
	g_bitmapInfo.bmiHeader.biSizeImage = 0;
	g_bitmapInfo.bmiHeader.biXPelsPerMeter = 100;
	g_bitmapInfo.bmiHeader.biYPelsPerMeter = 100;
	g_bitmapInfo.bmiHeader.biClrUsed = 0;
	g_bitmapInfo.bmiHeader.biClrImportant = 0;

	CoInitialize(NULL);

	// Init DirectX11 
	UINT flags = D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL FeatureLevel[] = { D3D_FEATURE_LEVEL_11_0, };

	D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, FeatureLevel, 1, D3D11_SDK_VERSION, &g_device, NULL, &g_context);

	// Create Shader
	std::vector<BYTE> bruteforce = LoadShader(_T("bruteforce.cso"));
	g_device->CreateComputeShader(&bruteforce[0], bruteforce.size(), NULL, &g_BruteForceCS);
	
	std::vector<BYTE> optimized = LoadShader(_T("optimized.cso"));
	g_device->CreateComputeShader(&optimized[0], optimized.size(), NULL, &g_OptimizedCS);

	// Create Const buffer
	float sx = ox + (g_image_width / zoom) / 2;
	float sy = oy + (g_image_height / zoom) / 2;
	float constants[4] = { sx, sy, zoom, 0 };
	CD3D11_BUFFER_DESC constDesc(sizeof(float)* 4, D3D11_BIND_CONSTANT_BUFFER);
	D3D11_SUBRESOURCE_DATA constData{ constants, 0, 0 };
	g_device->CreateBuffer(&constDesc, &constData, &g_constBuffer);

	ID3D11Buffer* csConsts[1] = { g_constBuffer };
	g_context->CSSetConstantBuffers(0, 1, csConsts);

	// Create Source
	std::vector<BYTE> source = LoadImage(L"test.jpg", &g_image_width, &g_image_height);
	g_source = (BGRA*)_aligned_malloc(sizeof(BGRA)* g_image_width*g_image_height, 16);
	memcpy(g_source, &source[0], sizeof(BGRA)*g_image_width*g_image_height);

	g_image_data = (__m128*)_aligned_malloc(sizeof(__m128) * g_image_width, 16);

	CD3D11_TEXTURE2D_DESC descSourceBuffer(DXGI_FORMAT_R8G8B8A8_UNORM, g_image_width, g_image_height, 1, 1, D3D11_BIND_SHADER_RESOURCE);
	D3D11_SUBRESOURCE_DATA sourceData{ &source[0], g_image_width * 4, 0 };
	g_device->CreateTexture2D(&descSourceBuffer, &sourceData, &g_sourceBuffer);

	CD3D11_SHADER_RESOURCE_VIEW_DESC descSourceView(D3D_SRV_DIMENSION_TEXTURE2D);
	g_device->CreateShaderResourceView(g_sourceBuffer, &descSourceView, &g_sourceView);

	ID3D11ShaderResourceView* srViews[] = { g_sourceView };
	UINT srCounts[] = { -1 };
	g_context->CSSetShaderResources(0, _countof(srCounts), srViews);

	// Create Target
	CD3D11_TEXTURE2D_DESC descTargetBuffer(DXGI_FORMAT_R8G8B8A8_UNORM, g_screen_width, g_screen_height, 1, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
	g_device->CreateTexture2D(&descTargetBuffer, NULL, &g_targetBuffer);

	CD3D11_UNORDERED_ACCESS_VIEW_DESC deescTargetView(g_targetBuffer, D3D11_UAV_DIMENSION_TEXTURE2D);
	g_device->CreateUnorderedAccessView(g_targetBuffer, &deescTargetView, &g_targetView);

	ID3D11UnorderedAccessView* uoViews[] = { g_targetView };
	UINT uoCounts[2] = { -1 };
	g_context->CSSetUnorderedAccessViews(0, _countof(uoViews), uoViews, uoCounts);

	// Create Copy
	CD3D11_TEXTURE2D_DESC descCopyByffer(DXGI_FORMAT_R8G8B8A8_UNORM, g_screen_width, g_screen_height, 1, 1, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
	g_device->CreateTexture2D(&descCopyByffer, NULL, &g_copyBuffer);

	// Create Query
	CD3D11_QUERY_DESC queryDesc(D3D11_QUERY_EVENT);
	g_device->CreateQuery(&queryDesc, &g_query);

	// Create Window
	TCHAR szTitle[100];
	TCHAR szWindowClass[100];
	LoadString(hInstance, IDS_APP_TITLE, szTitle, 100);
	LoadString(hInstance, IDC_BICUBICCSTEST, szWindowClass, 100);

	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BICUBICCSTEST));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDC_BICUBICCSTEST);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	RegisterClassEx(&wcex);

	RECT rect = { 0, 0, 1024, 1024 };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);

	HWND hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, hInstance, NULL);

	if (!hWnd)
	{
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	MSG msg = { 0, };
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			switch (g_resizeType)
			{
			case ResizeType_BruteForce:
			case ResizeType_Optimized:
			case ResizeType_OptimizedSSE:
				ResizeCPU(hWnd, g_resizeType);
				break;
			case ResizeType_BruteForceCS:
			case ResizeType_OptimizedCS:
				ResizeCS(hWnd, g_resizeType);
				break;
			}
		}
	}


	g_query = NULL;
	g_context = NULL;
	g_device = NULL;

	return (int)msg.wParam;
}

/*---------------------------------------------------------------------------*/
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;

	static int mx = 0;
	static int my = 0;

	switch (message)
	{
	case WM_CREATE:
		QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
		rdStart = __rdtsc();
		QueryPerformanceCounter((LARGE_INTEGER*)&start);
		Sleep(100);
		rdEnd = __rdtsc();
		QueryPerformanceCounter((LARGE_INTEGER*)&end);
		rdClock = (rdEnd - rdStart) * freq / ((end - start) * 1000);
		break;

	case WM_COMMAND:
		if (HIWORD(wParam) == 0)
		{
			switch (LOWORD(wParam))
			{
			case ID_MODE_BRUTEFORCECPU:
				g_resizeType = ResizeType_BruteForce;
				break;
			case ID_MODE_OPTIMIZEDCPU:
				g_resizeType = ResizeType_Optimized;
				break;
			case ID_MODE_OPTIMIZEDSSECPU:
				g_resizeType = ResizeType_OptimizedSSE;
				break;
			case ID_MODE_BRUTEFORCECS:
				g_resizeType = ResizeType_BruteForceCS;
				break;
			case ID_MODE_OPTIMIZEDCS:
				g_resizeType = ResizeType_OptimizedCS;
				break;
			}
		}
		break;

	case WM_MOUSEMOVE:
		if (wParam & MK_LBUTTON)
		{
			ox -= LOWORD(lParam) - mx;
			oy -= HIWORD(lParam) - my;
		}
		mx = LOWORD(lParam);
		my = HIWORD(lParam);
		break;

	case WM_CHAR:
		if (wParam == '+')
		{
			zoom = min(1.0f, zoom / 1.1f);
		}
		else if (wParam == '-')
		{
			zoom = min(1.0f, zoom * 1.1f);
		}
		break;

	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

/*---------------------------------------------------------------------------*/
void ResizeCPU(HWND hWnd, ResizeType resizeType)
{
	switch (resizeType)
	{
	case ResizeType_BruteForce:
		rdStart = __rdtsc();
		ResizeBruteForce();
		rdEnd = __rdtsc();
		break;
	case ResizeType_Optimized:
		rdStart = __rdtsc();
		ResizeOptimized();
		rdEnd = __rdtsc();
		break;
	case ResizeType_OptimizedSSE:
		rdStart = __rdtsc();
		ResizeOptimized_sse();
		rdEnd = __rdtsc();
		break;
	}

	rdMin = min(rdMin, rdEnd - rdStart);

	TCHAR text[256];
	_stprintf_s(text, sizeof(text) / sizeof(text[0]), _T("bicubic test - %f ms , %f ms min"), (rdEnd - rdStart) / double(rdClock), rdMin / double(rdClock));
	SetWindowText(hWnd, text);

	HDC hdc = GetDC(hWnd);
	SetDIBitsToDevice(hdc, 0, 0, g_screen_width, g_screen_height, 0, 0, 0, g_screen_width, g_screen, &g_bitmapInfo, DIB_RGB_COLORS);
	ReleaseDC(hWnd, hdc);
}

/*---------------------------------------------------------------------------*/
void ResizeCS(HWND hWnd, ResizeType resizeType)
{
	float sx = ox + (g_image_width / zoom) / 2;
	float sy = oy + (g_image_height / zoom) / 2;
	float constants[4] = { sx, sy, zoom, 0 };

	switch (resizeType)
	{
	case ResizeType_BruteForceCS:
		g_context->End(g_query);
		while (g_context->GetData(g_query, NULL, 0, 0) == S_FALSE) {}
		rdStart = __rdtsc();
		g_context->CSSetShader(g_BruteForceCS, NULL, 0);
		g_context->UpdateSubresource(g_constBuffer, 0, NULL, constants, 0, 0);
		g_context->Dispatch(g_screen_width / g_thread_width, g_screen_height / g_thread_height, 1);
		g_context->End(g_query);
		while (g_context->GetData(g_query, NULL, 0, 0) == S_FALSE) {}
		rdEnd = __rdtsc();
		break;
	case ResizeType_OptimizedCS:
		g_context->End(g_query);
		while (g_context->GetData(g_query, NULL, 0, 0) == S_FALSE) {}
		rdStart = __rdtsc();
		g_context->CSSetShader(g_OptimizedCS, NULL, 0);
		g_context->UpdateSubresource(g_constBuffer, 0, NULL, constants, 0, 0);
		g_context->Dispatch(g_screen_width / g_thread_width, g_screen_height / g_thread_height, 1);
		g_context->End(g_query);
		while (g_context->GetData(g_query, NULL, 0, 0) == S_FALSE) {}
		rdEnd = __rdtsc();
		break;
	}

	rdMin = min(rdMin, rdEnd - rdStart);

	TCHAR text[256];
	_stprintf_s(text, sizeof(text) / sizeof(text[0]), _T("bicubic test - %f ms , %f ms min"), (rdEnd - rdStart) / double(rdClock), rdMin / double(rdClock));
	SetWindowText(hWnd, text);

	HDC hdc = GetDC(hWnd);
	g_context->CopyResource(g_copyBuffer, g_targetBuffer);
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	if (SUCCEEDED(g_context->Map(g_copyBuffer, 0, D3D11_MAP_READ, 0, &mappedResource)))
	{
		int a = SetDIBitsToDevice(hdc, 0, 0, g_screen_width, g_screen_height, 0, 0, 0, g_screen_height, mappedResource.pData, &g_bitmapInfo, DIB_RGB_COLORS);

		g_context->Unmap(g_copyBuffer, 0);
	}
	ReleaseDC(hWnd, hdc);
}

/*---------------------------------------------------------------------------*/
