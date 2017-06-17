#include "DXUT.h"
#include "GPURelaxer.h"

namespace GridSorter {
const static WCHAR* szShader = L"Shader.hlsl.cpp";

static ID3D11Buffer*	g_pVBIndex = NULL;
static ID3D11Buffer* g_pGBStartEnd = NULL;
static ID3D11Buffer* g_pVBDensity = NULL;
static ID3D11Buffer* g_pVBGradient = NULL;
static ID3D11Buffer* g_pVBTension = NULL;
static ID3D11Buffer* g_pBufRandom = NULL;
static ID3D11Buffer* g_pBufBin = NULL;
static ID3D11UnorderedAccessView* g_pUAVBin = NULL;
static ID3D11Buffer* g_pBufBinStg = NULL;
static ID3D11ShaderResourceView* g_pSRVRandom = NULL;
static cudaGraphicsResource_t g_pGRRandom = NULL;
static ID3D11UnorderedAccessView* g_pUAVIndex = NULL;
static ID3D11UnorderedAccessView* g_pUAVStartEnd = NULL;
static ID3D11UnorderedAccessView* g_pUAVGradient = NULL;
static ID3D11UnorderedAccessView* g_pUAVDensity = NULL;
static ID3D11UnorderedAccessView* g_pUAVTension = NULL;
static ID3D11ComputeShader* g_pCSComputeIndex = NULL;
static ID3D11ComputeShader* g_pCSBuildGrid = NULL;
static ID3D11ComputeShader* g_pCSDensity = NULL;
static ID3D11ComputeShader* g_pCSIntegrate = NULL;
static ID3D11ComputeShader* g_pCSTension = NULL;
static ID3D11ShaderResourceView* g_pSRVPoints = NULL;
static ID3D11ShaderResourceView* g_pSRVStartEnd = NULL;
static ID3D11ShaderResourceView* g_pSRVIndex = NULL;
static ID3D11ShaderResourceView* g_pSRVGradient = NULL;
static ID3D11ShaderResourceView* g_pSRVTension = NULL;
static cudaGraphicsResource* g_pGRIndex = NULL;
static ID3D11Texture2D* g_pTexDDA = NULL;
static ID3D11ShaderResourceView* g_pSRVDDA = NULL;
static ID3D11RenderTargetView* g_pRTVDDA = NULL;
static ID3D11Buffer* g_pBufferDDA = NULL;
static ID3D11ShaderResourceView* g_pSRVBufferDDA = NULL;
static ID3D11UnorderedAccessView* g_pUAVBufferDDA = NULL;
static ID3D11Buffer* g_pVBQuad = NULL;
static ID3D11InputLayout* g_pILQuad = NULL;
static ID3D11VertexShader* g_pVSQuad = NULL;
static ID3D11PixelShader* g_pPSQuad = NULL;
static curandGenerator_t g_prngGPU = NULL;
static BOOL g_bInit = FALSE;
static BOOL g_bCreated = FALSE;

#define BLOCK_SIZE 128

HRESULT RecreateResources(ID3D11Device* pd3dDevice, UINT gsize, UINT numPoints) {
	HRESULT hr;

	cudaGraphicsUnregisterResource(g_pGRIndex);
	cudaGraphicsUnregisterResource(g_pGRRandom);
	g_pGRIndex = NULL;
	g_pGRRandom = NULL;
	SAFE_RELEASE(g_pVBIndex);
	SAFE_RELEASE(g_pGBStartEnd);
	SAFE_RELEASE(g_pUAVIndex);
	SAFE_RELEASE(g_pUAVStartEnd);
	SAFE_RELEASE(g_pUAVDensity);
	SAFE_RELEASE(g_pUAVGradient);
	SAFE_RELEASE(g_pUAVTension);
	SAFE_RELEASE(g_pVBDensity);
	SAFE_RELEASE(g_pVBGradient);
	SAFE_RELEASE(g_pVBTension);
	SAFE_RELEASE(g_pSRVStartEnd);
	SAFE_RELEASE(g_pSRVIndex);
	SAFE_RELEASE(g_pSRVGradient);
	SAFE_RELEASE(g_pSRVTension);
	SAFE_RELEASE(g_pSRVPoints);
	SAFE_RELEASE(g_pSRVRandom);
	SAFE_RELEASE(g_pBufRandom);


	D3D11_BUFFER_DESC bufdesc;
	bufdesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bufdesc.ByteWidth = sizeof(double) * numPoints;
	bufdesc.CPUAccessFlags = 0;
	bufdesc.MiscFlags = 0;
	bufdesc.StructureByteStride = 0;
	bufdesc.Usage = D3D11_USAGE_DEFAULT;

	V_RETURN(pd3dDevice->CreateBuffer(&bufdesc, NULL, &g_pVBIndex));
	V_RETURN(pd3dDevice->CreateBuffer(&bufdesc, NULL, &g_pVBTension));

	bufdesc.ByteWidth = sizeof(D3DXVECTOR4) * numPoints;
	V_RETURN(pd3dDevice->CreateBuffer(&bufdesc, NULL, &g_pVBGradient));

	bufdesc.ByteWidth = sizeof(float) * numPoints;
	V_RETURN(pd3dDevice->CreateBuffer(&bufdesc, NULL, &g_pVBDensity));

	bufdesc.ByteWidth = sizeof(UINT) * gsize * gsize * 2;
	V_RETURN(pd3dDevice->CreateBuffer(&bufdesc, NULL, &g_pGBStartEnd));

	bufdesc.ByteWidth = numPoints * sizeof(float) * 2;
	V_RETURN(pd3dDevice->CreateBuffer(&bufdesc, NULL, &g_pBufRandom));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvdesc;
	srvdesc.Buffer.FirstElement = 0;
	srvdesc.Buffer.NumElements = gsize * gsize;
	srvdesc.Format = DXGI_FORMAT_R32G32_UINT;
	srvdesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	V_RETURN(pd3dDevice->CreateShaderResourceView(g_pGBStartEnd, &srvdesc, &g_pSRVStartEnd));

	srvdesc.Buffer.NumElements = numPoints;
	srvdesc.Format = DXGI_FORMAT_R32G32_UINT;
	V_RETURN(pd3dDevice->CreateShaderResourceView(g_pVBIndex, &srvdesc, &g_pSRVIndex));

	srvdesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	V_RETURN(pd3dDevice->CreateShaderResourceView(g_pVBGradient, &srvdesc, &g_pSRVGradient));

	srvdesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	V_RETURN(pd3dDevice->CreateShaderResourceView(g_pVBTension, &srvdesc, &g_pSRVTension));

	V_RETURN(pd3dDevice->CreateShaderResourceView(g_pBufRandom, &srvdesc, &g_pSRVRandom));

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavdesc;
	uavdesc.Buffer.FirstElement = 0;
	uavdesc.Buffer.Flags = 0;
	uavdesc.Buffer.NumElements = numPoints * 2;
	uavdesc.Format = DXGI_FORMAT_R32_UINT;
	uavdesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

	V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pVBIndex, &uavdesc, &g_pUAVIndex));

	uavdesc.Format = DXGI_FORMAT_R32_FLOAT;
	uavdesc.Buffer.NumElements = numPoints * 4;
	V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pVBGradient, &uavdesc, &g_pUAVGradient));

	uavdesc.Buffer.NumElements = numPoints * 2;
	V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pVBTension, &uavdesc, &g_pUAVTension));

	uavdesc.Buffer.NumElements = numPoints;
	V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pVBDensity, &uavdesc, &g_pUAVDensity));

	uavdesc.Format = DXGI_FORMAT_R32_UINT;
	uavdesc.Buffer.NumElements = gsize * gsize * 2;
	V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pGBStartEnd, &uavdesc, &g_pUAVStartEnd));


	cudaGraphicsD3D11RegisterResource(&g_pGRIndex, g_pVBIndex, 0);
	cudaGraphicsD3D11RegisterResource(&g_pGRRandom, g_pBufRandom, 0);

	g_bCreated = TRUE;
	g_bInit = FALSE;
	return hr;
}

HRESULT RecompileDDAShader(WCHAR* str, BOOL isDrawDDA) {
	HRESULT hr;
	ID3DBlob* pBlob;

	SAFE_RELEASE(g_pCSDensity);

	if(isDrawDDA) {
		D3D10_SHADER_MACRO macros[] = {
			{"SHOW_DDA", "1"},
			{NULL, NULL}
		};
		V_RETURN(Utility::CompileShaderFromFile( str, "DensityCS", "cs_5_0", &pBlob, macros ));
	} else {
		V_RETURN(Utility::CompileShaderFromFile( str, "DensityCS", "cs_5_0", &pBlob));
	}

	hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSDensity );
	if( FAILED( hr ) )
	{	
		pBlob->Release();
		return hr;
	}
	pBlob->Release();
	return hr;
}

HRESULT Create(WCHAR* str, ID3D11Device* pd3dDevice, UINT gsize, UINT numPoints, BOOL isDrawDDA, curandGenerator_t prngGPU) {
	HRESULT hr;
	D3D11_BUFFER_DESC bufdesc;
	D3D11_SHADER_RESOURCE_VIEW_DESC srvdesc;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavdesc;

	RecreateResources(pd3dDevice, gsize, numPoints);
	g_prngGPU = prngGPU;

	bufdesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bufdesc.ByteWidth = DDA_SIZE * DDA_SIZE * sizeof(UINT);
	bufdesc.CPUAccessFlags = 0;
	bufdesc.MiscFlags = 0;
	bufdesc.StructureByteStride = 0;
	bufdesc.Usage = D3D11_USAGE_DEFAULT;
	V_RETURN(pd3dDevice->CreateBuffer(&bufdesc, NULL, &g_pBufferDDA));

	srvdesc.Buffer.FirstElement = 0;
	srvdesc.Buffer.NumElements = DDA_SIZE * DDA_SIZE;
	srvdesc.Format = DXGI_FORMAT_R32_UINT;
	srvdesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	V_RETURN(pd3dDevice->CreateShaderResourceView(g_pBufferDDA, &srvdesc, &g_pSRVBufferDDA));

	uavdesc.Buffer.FirstElement = 0;
	uavdesc.Buffer.Flags = 0;
	uavdesc.Buffer.NumElements = DDA_SIZE * DDA_SIZE;
	uavdesc.Format = DXGI_FORMAT_R32_UINT;
	uavdesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pBufferDDA, &uavdesc, &g_pUAVBufferDDA));

	D3DXVECTOR2 vertices[] =
	{
		D3DXVECTOR2( 1.0f, 1.0f ),
		D3DXVECTOR2( 1.0f, -1.0f ),
		D3DXVECTOR2( -1.0f, -1.0f ),
		D3DXVECTOR2( 1.0f, 1.0f ),
		D3DXVECTOR2( -1.0f, -1.0f ),
		D3DXVECTOR2( -1.0f, 1.0f )
	};

	D3D11_BUFFER_DESC bd;
	ZeroMemory( &bd, sizeof(bd) );
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof( D3DXVECTOR2 ) * 6;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory( &InitData, sizeof(InitData) );
	InitData.pSysMem = vertices;
	V_RETURN( pd3dDevice->CreateBuffer( &bd, &InitData, &g_pVBQuad ));

#ifdef BIN_DENSITY
	bd.ByteWidth = BIN_DENSITY * sizeof(UINT);
	bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	V_RETURN( pd3dDevice->CreateBuffer( &bd, NULL, &g_pBufBin));

	uavdesc.Buffer.NumElements = BIN_DENSITY;
	V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pBufBin, &uavdesc, &g_pUAVBin));	

	bd.BindFlags = 0;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	bd.Usage = D3D11_USAGE_STAGING;
	V_RETURN( pd3dDevice->CreateBuffer( &bd, NULL, &g_pBufBinStg ));
#endif

	ID3DBlob* pBlob = NULL;
	V_RETURN(Utility::CompileShaderFromFile( str, "IndexCS", "cs_5_0", &pBlob ));

	hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSComputeIndex );
	if( FAILED( hr ) )
	{	
		pBlob->Release();
		return hr;
	}
	pBlob->Release();

	V_RETURN(Utility::CompileShaderFromFile( str, "BuildGridCS", "cs_5_0", &pBlob ));

	hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSBuildGrid );
	if( FAILED( hr ) )
	{	
		pBlob->Release();
		return hr;
	}
	pBlob->Release();

	RecompileDDAShader(str, isDrawDDA);

	V_RETURN(Utility::CompileShaderFromFile( str, "TensionCS", "cs_5_0", &pBlob ));

	hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSTension );
	if( FAILED( hr ) )
	{	
		pBlob->Release();
		return hr;
	}
	pBlob->Release();

	V_RETURN(Utility::CompileShaderFromFile( str, "IntegrateCS", "cs_5_0", &pBlob ));

	hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSIntegrate );
	if( FAILED( hr ) )
	{	
		pBlob->Release();
		return hr;
	}
	pBlob->Release();

	V_RETURN(Utility::CompileShaderFromFile( str, "ShowDDAVS", "vs_5_0", &pBlob ));

	// Create the vertex shader
	hr = DXUTGetD3D11Device()->CreateVertexShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pVSQuad );
	if( FAILED( hr ) )
	{	
		pBlob->Release();
		return hr;
	}

	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = ARRAYSIZE( layout );

	hr = DXUTGetD3D11Device()->CreateInputLayout( layout, numElements, pBlob->GetBufferPointer(),
		pBlob->GetBufferSize(), &g_pILQuad );

	pBlob->Release();

	V_RETURN(Utility::CompileShaderFromFile( str, "ShowDDAPS", "ps_5_0", &pBlob ));

	// Create the vertex shader
	hr = DXUTGetD3D11Device()->CreatePixelShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pPSQuad );
	if( FAILED( hr ) )
	{	
		pBlob->Release();
		return hr;
	}
	pBlob->Release();

	return hr;
}


HRESULT BuildGrid(
	ID3D11DeviceContext* pImmediateContext, 
	ID3D11ShaderResourceView* pVB, 
	UINT numPoints, 
	ID3D11UnorderedAccessView* pVBUAV, bool bAdjust) {
	ID3D11ShaderResourceView* pSRVs[] = {
		pVB
	};

	static UINT counter = 0;

	pImmediateContext->CSSetShaderResources(1, 1, pSRVs);

	ID3D11UnorderedAccessView* pUAVs[] = {
		g_pUAVIndex,
		g_pUAVStartEnd
	};
	pImmediateContext->CSSetUnorderedAccessViews(0, 2, pUAVs, NULL);

	pImmediateContext->CSSetShader(g_pCSComputeIndex, NULL, 0);

	UINT pUINTClr[4] = {0};
	pImmediateContext->ClearUnorderedAccessViewUint(g_pUAVStartEnd, pUINTClr);

	UINT iGridX = iDivUp(numPoints, BLOCK_SIZE);

	pImmediateContext->Dispatch(iGridX, 1, 1);

//	Utility::CheckBuffer(g_pVBIndex);

	cudaGraphicsMapResources(1, &g_pGRIndex);

	unsigned __int64* dev_ptr = NULL;
	size_t dev_size = 0;
	cudaGraphicsResourceGetMappedPointer((void**)&dev_ptr, &dev_size, g_pGRIndex);

	thrust_stable_sort<unsigned __int64>(dev_ptr, numPoints);

	cudaGraphicsUnmapResources(1, &g_pGRIndex);

//	Utility::CheckBuffer(g_pVBIndex);

	pImmediateContext->CSSetShader(g_pCSBuildGrid, NULL, 0);
	pImmediateContext->Dispatch(iGridX, 1, 1);

//	Utility::CheckBuffer(g_pGBStartEnd);

	ID3D11UnorderedAccessView* pUAVs1[] = {
		NULL,
		NULL,
		g_pUAVDensity,
		g_pUAVGradient,
		g_pUAVTension,
		g_pUAVBufferDDA,
		NULL,
		g_pUAVBin
	};
	pImmediateContext->CSSetUnorderedAccessViews(0, 8, pUAVs1, NULL);
	UINT clrv[4] = {0};
	pImmediateContext->ClearUnorderedAccessViewUint(g_pUAVBufferDDA, clrv);
//	Utility::CheckBuffer(g_pBufferDDA);
	ID3D11ShaderResourceView* pSRVs1[] = {
		g_pSRVStartEnd,
		g_pSRVIndex
	};

	pImmediateContext->CSSetShaderResources(2, 2, pSRVs1);

	pImmediateContext->CSSetShader(g_pCSDensity, NULL, 0);
	pImmediateContext->Dispatch(iGridX, 1, 1);

	ID3D11ShaderResourceView* pSRVClr[4] = {NULL};
/*	Utility::CheckBuffer(g_pBufferDDA);*/
	if(bAdjust) {
		ID3D11ShaderResourceView* pSRVs2[] = {
			g_pSRVGradient
		};

		pUAVs1[2] = pUAVs1[3] = pUAVs1[5] = NULL;

		pImmediateContext->CSSetUnorderedAccessViews(0, 8, pUAVs1, NULL);

		pImmediateContext->CSSetShaderResources(4, 1, pSRVs2);

		Utility::CheckBuffer(g_pVBGradient);

		if(!g_bInit) {
			pImmediateContext->CSSetShader(g_pCSTension, NULL, 0);
			pImmediateContext->Dispatch(iGridX, 1, 1);
			g_bInit = TRUE;
			counter = 0;
		}
#ifdef RANDOM_DISTURBING
		Utility::UpdateRNG(g_prngGPU, numPoints * 2, g_pGRRandom);
#endif
		pImmediateContext->CSSetShaderResources(0, 4, pSRVClr);
#ifdef BIN_DENSITY
		pImmediateContext->ClearUnorderedAccessViewUint(g_pUAVBin, clrv);
#endif
		pUAVs1[4] = NULL;
		pImmediateContext->CSSetUnorderedAccessViews(0, 8, pUAVs1, NULL);
		pImmediateContext->CSSetUnorderedAccessViews(6, 1, &pVBUAV, NULL);

		pImmediateContext->CSSetShaderResources(5, 1, &g_pSRVTension);
		pImmediateContext->CSSetShaderResources(7, 1, &g_pSRVRandom);

		pImmediateContext->CSSetShader(g_pCSIntegrate, NULL, 0);
		pImmediateContext->Dispatch(iGridX, 1, 1);
#ifdef BIN_DENSITY
		pImmediateContext->CopyResource(g_pBufBinStg, g_pBufBin);
#endif
	}
	ID3D11UnorderedAccessView* pUAVClr[8] = {NULL};
	pImmediateContext->CSSetUnorderedAccessViews(0, 8, pUAVClr, NULL);

	pImmediateContext->CSSetShaderResources(4, 2, pSRVClr);

	counter++;
	return S_OK;
}

VOID GetBufBinData(FLOAT* pBin) {
#ifdef BIN_DENSITY
	ID3D11DeviceContext* pImmediateContext = DXUTGetD3D11DeviceContext();

	D3D11_MAPPED_SUBRESOURCE ms;
	pImmediateContext->Map(g_pBufBinStg, 0, D3D11_MAP_READ, 0, &ms);
	UINT* ptr = (UINT*)(ms.pData);

	UINT iSum = 0;
	for(UINT i = 0; i < BIN_DENSITY; i++) {
		iSum += ptr[i];
	}

	for(UINT i = 0; i < BIN_DENSITY; i++) {
		pBin[i] = (FLOAT)ptr[i] / (FLOAT)iSum;
	}
#endif
}

HRESULT ShowDDA(
	ID3D11DeviceContext* pImmediateContext
	) 
{
	UINT stride = sizeof(D3DXVECTOR2);
	UINT offset = 0;
	pImmediateContext->IASetVertexBuffers(0, 1, &g_pVBQuad, &stride, &offset);
	pImmediateContext->IASetInputLayout(g_pILQuad);
	pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pImmediateContext->VSSetShader(g_pVSQuad, NULL, 0);
	pImmediateContext->GSSetShader(NULL, NULL, 0);
	pImmediateContext->PSSetShader(g_pPSQuad, NULL, 0);
	pImmediateContext->PSSetShaderResources(6, 1, &g_pSRVBufferDDA);

	D3D11_VIEWPORT vp;
	vp.Width = DXUTGetWindowWidth();
	vp.Height = DXUTGetWindowHeight();
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	vp.TopLeftX = vp.TopLeftY = 0;
	pImmediateContext->RSSetViewports(1, &vp);

	ID3D11RenderTargetView* pRTV = DXUTGetD3D11RenderTargetView();
	pImmediateContext->OMSetRenderTargets(1, &pRTV, NULL);
	pImmediateContext->Draw(6, 0);

	ID3D11ShaderResourceView* pSRVClr[1] = {NULL};
	pImmediateContext->PSSetShaderResources(6, 1, pSRVClr);
	return S_OK;
}

VOID Destroy() {
	cudaGraphicsUnregisterResource(g_pGRIndex);
	cudaGraphicsUnregisterResource(g_pGRRandom);
	g_pGRIndex = NULL;
	g_pGRRandom = NULL;
	SAFE_RELEASE(g_pVBIndex);
	SAFE_RELEASE(g_pGBStartEnd);
	SAFE_RELEASE(g_pUAVIndex);
	SAFE_RELEASE(g_pUAVStartEnd);
	SAFE_RELEASE(g_pCSComputeIndex);
	SAFE_RELEASE(g_pCSBuildGrid);
	SAFE_RELEASE(g_pUAVDensity);
	SAFE_RELEASE(g_pUAVGradient);
	SAFE_RELEASE(g_pUAVTension);
	SAFE_RELEASE(g_pVBDensity);
	SAFE_RELEASE(g_pVBGradient);
	SAFE_RELEASE(g_pVBTension);
	SAFE_RELEASE(g_pCSDensity);
	SAFE_RELEASE(g_pCSIntegrate);
	SAFE_RELEASE(g_pSRVStartEnd);
	SAFE_RELEASE(g_pSRVIndex);
	SAFE_RELEASE(g_pSRVGradient);
	SAFE_RELEASE(g_pSRVTension);
	SAFE_RELEASE(g_pCSTension);
	SAFE_RELEASE(g_pSRVPoints);
	SAFE_RELEASE(g_pSRVBufferDDA);
	SAFE_RELEASE(g_pUAVBufferDDA);
	SAFE_RELEASE(g_pBufferDDA);
	SAFE_RELEASE(g_pVBQuad);
	SAFE_RELEASE(g_pILQuad);
	SAFE_RELEASE(g_pVSQuad);
	SAFE_RELEASE(g_pPSQuad);
	SAFE_RELEASE(g_pSRVRandom);
	SAFE_RELEASE(g_pBufRandom);
	SAFE_RELEASE(g_pUAVBin);
	SAFE_RELEASE(g_pBufBin);
	SAFE_RELEASE(g_pBufBinStg);

	g_bInit = FALSE;
	g_bCreated = FALSE;
}

BOOL IsInitialized() {
	return g_bCreated;
}

BOOL SetInitialized(BOOL v) {
	BOOL bP = g_bCreated;
	g_bCreated = v;
	return bP;
}


};