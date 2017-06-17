#include "distance.hlsl.h"

Buffer<float> RandFloats						: register(t0);
StructuredBuffer<float3>	texBufPos			: register(t1);

RWTexture2DArray<float> texIdxPoints			:	register(u0); 
RWTexture2DArray<float> texIdxPointsPrev		:	register(u1); 
AppendStructuredBuffer<float3> bufIdxPointsApd	:	register(u2);

cbuffer CB_CONST_FORALL		: register(b0)
{
	float4 f0;
	float4 f1;
	float4 f2;
	uint4 i0;
	uint4 i1;
}

struct VS_INPUT {
	uint proxy	:	POSITION;
	uint id		:	SV_VertexID;
};

struct GS_INPUT {
	float2 pos		:	TEXCOORD0;
	float2 posori	: TEXCOORD1;
	float color	:	TEXCOORD2;
};

struct GS_OUTPUT {
	float4 pos : SV_POSITION;
	float color : TEXCOORD0;
	float2 posori : TEXCOORD1;
	float2 posref : TEXCOORD2;
};

cbuffer CB_CONST_RAND			: register(b0)
{
	uint4 iParam0;		//[iBlockSize, iSelPix, GridSizeX, iRandomBase]
	uint4 iParam1;		//[nLayer, iLayer, 0, 0]
	float4 fParam0;		//[Radius, 0, 0, 0]
}


GS_INPUT PointVS( VS_INPUT input )
{
	GS_INPUT o;
	float3 p = texBufPos[input.id];
	o.posori = p.xy;
	o.pos = p.xy * 2.0f - 1.0f;
	o.pos.y = -o.pos.y;
	o.color = p.z;

	return o;
};

const static float2 quadPos[] = 
{
	{0, 2.0f},
	{1.73205081f, -1.0f},
	{-1.73205081f, -1.0f},
};

[maxvertexcount(3)]
void VisualizeGS(point GS_INPUT input[1],
	inout TriangleStream<GS_OUTPUT> ParticleOutputStream)
{
	GS_OUTPUT o;
	o.color =  input[0].color;
	o.pos.zw = float2(0.5f, 1.0f);
	o.posori = input[0].posori;

	[unroll(3)] 
	for(int i = 0; i < 3; i++) {
		o.posref = quadPos[i];
		o.pos.xy = (input[0].pos * f1.y + o.posref * f0.xy
			* f1.z);
		ParticleOutputStream.Append(o);
	}

	ParticleOutputStream.RestartStrip();
}


void h2rgb( float h, out float r, out float g, out float b ) {
	float H, F, N, K;
	int   I;

	H = h;  /* Hue */

	if (H >= 1.0) {
		H = 0.99999f * 6.0f;
	} else {
		H = H * 6.0f;
	} /* end if */
	I = (int) H;
	F = H - I;

	N = (1 - F);
	K = F;

	if (I == 0) { r = 1.0f; g = K; b = 0; }
	else if (I == 1) { r = N; g = 1.0f; b = 0; }
	else if (I == 2) { r = 0; g = 1.0f; b = K; }
	else if (I == 3) { r = 0; g = N; b = 1.0f; }
	else if (I == 4) { r = K; g = 0; b = 1.0f; }
	else if (I == 5) { r = 1.0f; g = 0; b = N; }
}

struct PPS_OUT {
	float4 color : SV_Target;
	float d : SV_Depth;
};

PPS_OUT PointPS( GS_OUTPUT input )
{
	clip(1.0f - length(input.posref));

	PPS_OUT o;
#ifdef FUZZY_BLUE_NOISE
	float r, g, b;
	h2rgb(input.color * 0.666f, r, g, b);
	o.color = float4(r, g, b, 1.0f ); 

#ifdef SCROLL_VALUE
	if(f1.w < 1.0f) {
		float w = similarity(input.color, f1.w, 0.03);
		clip(w - 0.5f);
	}
#endif
#else
	o.color = 0;
#endif
#ifdef SHOW_BORDER
	if(input.posori.x == BOX_LEFT || input.posori.x == BOX_RIGHT || input.posori.y == BOX_BOTTOM || input.posori.y == BOX_TOP)
		o.color = float4(0, 0, 1, 0);
#endif
	o.d = 1.0f - input.color;
	return o;
}

const static int2 invest_pix[] = {
	{0, 0},
	{1, 0},
	{0, 1},
	{1, 1},
	{0, 2},
	{2, 1},
	{1, 2},
	{2, 0},
	{2, 2}
};

const static int2 invest_pix2[] = {
	{-1, -1},
	{-1, 0},
	{-1, 1},
	{0, -1},
	{0, 1},
	{1, -1},
	{1, 0},
	{1, 1},
// 	{-2, -2},
// 	{-2, -1},
// 	{-2, 0},
// 	{-2, 1},
// 	{-2, 2},
// 	{-1, -2},
// 	{-1, 2},
// 	{0, -2},
// 	{0, 2},
// 	{1, -2},
// 	{1, 2},
// 	{2, -2},
// 	{2, -1},
// 	{2, 0},
// 	{2, 1},
// 	{2, 2},
	{0, 0}
};

[numthreads(BLOCK_SIZE0, BLOCK_SIZE0, 1)]
void DartThrowCS(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	int2 pos_base = DTid.xy * 2;

	if( DTid.x >= iParam0.z || DTid.y >= iParam0.z ) {
		return;
	}

	const uint idxRandBase = iParam0.w + (DTid.y * iParam0.z + DTid.x) * iParam0.x;

 	int2 pos = pos_base + invest_pix[iParam0.y];

	float2 rando_pos = float2(texIdxPoints[uint3(pos, Gid.z * 3 + 0)], texIdxPoints[uint3(pos, Gid.z * 3 + 1)]);
	float rando_color = texIdxPoints[uint3(pos, Gid.z * 3 + 2)];

	if(rando_pos.x < 0 || rando_pos.y < 0) {
		uint idxRand = idxRandBase + iParam0.y;
		uint base_t = (idxRand * iParam1.x + Gid.z) * 3;
		float2 rando = float2(RandFloats[base_t + 0], RandFloats[base_t + 1]);
		rando_pos = (float2)pos + rando;
		rando_color = RandFloats[base_t + 2];
	}

	uint dx, dy, dz;
	texIdxPoints.GetDimensions(dx, dy, dz);
	float scal = rcp((float)dx);

	bool isAvail = true;
	for(uint j = 0; j < 8; j++) {
		int2 pos_N = pos + invest_pix2[j];
		float2 rando_pos_N = float2(texIdxPoints[uint3(pos_N, Gid.z * 3 + 0)], texIdxPoints[uint3(pos_N, Gid.z * 3 + 1)]);
		float rando_color_N = texIdxPoints[uint3(pos_N, Gid.z * 3 + 2)];

		if(rando_pos_N.x >= 0 && rando_pos_N.y >= 0) {
			float2 diff = diff_integrate(rando_pos, rando_pos_N, scal);
			isAvail = isAvail & (distance_func(diff, rando_color, rando_color_N) > fParam0.x);
		}
	}

	if(isAvail) {
		texIdxPoints[uint3(pos, Gid.z * 3 + 0)] = rando_pos.x;
		texIdxPoints[uint3(pos, Gid.z * 3 + 1)] = rando_pos.y;
		texIdxPoints[uint3(pos, Gid.z * 3 + 2)] = rando_color;
	}
}

[numthreads(BLOCK_SIZE0, BLOCK_SIZE0, 1)]
void DartThrowCSFinal(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	int2 pos_base = DTid.xy * 3;

	if( DTid.x >= iParam0.z || DTid.y >= iParam0.z ) {
		return;
	}

	const uint idxRandBase = iParam0.w + (DTid.y * iParam0.z + DTid.x) * iParam0.x;

	int2 pos = pos_base + invest_pix[iParam0.y];

	bool isAvail = true;

	float2 rando_pos = float2(texIdxPoints[uint3(pos, Gid.z * 3 + 0)], texIdxPoints[uint3(pos, Gid.z * 3 + 1)]);
	float rando_color = texIdxPoints[uint3(pos, Gid.z * 3 + 2)];

	if(rando_pos.x < 0 || rando_pos.y < 0) {
		uint idxRand = idxRandBase + iParam0.y;
		uint base_t = (idxRand * iParam1.x + Gid.z) * 3;
		float2 rando = float2(RandFloats[base_t + 0], RandFloats[base_t + 1]);
		rando_pos = (float2)pos + rando;
		rando_color = RandFloats[base_t + 2];
	}

	uint dx, dy, dz;
	texIdxPoints.GetDimensions(dx, dy, dz);
	float scal = rcp((float)dx);

	for(uint j = 0; j < 8; j++) {
		int2 pos_N = pos + invest_pix2[j];
		float2 rando_pos_N = float2(texIdxPoints[uint3(pos_N, Gid.z * 3 + 0)], texIdxPoints[uint3(pos_N, Gid.z * 3 + 1)]);
		float rando_color_N = texIdxPoints[uint3(pos_N, Gid.z * 3 + 2)];

		if(rando_pos_N.x >= 0 && rando_pos_N.y >= 0) {
			float2 diff = diff_integrate(rando_pos, rando_pos_N, scal);
			isAvail = isAvail & (distance_func(diff, rando_color, rando_color_N) > fParam0.x);
		}
	}

	if(isAvail) {
		texIdxPoints[uint3(pos, Gid.z * 3 + 0)] = rando_pos.x;
		texIdxPoints[uint3(pos, Gid.z * 3 + 1)] = rando_pos.y;
		texIdxPoints[uint3(pos, Gid.z * 3 + 2)] = rando_color;

		if(Gid.z == 0) {
			uint dx, dy, dz;
			texIdxPoints.GetDimensions(dx, dy, dz);
			bufIdxPointsApd.Append(float3(rando_pos / (float)dx * float2(BOX_LEN_X_DT, BOX_LEN_Y_DT) + float2(BOX_LEFT_DT, BOX_TOP_DT), rando_color));
		}
	}
}

[numthreads(BLOCK_SIZE0, BLOCK_SIZE0, 1)]
void DartThrowCSCombine(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	if( DTid.x >= iParam0.z || DTid.y >= iParam0.z ) {
		return;
	}

	int2 pos = DTid.xy;

	float2 rando_pos = float2(texIdxPoints[uint3(pos, iParam1.y * 3 + 0)], texIdxPoints[uint3(pos, iParam1.y * 3 + 1)]);
	float rando_color = texIdxPoints[uint3(pos, iParam1.y * 3 + 2)];

	if(rando_pos.x < 0 || rando_pos.y < 0)
		return;

	bool isAvail = true;
	uint dx, dy, dz;
	texIdxPoints.GetDimensions(dx, dy, dz);
	float scal = rcp((float)dx);

	[allow_uav_condition]
	for(uint i = 0; i < iParam1.y; i++) {
		for(uint j = 0; j < 9; j++) {
			int2 pos_N = pos + invest_pix2[j];
			float2 rando_pos_N = float2(texIdxPoints[uint3(pos_N, i * 3 + 0)], texIdxPoints[uint3(pos_N, i * 3 + 1)]);
			float rando_color_N = texIdxPoints[uint3(pos_N, i * 3 + 2)];

			if(rando_pos_N.x >= 0 && rando_pos_N.y >= 0) {
				float2 diff = diff_integrate(rando_pos, rando_pos_N, scal);
				isAvail = isAvail & (distance_func(diff, rando_color, rando_color_N) > fParam0.x);
			}
		}
	}

	if(isAvail) {
		bufIdxPointsApd.Append(float3(rando_pos / (float)dx * float2(BOX_LEN_X_DT, BOX_LEN_Y_DT) + float2(BOX_LEFT_DT, BOX_TOP_DT), rando_color));
	} else {
		texIdxPoints[uint3(pos, iParam1.y * 3 + 0)] = -1.0f;
		texIdxPoints[uint3(pos, iParam1.y * 3 + 1)] = -1.0f;
	}
}

[numthreads(BLOCK_SIZE0, BLOCK_SIZE0, 1)]
void MipMappingCS(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	int2 pos_base_src = DTid.xy;

	if( pos_base_src.x >= iParam0.z * 2 || pos_base_src.y >= iParam0.z * 2 ) {
		return;
	}

	int2 pos_base_dest = DTid.xy * 2;

	float2 rando_pos = float2(texIdxPointsPrev[uint3(pos_base_src, Gid.z * 3 + 0)], texIdxPointsPrev[uint3(pos_base_src, Gid.z * 3 + 1)]);

	if(rando_pos.x >= 0 && rando_pos.y >= 0) {
		float rando_color = texIdxPointsPrev[uint3(pos_base_src, Gid.z * 3 + 2)];
		uint dx_prev, dy_prev, dz_prev, dx, dy, dz;
		texIdxPointsPrev.GetDimensions(dx_prev, dy_prev, dz_prev);
		texIdxPoints.GetDimensions(dx, dy, dz);
		float2 rando_target = rando_pos * (float)dx / (float)dx_prev;
		int2 offset = (int2)(rando_target - (float2)pos_base_dest);
		pos_base_dest += offset;

		texIdxPoints[uint3(pos_base_dest, Gid.z * 3 + 0)] = rando_target.x;
		texIdxPoints[uint3(pos_base_dest, Gid.z * 3 + 1)] = rando_target.y;
		texIdxPoints[uint3(pos_base_dest, Gid.z * 3 + 2)] = rando_color;
	}
}
