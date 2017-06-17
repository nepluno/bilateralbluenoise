#include "DXUT.h"
#include "GPURelaxer.h"

namespace DartThrower
{
	static ID3D11Buffer*				g_pRandomBuffer = NULL;
	static ID3D11ShaderResourceView*	g_pSRVRandomBuffer = NULL;
	static cudaGraphicsResource_t		g_pGRRandomBuffer = NULL;

	static ID3D11Buffer*				g_pCBParams = NULL;
	static ID3D11Buffer*				g_pVBPoints = NULL;
	static ID3D11Buffer*				g_pVBProxy = NULL;

	static ID3D11UnorderedAccessView*	g_pUAVPoints = NULL;
	static ID3D11UnorderedAccessView*	g_pUAVPointsCommon = NULL;
 	static ID3D11ShaderResourceView*	g_pSRVVB = NULL;

	static ID3D11Buffer*				g_pIIBPara = NULL;
	static ID3D11UnorderedAccessView*	g_pUAVIIBPara = NULL;
	static ID3D11ShaderResourceView*	g_pSRVIIBPara = NULL;

	static ID3D11Buffer*				g_pIDPara = NULL;
	
	static ID3D11Texture2D*				g_pTexGrid = NULL;
	static ID3D11ShaderResourceView*	g_pSRVGrid = NULL;
	static ID3D11UnorderedAccessView**	g_ppUAVGrid = NULL;

	static ID3D11ComputeShader*			g_pCSDartThrow = NULL;
	static ID3D11ComputeShader*			g_pCSDartThrowToBuffer = NULL;
	static ID3D11ComputeShader*			g_pCSMipMap = NULL;
	static ID3D11ComputeShader*			g_pCSDartThrowCombine = NULL;

	static curandGenerator_t			g_prngGPU = NULL;

	static ID3D11InputLayout*			g_pLayout11 = NULL;
	static ID3D11GeometryShader*		g_pGSVisualizePoint = NULL;
	static ID3D11VertexShader*			g_pVSPoint = NULL;
	static ID3D11PixelShader*			g_pPSVisualizePoint = NULL;

	static UINT BufSize = 1048576;
	static UINT GSize = 0;
	static UINT GLevel = 1;

	static FLOAT GRadius = 1.0f;

	struct CB_CONST_RAND {
		UINT iParam0[4];
		UINT iParam1[4];
		D3DXVECTOR4 fParam0;
	};

	static UINT iRandomBufPtr = 0;
	static UINT GnLayers = 1;

	HRESULT Create(WCHAR* str, ID3D11Device* pd3dDevice, UINT numPoints, FLOAT radius, UINT nLayers) {
		HRESULT hr = S_OK;

		GSize = sqrtf(numPoints) * 3;
		GRadius = radius;
		GnLayers = nLayers;

		D3D11_TEXTURE2D_DESC t2desc;
		t2desc.ArraySize = 3 * GnLayers;
		t2desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		t2desc.CPUAccessFlags = 0;
		t2desc.Format = DXGI_FORMAT_R32_FLOAT;
		t2desc.Height = GSize;
		t2desc.MipLevels = max(1, (UINT)(logf(GSize / 3) / logf(2.0f)));
		t2desc.MiscFlags = 0;
		t2desc.SampleDesc.Count = 1;
		t2desc.SampleDesc.Quality = 0;
		t2desc.Usage = D3D11_USAGE_DEFAULT;
		t2desc.Width = GSize;
		GLevel = t2desc.MipLevels;
		
		V_RETURN(pd3dDevice->CreateTexture2D(&t2desc, NULL, &g_pTexGrid));

		g_ppUAVGrid = new ID3D11UnorderedAccessView*[t2desc.MipLevels];
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavdesc;
		uavdesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavdesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uavdesc.Texture2DArray.FirstArraySlice = 0;
		uavdesc.Texture2DArray.ArraySize = 3 * GnLayers;
		int k = 0;
		for(int i = t2desc.MipLevels - 1; i >= 0; i--, k++) {
			uavdesc.Texture2DArray.MipSlice = i;
			pd3dDevice->CreateUnorderedAccessView(g_pTexGrid, &uavdesc, g_ppUAVGrid + k);
		}

		V_RETURN(pd3dDevice->CreateShaderResourceView(g_pTexGrid, NULL, &g_pSRVGrid));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvdesc;

		BufSize = 18 * numPoints;
		D3D11_BUFFER_DESC bdesc;
		bdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bdesc.ByteWidth = BufSize * 3 * sizeof(FLOAT) * GnLayers;
		bdesc.CPUAccessFlags = 0;
		bdesc.MiscFlags = 0;
		bdesc.StructureByteStride = 0;
		bdesc.Usage = D3D11_USAGE_DEFAULT;

		V_RETURN(pd3dDevice->CreateBuffer(&bdesc, NULL, &g_pRandomBuffer));

		srvdesc.Buffer.FirstElement = 0;
		srvdesc.Buffer.NumElements = BufSize * 3 * GnLayers;
		srvdesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvdesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		V_RETURN(pd3dDevice->CreateShaderResourceView(g_pRandomBuffer, &srvdesc, &g_pSRVRandomBuffer));

		cudaGraphicsD3D11RegisterResource(&g_pGRRandomBuffer, g_pRandomBuffer, 0);

		curandCreateGenerator(&g_prngGPU, CURAND_RNG_PSEUDO_MTGP32);
		curandSetPseudoRandomGeneratorSeed(g_prngGPU, GetTickCount());

		bdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bdesc.ByteWidth = sizeof(CB_CONST_RAND);
		bdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bdesc.MiscFlags = 0;
		bdesc.StructureByteStride = 0;
		bdesc.Usage = D3D11_USAGE_DYNAMIC;

		V_RETURN(pd3dDevice->CreateBuffer(&bdesc, NULL, &g_pCBParams));

		bdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		bdesc.ByteWidth = sizeof(D3DXVECTOR3) * GSize * GSize;
		bdesc.CPUAccessFlags = 0;
		bdesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bdesc.StructureByteStride = sizeof(D3DXVECTOR3);
		bdesc.Usage = D3D11_USAGE_DEFAULT;

		V_RETURN(pd3dDevice->CreateBuffer(&bdesc, NULL, &g_pVBPoints));

		srvdesc.Buffer.FirstElement = 0;
		srvdesc.Buffer.NumElements = GSize * GSize;
		srvdesc.Format = DXGI_FORMAT_UNKNOWN;
		srvdesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		V_RETURN(pd3dDevice->CreateShaderResourceView(g_pVBPoints, &srvdesc, &g_pSRVVB));

		uavdesc.Buffer.FirstElement = 0;
		uavdesc.Buffer.NumElements = GSize * GSize;
		uavdesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
		uavdesc.Format = DXGI_FORMAT_UNKNOWN;
		uavdesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pVBPoints, &uavdesc, &g_pUAVPoints));

		uavdesc.Buffer.Flags = 0;
		V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pVBPoints, &uavdesc, &g_pUAVPointsCommon));

		bdesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		bdesc.ByteWidth = sizeof(UINT) * 4;
		bdesc.CPUAccessFlags = 0;
		bdesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		bdesc.StructureByteStride = 0;
		bdesc.Usage = D3D11_USAGE_DEFAULT;
		V_RETURN(pd3dDevice->CreateBuffer(&bdesc, NULL, &g_pIIBPara));

		bdesc.BindFlags = NULL;
		bdesc.ByteWidth = sizeof(UINT);
		bdesc.MiscFlags = 0;
		bdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		bdesc.Usage = D3D11_USAGE_STAGING;
		V_RETURN(pd3dDevice->CreateBuffer(&bdesc, NULL, &g_pIDPara));

		uavdesc.Buffer.NumElements = 1;
		uavdesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
		uavdesc.Buffer.Flags = 0;
		V_RETURN(pd3dDevice->CreateUnorderedAccessView(g_pIIBPara, &uavdesc, &g_pUAVIIBPara));	

		srvdesc.Buffer.FirstElement = 0;
		srvdesc.Buffer.NumElements = 1;
		srvdesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
		srvdesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

		V_RETURN(pd3dDevice->CreateShaderResourceView(g_pIIBPara, &srvdesc, &g_pSRVIIBPara));

		bdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bdesc.ByteWidth = BufSize;
		bdesc.MiscFlags = 0;
		bdesc.StructureByteStride = 0;
 		bdesc.CPUAccessFlags = 0;
 		bdesc.Usage = D3D11_USAGE_DEFAULT;
		V_RETURN(pd3dDevice->CreateBuffer(&bdesc, NULL, &g_pVBProxy));

		ID3DBlob* pBlob;

		V_RETURN(Utility::CompileShaderFromFile( str, "DartThrowCS", "cs_5_0", &pBlob ));

		hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSDartThrow );
		if( FAILED( hr ) )
		{	
			pBlob->Release();
			return hr;
		}
		pBlob->Release();

		V_RETURN(Utility::CompileShaderFromFile( str, "DartThrowCSFinal", "cs_5_0", &pBlob));

		hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSDartThrowToBuffer );
		if( FAILED( hr ) )
		{	
			pBlob->Release();
			return hr;
		}
		pBlob->Release();

		V_RETURN(Utility::CompileShaderFromFile( str, "DartThrowCSCombine", "cs_5_0", &pBlob));

		hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSDartThrowCombine );
		if( FAILED( hr ) )
		{	
			pBlob->Release();
			return hr;
		}
		pBlob->Release();

		V_RETURN(Utility::CompileShaderFromFile( str, "MipMappingCS", "cs_5_0", &pBlob ));

		hr = DXUTGetD3D11Device()->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pCSMipMap );
		if( FAILED( hr ) )
		{	
			pBlob->Release();
			return hr;
		}
		pBlob->Release();

		V_RETURN(Utility::CompileShaderFromFile( str, "PointVS", "vs_5_0", &pBlob ));

		// Create the vertex shader
		hr = DXUTGetD3D11Device()->CreateVertexShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pVSPoint );
		if( FAILED( hr ) )
		{	
			pBlob->Release();
			return hr;
		}

		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R8_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		UINT numElements = ARRAYSIZE( layout );

		hr = DXUTGetD3D11Device()->CreateInputLayout( layout, numElements, pBlob->GetBufferPointer(),
			pBlob->GetBufferSize(), &g_pLayout11 );

		pBlob->Release();

		V_RETURN(Utility::CompileShaderFromFile( str, "VisualizeGS", "gs_5_0", &pBlob ));

		// Create the vertex shader
		hr = DXUTGetD3D11Device()->CreateGeometryShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pGSVisualizePoint );
		if( FAILED( hr ) )
		{	
			pBlob->Release();
			return hr;
		}
		pBlob->Release();

		V_RETURN(Utility::CompileShaderFromFile( str, "PointPS", "ps_5_0", &pBlob ));

		// Create the vertex shader
		hr = DXUTGetD3D11Device()->CreatePixelShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pPSVisualizePoint );
		if( FAILED( hr ) )
		{	
			pBlob->Release();
			return hr;
		}
		pBlob->Release();

		UpdateRNG();
		return hr;
	}

	VOID UpdateRNG() {
		Utility::UpdateRNG(g_prngGPU, BufSize * 3 * GnLayers, g_pGRRandomBuffer);

		Utility::CheckBuffer(g_pRandomBuffer);

		iRandomBufPtr = 0;
	}

	VOID DartThrowCombine(ID3D11DeviceContext* pd3dImmediateContext) {
		UINT bSizeX = max(1, iDivUp(GSize, 16));

		ID3D11UnorderedAccessView* pUAVs[] = {
			g_ppUAVGrid[GLevel - 1],
			NULL,
			g_pUAVPoints
		};

		UINT iClr[4] = {0, 0, -1U};
		pd3dImmediateContext->CSSetUnorderedAccessViews(0, 3, pUAVs, iClr);
		pd3dImmediateContext->CSSetShader(g_pCSDartThrowCombine, NULL, 0);

		for(int i = 1; i < GnLayers; i++) {
			D3D11_MAPPED_SUBRESOURCE ms;
			pd3dImmediateContext->Map(g_pCBParams, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
			CB_CONST_RAND* pData = (CB_CONST_RAND*)ms.pData;
			pData->iParam0[0] = 1;
			pData->iParam0[1] = 0;
			pData->iParam0[2] = GSize;
			pData->iParam0[3] = iRandomBufPtr;
			pData->iParam1[0] = GnLayers;
			pData->iParam1[1] = i;
			pData->iParam1[2] = 0;
			pData->iParam1[3] = 0;
			pData->fParam0 = D3DXVECTOR4(GRadius * (3 << GLevel), 0, 0, 0);
			pd3dImmediateContext->Unmap(g_pCBParams, 0);

			pd3dImmediateContext->CSSetConstantBuffers(0, 1, &g_pCBParams);

			pd3dImmediateContext->Dispatch(bSizeX, bSizeX, 1);
		}
	}

	VOID DartThrowInternal(UINT iLevel, ID3D11DeviceContext* pd3dImmediateContext, BOOL bFinal) {
		UINT iOrder[9] = {0};
		UINT iBlockSize = bFinal ? 9 : 4;
		for(UINT i = 0; i < iBlockSize; i++) {
			iOrder[i] = i;
		}
		for(UINT i = 0; i < iBlockSize; i++) {
			UINT j = rand() % iBlockSize;
			UINT t = iOrder[i];
			iOrder[i] = iOrder[j];
			iOrder[j] = t;
		}


		UINT iSizeX = ((bFinal ? 2 : 3) << iLevel);
		UINT RandomInc = iSizeX * iSizeX * iBlockSize;
		if(iRandomBufPtr + RandomInc > BufSize) {
			UpdateRNG();
		}

		UINT bSizeX = max(1, iDivUp(iSizeX, 16));

		ID3D11UnorderedAccessView* pUAVs[] = {
			g_ppUAVGrid[iLevel],
			NULL,
			g_pUAVPoints
		};

		UINT iClr[3] = {0};
		pd3dImmediateContext->CSSetUnorderedAccessViews(0, 3, pUAVs, iClr);
		pd3dImmediateContext->CSSetShader(bFinal ? g_pCSDartThrowToBuffer : g_pCSDartThrow, NULL, 0);
		pd3dImmediateContext->CSSetShaderResources(0, 1, &g_pSRVRandomBuffer);

		for(int j = 0; j < iBlockSize; j++) {
			D3D11_MAPPED_SUBRESOURCE ms;
			pd3dImmediateContext->Map(g_pCBParams, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
			CB_CONST_RAND* pData = (CB_CONST_RAND*)ms.pData;
			pData->iParam0[0] = iBlockSize;
			pData->iParam0[1] = iOrder[j];
			pData->iParam0[2] = iSizeX;
			pData->iParam0[3] = iRandomBufPtr;
			pData->iParam1[0] = GnLayers;
			pData->iParam1[1] = 0;
			pData->iParam1[2] = 0;
			pData->iParam1[3] = 0;
			pData->fParam0 = D3DXVECTOR4(GRadius * (3 << (iLevel + 1)), 0, 0, 0);
			pd3dImmediateContext->Unmap(g_pCBParams, 0);

			pd3dImmediateContext->CSSetConstantBuffers(0, 1, &g_pCBParams);

			pd3dImmediateContext->Dispatch(bSizeX, bSizeX, GnLayers);

			UINT iContinue[] = {-1U};
			pd3dImmediateContext->CSSetUnorderedAccessViews(2, 1, pUAVs + 2, iContinue);

// 			Utility::CheckBuffer(g_pTexGrid, GLevel - iLevel - 1, 0);
// 			Utility::CheckBuffer(g_pTexGrid, GLevel - iLevel - 1, 1);
		}

		iRandomBufPtr += iSizeX * iSizeX * iBlockSize;
	}

	VOID MipMapInternal(UINT iLevel, ID3D11DeviceContext* pd3dImmediateContext) {
		ID3D11UnorderedAccessView* pUAVs[] = {
			g_ppUAVGrid[iLevel + 1],
			g_ppUAVGrid[iLevel],
			NULL
		};

		FLOAT clrPos[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
		pd3dImmediateContext->ClearUnorderedAccessViewFloat(g_ppUAVGrid[iLevel + 1], clrPos);
		pd3dImmediateContext->CSSetUnorderedAccessViews(0, 3, pUAVs, NULL);
		pd3dImmediateContext->CSSetShader(g_pCSMipMap, NULL, 0);

		UINT iSizeX = (3 << (iLevel + 1));
		UINT bSizeX = max(1, iDivUp(iSizeX, 16));

		pd3dImmediateContext->Dispatch(bSizeX, bSizeX, GnLayers);
	}

	UINT DartThrow(ID3D11DeviceContext* pd3dImmediateContext, BOOL isAlways, BOOL isDisplay) {
		FLOAT clrPos[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
		pd3dImmediateContext->ClearUnorderedAccessViewFloat(g_ppUAVGrid[0], clrPos);

		for(int i = 0; i < GLevel - 1; i++) {
			DartThrowInternal(i, pd3dImmediateContext, FALSE);

			Utility::CheckBuffer(g_pTexGrid, (GLevel - i - 1), 0);

			MipMapInternal(i, pd3dImmediateContext);

			Utility::CheckBuffer(g_pTexGrid, (GLevel - i - 2), 0);
		}

		DartThrowInternal(GLevel - 1, pd3dImmediateContext, TRUE);

		DartThrowCombine(pd3dImmediateContext);

		if(isDisplay) {
			UINT clrValue[4] = {0, 1, 0, 0};
			pd3dImmediateContext->ClearUnorderedAccessViewUint(g_pUAVIIBPara, clrValue);
			pd3dImmediateContext->CopyStructureCount(g_pIIBPara, 0, g_pUAVPoints);
		}

		ID3D11UnorderedAccessView* pUAVClrs[6] = {0};
		pd3dImmediateContext->CSSetUnorderedAccessViews(0, 6, pUAVClrs, NULL);

		ID3D11ShaderResourceView* pSRVClrs[3] = {0};
		pd3dImmediateContext->CSSetShaderResources(0, 3, pSRVClrs);

		UINT count = 0;
		if(!isAlways) {
			count = GetNumPoints(pd3dImmediateContext);
		}
		return count;
	}

	UINT GetNumPoints(ID3D11DeviceContext* pd3dImmediateContext) {
		UINT count;
		pd3dImmediateContext->CopyStructureCount(g_pIDPara, 0, g_pUAVPoints);
		D3D11_MAPPED_SUBRESOURCE ms;
		pd3dImmediateContext->Map(g_pIDPara, 0, D3D11_MAP_READ, 0, &ms);
		count = *((UINT*)ms.pData);
		pd3dImmediateContext->Unmap(g_pIDPara, 0);
		return count;
	}

	void Visualize(ID3D11DeviceContext* pd3dImmediateContext) {
		pd3dImmediateContext->VSSetShader(g_pVSPoint, NULL, 0);
		pd3dImmediateContext->GSSetShader(g_pGSVisualizePoint, NULL, 0);
		pd3dImmediateContext->PSSetShader(g_pPSVisualizePoint, NULL, 0);

		UINT stride = sizeof(D3DXVECTOR4);
		UINT offset = 0;
		pd3dImmediateContext->IASetVertexBuffers(0, 1, &g_pVBProxy, &stride, &offset);
		pd3dImmediateContext->IASetInputLayout(g_pLayout11);
		pd3dImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

		ID3D11ShaderResourceView* pSRVs[] = {
			g_pSRVVB
		};

		pd3dImmediateContext->VSSetShaderResources(1, 1, pSRVs);

		D3D11_VIEWPORT vp;
		vp.Width = DXUTGetWindowWidth();
		vp.Height = DXUTGetWindowHeight();
		vp.MinDepth = 0;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = vp.TopLeftY = 0;
		pd3dImmediateContext->RSSetViewports(1, &vp);

		ID3D11RenderTargetView* pRTV = DXUTGetD3D11RenderTargetView();
		pd3dImmediateContext->OMSetRenderTargets(1, &pRTV, DXUTGetD3D11DepthStencilView());

		pd3dImmediateContext->DrawInstancedIndirect(g_pIIBPara, 0);

		pd3dImmediateContext->VSSetShader(NULL, NULL, 0);

		ID3D11ShaderResourceView* pSRVClr[2] = {NULL};
		pd3dImmediateContext->VSSetShaderResources(1, 2, pSRVClr);

	}

	VOID Destroy() {
		SAFE_RELEASE(g_pSRVGrid);
		for(int i = 0; i < GLevel; i++) {
			SAFE_RELEASE(g_ppUAVGrid[i]);
		}
		delete[] g_ppUAVGrid;

		SAFE_RELEASE(g_pTexGrid);
		cudaGraphicsUnregisterResource(g_pGRRandomBuffer);
		SAFE_RELEASE(g_pRandomBuffer);
		SAFE_RELEASE(g_pSRVRandomBuffer);
		SAFE_RELEASE(g_pCBParams);
		SAFE_RELEASE(g_pCSDartThrow);
		SAFE_RELEASE(g_pCSMipMap);
		SAFE_RELEASE(g_pVBPoints);
		SAFE_RELEASE(g_pUAVPoints);
		SAFE_RELEASE(g_pIIBPara);
		SAFE_RELEASE(g_pCSDartThrowToBuffer);

		SAFE_RELEASE(g_pUAVPointsCommon);
		SAFE_RELEASE(g_pSRVVB);

		SAFE_RELEASE(g_pVSPoint);
		SAFE_RELEASE(g_pGSVisualizePoint);
		SAFE_RELEASE(g_pPSVisualizePoint);
		SAFE_RELEASE(g_pLayout11);

		SAFE_RELEASE(g_pIIBPara);
		SAFE_RELEASE(g_pUAVIIBPara);
		SAFE_RELEASE(g_pIDPara);
		SAFE_RELEASE(g_pSRVIIBPara);
		SAFE_RELEASE(g_pVBProxy);
		SAFE_RELEASE(g_pCSDartThrowCombine);
	}

	ID3D11ShaderResourceView* GetSRVPos() {
		return g_pSRVVB;
	}

	ID3D11UnorderedAccessView* GetUAVPos() {
		return g_pUAVPointsCommon;
	}

	ID3D11Buffer* GetStagingBuffer() {
		DXUTGetD3D11DeviceContext()->Flush();
		D3D11_BUFFER_DESC bufdesc;
		g_pVBPoints->GetDesc(&bufdesc);
		bufdesc.BindFlags = 0;
		bufdesc.Usage = D3D11_USAGE_STAGING;
		bufdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		bufdesc.StructureByteStride = 0;
		bufdesc.MiscFlags = 0;

		ID3D11Buffer* pStag;
		DXUTGetD3D11Device()->CreateBuffer(&bufdesc, NULL, &pStag);
		DXUTGetD3D11DeviceContext()->CopyResource(pStag, g_pVBPoints);

		return pStag;
	}


	curandGenerator_t GetRandomGen()
	{
		return g_prngGPU;
	}

};

