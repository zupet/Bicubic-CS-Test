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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float4 resize_nearest(int x, int y)
{
	int u, v;
	float fu = (x + sx) * zoom;
	float fv = (y + sy) * zoom;
	float du = modf(fu, u);
	float dv = modf(fv, v);

	return source[uint2(u, v)];
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float4 resize_linear(int x, int y)
{
	int u, v;
	float fu = (x + sx) * zoom;
	float fv = (y + sy) * zoom;
	float du = modf(fu, u);
	float dv = modf(fv, v);

	return lerp(lerp(source[uint2(u, v + 0)], source[uint2(u + 1, v + 0)], du), lerp(source[uint2(u, v + 1)], source[uint2(u + 1, v + 1)], du), dv);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

float4 resize(int x, int y)
{
	int v = (int)floor((y + sy) * zoom);
	int u = (int)floor((x + sx) * zoom);

	float dv = ((y + sy) * zoom) - v;
	float du = ((x + sx) * zoom) - u;

	return pxy(u, v, du, dv);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
groupshared float4 buffer[g_thread_height + 3][g_thread_width + 3];

float4 px_opt(int u, int v, int thread_x, float du)
{
	return w0(du) * buffer[v][u] + w1(du) * buffer[v][u + 1] + w2(du) * buffer[v][u + 2] + w3(du) * buffer[v][u + 3];
}

float4 pxy_opt(int u, int v, int thread_x, int thread_y, float du, float dv)
{
	return w0(dv) * px_opt(u, v, thread_x, du) + w1(dv) * px_opt(u, v + 1, thread_x, du) + w2(dv) * px_opt(u, v + 2, thread_x, du) + w3(dv) * px_opt(u, v + 3, thread_x, du);
}

float4 resize_opt(int group_x, int group_y, int thread_x, int thread_y, int x, int y)
{
	float fu = (x + sx) * zoom;
	float fv = (y + sy) * zoom;
	int u = (int)floor(fu);
	int v = (int)floor(fv);
	float du = fu - u;
	float dv = fv - v;

	int u_first = (int)floor((group_x * g_thread_width + sx) * zoom);
	int u_last = (int)floor((group_x * g_thread_width + g_thread_width - 1 + sx) * zoom);

	int v_first = (int)floor((group_y * g_thread_height + sy) * zoom);
	int v_last = (int)floor((group_y * g_thread_height + g_thread_height - 1 + sy) * zoom);

	int buffer_y = thread_y;
	for (int py = v_first - 1 + thread_y; py <= v_last + 3; py += g_thread_height)
	{
		int buffer_x = thread_x;
		for (int px = u_first - 1 + thread_x; px <= u_last + 3; px += g_thread_width)
		{
			buffer[buffer_y][buffer_x] = source[uint2(px, py)];

			buffer_x += g_thread_width;
		}
		buffer_y += g_thread_height;
	}

	GroupMemoryBarrierWithGroupSync();

	return pxy_opt(u - u_first, v - v_first, thread_x, thread_y, du, dv);
	//return buffer[thread_y][thread_x];
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
groupshared float4 buffer2[g_thread_height + 4][g_thread_width];

float4 pxy_opt2(int v, int thread_x, int thread_y, float dv)
{
	return w0(dv) * buffer2[v][thread_x] + w1(dv) * buffer2[v + 1][thread_x] + w2(dv) * buffer2[v + 2][thread_x] + w3(dv) * buffer2[v + 3][thread_x];
}

float4 resize_opt2(int group_x, int group_y, int thread_x, int thread_y, int x, int y)
{
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

	return pxy_opt2(v - v_first, thread_x, thread_y, dv);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
[numthreads(g_thread_width, g_thread_height, 1)]
void main( 
	uint3 groupID : SV_GroupID,						// 외부에서 Dispatch 한 그리드 번호
	uint3 groupThreadID : SV_GroupThreadID,			// numthreads 의 그리드 번호
	uint3 dispatchThreadID : SV_DispatchThreadID,	// Dispatch * numthreads 번호
	uint groupIndex : SV_GroupIndex					// numthreads 내의 순차 번호
	)
{
	//float4 result = resize_nearest(dispatchThreadID.x, dispatchThreadID.y);
	//float4 result = resize_linear(dispatchThreadID.x, dispatchThreadID.y);
	//float4 result = resize(dispatchThreadID.x, dispatchThreadID.y);
	//float4 result = resize_opt(groupID.x, groupID.y, groupThreadID.x, groupThreadID.y, dispatchThreadID.x, dispatchThreadID.y);
	float4 result = resize_opt2(groupID.x, groupID.y, groupThreadID.x, groupThreadID.y, dispatchThreadID.x, dispatchThreadID.y);
	target[uint2(dispatchThreadID.x, dispatchThreadID.y)] = float4(result.b, result.g, result.r, result.a);
}