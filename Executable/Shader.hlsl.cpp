#include "distance.hlsl.h"

StructuredBuffer<float3>	texBufPos : register(t1);
Buffer<uint2> texBufStartEnd : register(t2);
Buffer<uint2> texBufIndex : register(t3);
Buffer<float4> texBufGradient : register(t4);
Buffer<float2> texBufTension : register(t5);
Buffer<uint> texBufDDA : register(t6);
Buffer<float2> texBufRandom : register(t7);

RWBuffer<uint> bufIndex : register(u0);
RWBuffer<uint> bufStartEnd : register(u1);
RWBuffer<float> bufDensity : register(u2);
RWBuffer<float> bufGradient : register(u3);
RWBuffer<float> bufTension : register(u4);
RWBuffer<uint> bufDDA : register(u5);
RWStructuredBuffer<float3>	bufPos : register(u6);
RWBuffer<uint> bufBin : register(u7);

cbuffer CB_CONST_FORALL		: register(c0)
{
	float4 f0;
	float4 f1;
	float4 f2;
	uint4 i0;
	uint4 i1;
}

uint2 PosToIdx(uint2 gpos, uint tid) {
	return uint2(tid, ((gpos.y << 16) | gpos.x));
}

[numthreads(BLOCK_SIZE1, 1, 1)]
void IndexCS( 
	uint3 DTid : SV_DispatchThreadID, 
	uint3 GTid : SV_GroupThreadID, 
	uint3 Gid : SV_GroupID 
	)
{
	if(DTid.x >= i0.x)
		return;

	float2 pos = texBufPos[DTid.x];
	uint2 ipos = (uint2)(pos * f0.z);
	uint2 idx = PosToIdx(ipos, DTid.x);

	bufIndex[DTid.x * 2 + 0] = idx.x;
	bufIndex[DTid.x * 2 + 1] = idx.y;	
}

uint2 DecodePos(uint cell) {
	return uint2(cell & 0xFFFF, cell >> 16);
}

[numthreads(BLOCK_SIZE1, 1, 1)]
void BuildGridCS( uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex )
{
	const unsigned int G_ID = DTid.x; // Grid ID to operate on
	if(G_ID >= i0.x)
		return;

	unsigned int G_ID_PREV = (G_ID == 0)? (i0.x) : (G_ID); G_ID_PREV--;

	unsigned int G_ID_NEXT = G_ID + 1; if (G_ID_NEXT == i0.x) { G_ID_NEXT = 0; };

	unsigned int cell = bufIndex[G_ID * 2 + 1];
	unsigned int cell_prev = bufIndex[G_ID_PREV * 2 + 1];
    unsigned int cell_next = bufIndex[G_ID_NEXT * 2 + 1];

	uint2 pos = DecodePos(cell);
	uint ip = (pos.y << i0.z) | pos.x;

	if (cell != cell_prev)
	{
		// I'm the start of a cell
		bufStartEnd[ip * 2] = G_ID;
	}
	if (cell != cell_next)
	{
		// I'm the end of a cell
		bufStartEnd[ip * 2 + 1] = G_ID + 1;
	}
}

float4 CalculateGradientAndDensity(float2 diff, float2 s_i, float2 s_k, float c_i, float c_k, float h_sq, inout float b) {
	float2 dis = grad_func(s_i - s_k, c_i, c_k);

	float distance = distance_func(diff, c_i, c_k);

	float density = h_sq * exp(-(distance * distance) * h_sq * 0.5f);

	b = density;

	return float4(dis, 1.0f, 1.0f) * density;
}

[numthreads(BLOCK_SIZE1, 1, 1)]
void DensityCS( uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex )
{
	const unsigned int P_ID = DTid.x;

	if(DTid.x >= i0.x)
		return;

	const float h_sq = rcp(f0.w);
	float3 P_position = texBufPos[P_ID];

	float4 gradient = 0.0f;
	float w = 0;

	// Calculate the density based on neighbors from the 8 adjacent cells + current cell
	int2 G_XY = (int2)(P_position.xy * f0.z);

	for (int Y = max(G_XY.y - 1, 0) ; Y <= min(G_XY.y + 1, i0.y - 1) ; Y++)
	{
		for (int X = max(G_XY.x - 1, 0) ; X <= min(G_XY.x + 1, i0.y - 1) ; X++)
		{
			unsigned int G_CELL = (Y << i0.z) | X;
            uint2 G_START_END = texBufStartEnd[G_CELL];
			for (unsigned int N_ID = G_START_END.x ; N_ID < G_START_END.y ; N_ID++)
			{
				uint nid = texBufIndex[N_ID].x;
				float3 N_position = texBufPos[nid];

				float b;
				float2 diff = diff_integrate(N_position.xy, P_position.xy, 1.0f);
				float4 gd = CalculateGradientAndDensity(diff, N_position.xy, P_position.xy, N_position.z, P_position.z, h_sq, b);
				gd.zw *= N_position.xy;
				gradient += gd;
				w += b;
#ifdef SHOW_DDA
				float2 dda = diff_func(diff, N_position.z, P_position.z);
				if(length(dda) > 0) {
					dda = dda - floor(dda + 0.5f);
					if(abs(dda.x) < f1.x && abs(dda.y) < f1.x) {
						int2 index = (int2)(dda * (DDA_SIZE / 2.0f / f1.x) + (DDA_SIZE / 2));
						InterlockedAdd(bufDDA[index.y * DDA_SIZE + index.x], 1);
					}
				}
#endif
			}
		}
	}

	gradient /= w;
	bufGradient[P_ID * 4] = gradient.x;
	bufGradient[P_ID * 4 + 1] = gradient.y;
	bufGradient[P_ID * 4 + 2] = gradient.z;
	bufGradient[P_ID * 4 + 3] = gradient.w;
}

float2 CalculateTension(float3 s_i, float3 s_k, float ih_sq) {
	float2 dg = (s_k.xy - s_i.xy) * 1e-6f;

	return (s_i.z - s_k.z) * (ih_sq * dg * exp(-dot(dg, dg) * ih_sq * 0.5f));
}

[numthreads(BLOCK_SIZE1, 1, 1)]
void TensionCS( uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex )
{
	const unsigned int P_ID = DTid.x;

	if(DTid.x >= i0.x)
		return;

	float2 P_gradient = texBufGradient[P_ID].zw;

	bufTension[P_ID * 2] = P_gradient.x;
	bufTension[P_ID * 2 + 1] = P_gradient.y;
}

#ifdef BIN_DENSITY
groupshared uint bin_shared[4];
#endif
[numthreads(BLOCK_SIZE1, 1, 1)]
void IntegrateCS( uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex )
{
	const unsigned int P_ID = DTid.x; // Particle ID to operate on
#ifdef BIN_DENSITY
	if(GTid.x < BIN_DENSITY) {
		bin_shared[GTid.x] = 0;
	}
#endif
	float3 position = 0;

	if(DTid.x < i0.x) {
		position = bufPos[P_ID];

		float4 g = texBufGradient[P_ID];
		float2 rando = texBufRandom[P_ID];

		float2 inc = 0;
#ifdef RANDOM_DISTURBING
		float2 rtheta = rando * float2(f2.x, 3.14159265359f * 2.0f);
		inc += float2(cos(rtheta.y), sin(rtheta.y)) * rtheta.x * f1.x;
#endif
#ifdef UNKNOWN_DENSITY
		inc += (g.zw - texBufTension[P_ID]) * 2e-2f;
#endif
		float2 velocity = g.xy + inc;

		position.xy -= velocity;

		position.x = min(BOX_RIGHT, max(BOX_LEFT, position.x));
		position.y = min(BOX_BOTTOM, max(BOX_TOP, position.y));

		// Update
		bufPos[P_ID] = position;
	}
#ifdef BIN_DENSITY
	GroupMemoryBarrierWithGroupSync();

	if(DTid.x < i0.x) {
		if(position.x > BOX_LEFT && position.x < BOX_RIGHT && position.y > BOX_TOP && position.y < BOX_BOTTOM) {
			InterlockedAdd(bin_shared[(uint)(position.x * (float)BIN_DENSITY)], 1);
		}
	}

	GroupMemoryBarrierWithGroupSync();

	if(GTid.x < BIN_DENSITY) {
		InterlockedAdd(bufBin[GTid.x], bin_shared[GTid.x]);
	}
#endif
}

float4 ShowDDAVS(float2 pos : POSITION) : SV_POSITION
{
	return float4(pos, 0.5f, 1.0f);
}


#define s2(a, b)				temp = a; a = min(a, b); b = max(temp, b);
#define mn3(a, b, c)			s2(a, b); s2(a, c);
#define mx3(a, b, c)			s2(b, c); s2(a, c);

#define mnmx3(a, b, c)			mx3(a, b, c); s2(a, b);                                   // 3 exchanges
#define mnmx4(a, b, c, d)		s2(a, b); s2(c, d); s2(a, c); s2(b, d);                   // 4 exchanges
#define mnmx5(a, b, c, d, e)	s2(a, b); s2(c, d); mn3(a, c, e); mx3(b, d, e);           // 6 exchanges
#define mnmx6(a, b, c, d, e, f) s2(a, d); s2(b, e); s2(c, f); mn3(a, b, c); mx3(d, e, f); // 7 exchanges

float4 ShowDDAPS(float4 pos : SV_POSITION) : SV_Target
{
	if(pos.x < i1.x - DDA_SIZE || pos.y < i1.y - DDA_SIZE)
		discard;

	uint2 tex = pos - i1.xy + DDA_SIZE;

	uint idx = tex.y * DDA_SIZE + tex.x;
	if(idx >= DDA_SIZE * DDA_SIZE)
		idx -= DDA_SIZE * DDA_SIZE;

	uint v[6];
	v[0] = texBufDDA[idx - 1 - DDA_SIZE];
	v[1] = texBufDDA[idx - DDA_SIZE];
	v[2] = texBufDDA[idx + 1 - DDA_SIZE];
	v[3] = texBufDDA[idx - 1];
	v[4] = texBufDDA[idx];
	v[5] = texBufDDA[idx + 1];

	uint temp;
	mnmx6(v[0], v[1], v[2], v[3], v[4], v[5]);
	v[5] = texBufDDA[idx - 1 + DDA_SIZE];
	mnmx5(v[1], v[2], v[3], v[4], v[5]);
	v[5] = texBufDDA[idx + DDA_SIZE];
	mnmx4(v[2], v[3], v[4], v[5]);
	v[5] = texBufDDA[idx + 1 + DDA_SIZE];
	mnmx3(v[3], v[4], v[5]);

	return (float)v[4] / (float)i0.x * 17.32f;
}