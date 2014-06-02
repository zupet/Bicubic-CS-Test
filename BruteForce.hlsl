Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);

static const uint g_screen_width = 1024;
static const uint g_screen_height = 1024;

static const uint g_thread_width = 16;
static const uint g_thread_height = 16;

cbuffer constants : register(b0)
{
	float sx;
	float sy;
	float zoom;
}

float4 p(int x, int y)
{
	return source[int2(x, y)];
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

float4 px(int x, int y, float dx)
{
	return w0(dx)*p(x - 1, y) + w1(dx)*p(x, y) + w2(dx)*p(x + 1, y) + w3(dx)*p(x + 2, y);
}

float4 pxy(int x, int y, float dx, float dy)
{
	return w0(dy)*px(x, y - 1, dx) + w1(dy)*px(x, y, dx) + w2(dy)*px(x, y + 1, dx) + w3(dy)*px(x, y + 2, dx);
}

[numthreads(g_thread_width, g_thread_height, 1)]
void main(
	uint3 groupID : SV_GroupID,						// 외부에서 Dispatch 한 그리드 번호
	uint3 groupThreadID : SV_GroupThreadID,			// numthreads 의 그리드 번호
	uint3 dispatchThreadID : SV_DispatchThreadID,	// Dispatch * numthreads 번호
	uint groupIndex : SV_GroupIndex					// numthreads 내의 순차 번호
	)
{
	int x = dispatchThreadID.x;
	int y = dispatchThreadID.y;

	int v = (int)floor((y + sy) * zoom);
	int u = (int)floor((x + sx) * zoom);

	float dv = ((y + sy) * zoom) - v;
	float du = ((x + sx) * zoom) - u;

	float4 result = pxy(u, v, du, dv);

	target[uint2(dispatchThreadID.x, dispatchThreadID.y)] = float4(result.r, result.g, result.b, result.a); //BGRA format
}