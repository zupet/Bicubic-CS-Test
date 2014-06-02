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

groupshared float4 buffer2[g_thread_height + 4][g_thread_width];

float4 pxy_opt2(int v, int thread_x, int thread_y, float dv)
{
	return w0(dv) * buffer2[v][thread_x] + w1(dv) * buffer2[v + 1][thread_x] + w2(dv) * buffer2[v + 2][thread_x] + w3(dv) * buffer2[v + 3][thread_x];
}

[numthreads(g_thread_width, g_thread_height, 1)]
void main(
	uint3 groupID : SV_GroupID,						// 외부에서 Dispatch 한 그리드 번호
	uint3 groupThreadID : SV_GroupThreadID,			// numthreads 의 그리드 번호
	uint3 dispatchThreadID : SV_DispatchThreadID,	// Dispatch * numthreads 번호
	uint groupIndex : SV_GroupIndex					// numthreads 내의 순차 번호
	)
{
	int group_x = groupID.x;
	int group_y = groupID.y;
	int thread_x = groupThreadID.x;
	int thread_y = groupThreadID.y;
	int x = dispatchThreadID.x;
	int y = dispatchThreadID.y;

	float fu = (x + sx) * zoom;
	float fv = (y + sy) * zoom;
	int u = (int)floor(fu);
	int v = (int)floor(fv);
	float du = fu - u;
	float dv = fv - v;

	int v_first = (int)floor((group_y * g_thread_height + sy) * zoom);
	int v_last = (int)floor((group_y * g_thread_height + g_thread_height - 1 + sy) * zoom);

	int buffer_y = thread_y;
	for (int pv = v_first - 1 + thread_y; pv <= v_last + 3; pv += g_thread_height)
	{
		buffer2[buffer_y][thread_x] = px(u, pv, du);

		buffer_y += g_thread_height;
	}

	GroupMemoryBarrierWithGroupSync();

	float4 result = pxy_opt2(v - v_first, thread_x, thread_y, dv);

	target[uint2(dispatchThreadID.x, dispatchThreadID.y)] = float4(result.r, result.g, result.b, result.a); //BGRA format
}