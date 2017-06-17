#include "DXUT.h"
#include "GPURelaxer.h"
#include <fstream>

namespace PointDist {
ID3D11InputLayout*          g_pLayout11 = NULL;
ID3D11GeometryShader*		g_pGSVisualizePoint = NULL;
ID3D11VertexShader*			g_pVSPoint = NULL;
ID3D11PixelShader*			g_pPSVisualizePoint = NULL;
ID3D11Buffer*				g_pVBPoints = NULL;
ID3D11Buffer*				g_pVBPointsColor = NULL;
ID3D11ShaderResourceView*	g_pSRVVB = NULL;
ID3D11ShaderResourceView*	g_pSRVVBColor = NULL;
ID3D11UnorderedAccessView*	g_pUAVVBColor = NULL;
ID3D11UnorderedAccessView*	g_pUAVVB = NULL;
const static UINT			g_iMaxPoint = 1048576;

ID3D11ShaderResourceView* GetSRVVB() {
	return g_pSRVVB;
}

ID3D11ShaderResourceView* GetSRVVBColor() {
	return g_pSRVVBColor;
}

ID3D11UnorderedAccessView*	GetUAVVB() {
	return g_pUAVVB;
}

void RandomGenPointsFromSketch(
	ID3D11Buffer** ppProxyBuffer,
	ID3D11Buffer** ppColorBuffer, ID3D11ShaderResourceView** ppSRVColorBuffer, ID3D11UnorderedAccessView** ppUAVColorBuffer) 
{
	D3D11_BUFFER_DESC bdesc;
	bdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bdesc.ByteWidth = g_iMaxPoint;
	bdesc.CPUAccessFlags = 0;
	bdesc.MiscFlags = 0;
	bdesc.StructureByteStride = 0;
	bdesc.Usage = D3D11_USAGE_DEFAULT;

	DXUTGetD3D11Device()->CreateBuffer(&bdesc, NULL, ppProxyBuffer);

	bdesc.ByteWidth = g_iMaxPoint * sizeof(D3DXVECTOR4);
	bdesc.StructureByteStride = 0;

	DXUTGetD3D11Device()->CreateBuffer(&bdesc, NULL, ppColorBuffer);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvdesc;
	srvdesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	srvdesc.Buffer.FirstElement = 0;
	srvdesc.Buffer.NumElements = g_iMaxPoint;
	srvdesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	DXUTGetD3D11Device()->CreateShaderResourceView(*ppColorBuffer, &srvdesc, ppSRVColorBuffer);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavdesc;
	uavdesc.Buffer.NumElements = g_iMaxPoint * 4;
	uavdesc.Buffer.FirstElement = 0;
	uavdesc.Format = DXGI_FORMAT_R32_FLOAT;
	uavdesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	DXUTGetD3D11Device()->CreateUnorderedAccessView(*ppColorBuffer, &uavdesc, ppUAVColorBuffer);


}


HRESULT Create(WCHAR* str, ID3D11Device* pd3dDevice, UINT& num, ID3D11ShaderResourceView* pSRVSketch) {
	HRESULT hr = S_OK;
	ID3DBlob* pBlob = NULL;



	RandomGenPointsFromSketch(g_iMaxPoint, &g_pVBPoints, &g_pVBPointsColor, &g_pSRVVBColor, &g_pUAVVBColor);
	//RandomGenPoints(num, &g_pVBPoints, &g_pSRVVB, &g_pUAVVB, &g_pVBPointsColor, &g_pSRVVBColor, &g_pUAVVBColor);
	//num = ReadFromOFF("photons0.off", &g_pVBPoints, &g_pSRVVB, &g_pUAVVB, &g_pVBPointsColor, &g_pSRVVBColor, &g_pUAVVBColor);
	return hr;
}

void Destroy() {
	SAFE_RELEASE(g_pVSPoint);
	SAFE_RELEASE(g_pGSVisualizePoint);
	SAFE_RELEASE(g_pPSVisualizePoint);
	SAFE_RELEASE(g_pVBPoints);
	SAFE_RELEASE(g_pSRVVB);
	SAFE_RELEASE(g_pUAVVB);
	SAFE_RELEASE(g_pVBPointsColor);
	SAFE_RELEASE(g_pSRVVBColor);
	SAFE_RELEASE(g_pUAVVBColor);
	SAFE_RELEASE(g_pLayout11);
}

void Visualize(ID3D11DeviceContext* pd3dImmediateContext, UINT num) {
	pd3dImmediateContext->VSSetShader(g_pVSPoint, NULL, 0);
	pd3dImmediateContext->GSSetShader(g_pGSVisualizePoint, NULL, 0);
	pd3dImmediateContext->PSSetShader(g_pPSVisualizePoint, NULL, 0);

	UINT stride = sizeof(D3DXVECTOR2);
	UINT offset = 0;
	pd3dImmediateContext->IASetVertexBuffers(0, 1, &g_pVBPoints, &stride, &offset);
	pd3dImmediateContext->IASetInputLayout(g_pLayout11);
	pd3dImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

	pd3dImmediateContext->GSSetShaderResources(0, 1, &g_pSRVVBColor);

	D3D11_VIEWPORT vp;
	vp.Width = DXUTGetWindowWidth();
	vp.Height = DXUTGetWindowHeight();
	vp.MinDepth = 0;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = vp.TopLeftY = 0;
	pd3dImmediateContext->RSSetViewports(1, &vp);

	ID3D11RenderTargetView* pRTV = DXUTGetD3D11RenderTargetView();
	pd3dImmediateContext->OMSetRenderTargets(1, &pRTV, NULL);

	pd3dImmediateContext->Draw(num, 0);

	pd3dImmediateContext->GSSetShader(NULL, NULL, 0);

	ID3D11ShaderResourceView* pSRVClr[1] = {NULL};
	pd3dImmediateContext->GSSetShaderResources(0, 1, pSRVClr);

}

void Visualize(ID3D11DeviceContext* pd3dImmediateContext, UINT num, ID3D11Buffer* pAppendBuffer, UINT iCount) {
	pd3dImmediateContext->VSSetShader(g_pVSPoint, NULL, 0);
	pd3dImmediateContext->GSSetShader(g_pGSVisualizePoint, NULL, 0);
	pd3dImmediateContext->PSSetShader(g_pPSVisualizePoint, NULL, 0);

	UINT stride = sizeof(D3DXVECTOR2);
	UINT offset = 0;
	pd3dImmediateContext->IASetVertexBuffers(0, 1, &pAppendBuffer, &stride, &offset);
	pd3dImmediateContext->IASetInputLayout(g_pLayout11);
	pd3dImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

	D3D11_VIEWPORT vp;
	vp.Width = DXUTGetWindowWidth();
	vp.Height = DXUTGetWindowHeight();
	vp.MinDepth = 0;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = vp.TopLeftY = 0;
	pd3dImmediateContext->RSSetViewports(1, &vp);

	ID3D11RenderTargetView* pRTV = DXUTGetD3D11RenderTargetView();
	pd3dImmediateContext->OMSetRenderTargets(1, &pRTV, NULL);

	pd3dImmediateContext->Draw(iCount, 0);

	pd3dImmediateContext->GSSetShader(NULL, NULL, 0);

	ID3D11ShaderResourceView* pSRVClr[1] = {NULL};
	pd3dImmediateContext->GSSetShaderResources(0, 1, pSRVClr);

}
};