#pragma once
#include "common.hlsl.h"

inline UINT iDivUp(UINT a, UINT b) {
	return a / b + ((a % b != 0));
}

namespace Utility {
	HRESULT CompileShaderFromFile( WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut, const D3D10_SHADER_MACRO* pMacro = NULL );
	VOID CheckBuffer(ID3D11Buffer* pBuffer);
	VOID CheckBuffer(ID3D11Texture2D* pBuffer, UINT iMipLevel, UINT iLayer);
	VOID UpdateRNG(curandGenerator_t prng, UINT numRandom, cudaGraphicsResource_t buffer);
};

namespace GridSorter {
	HRESULT Create(WCHAR* str, ID3D11Device* pd3dDevice, UINT gsize, UINT numPoints, BOOL isDrawDDA, curandGenerator_t prngGPU);
	HRESULT RecompileDDAShader(WCHAR* str, BOOL isDrawDDA);
	HRESULT BuildGrid(
		ID3D11DeviceContext* pImmediateContext, 
		ID3D11ShaderResourceView* pVB, 
		UINT numPoints, 
		ID3D11UnorderedAccessView* pVBUAV, bool bAdjust);
	VOID Destroy();
	BOOL IsInitialized();
	HRESULT ShowDDA(
		ID3D11DeviceContext* pImmediateContext
		);
	HRESULT RecreateResources(ID3D11Device* pd3dDevice, UINT gsize, UINT numPoints);
	BOOL SetInitialized(BOOL v);
	VOID GetBufBinData(FLOAT* pBin);
};

namespace DartThrower {
	HRESULT Create(WCHAR* str, ID3D11Device* pd3dDevice, UINT numPoints, FLOAT radius, UINT nLayers);
	VOID UpdateRNG();
	VOID UpdateColor();
	UINT DartThrow(ID3D11DeviceContext* pd3dImmediateContext, BOOL isAlways = FALSE, BOOL isDisplay = TRUE);
	VOID Destroy();
	UINT GetNumPoints(ID3D11DeviceContext* pd3dImmediateContext);
	ID3D11ShaderResourceView* GetSRVPos();
	ID3D11UnorderedAccessView* GetUAVPos();
	ID3D11ShaderResourceView* GetSRVColor();
	void Visualize(ID3D11DeviceContext* pd3dImmediateContext);
	ID3D11Buffer* GetStagingBuffer();
	curandGenerator_t GetRandomGen();
}