#include "DXUT.h"
#include "DXUTgui.h"
#include "DXUTmisc.h"
#include "DXUTCamera.h"
#include "DXUTSettingsDlg.h"
#include "SDKmisc.h"
#include "SDKmesh.h"
#include "resource.h"
#include "GPURelaxer.h"
#include <math.h>
#include "common.hlsl.h"

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------
CModelViewerCamera          g_Camera;               // A model viewing camera
CDXUTDialogResourceManager  g_DialogResourceManager; // manager for shared resources of dialogs
CD3DSettingsDlg             g_SettingsDlg;          // Device settings dialog
CDXUTTextHelper*            g_pTxtHelper = NULL;
CDXUTDialog                 g_HUD;                  // dialog for standard controls
CDXUTDialog                 g_SampleUI;             // dialog for sample specific controls

// Direct3D 9 resources
extern ID3DXFont*           g_pFont9;
extern ID3DXSprite*         g_pSprite9;
ID3D11SamplerState*         g_pSamLinear = NULL;

UINT						g_iPointLevel = G_POINT_LEVEL;
UINT						g_iNumPoints = 1 << g_iPointLevel;
FLOAT						g_radius = 0.75f / sqrtf(3.4641f * (float)g_iNumPoints);
FLOAT						g_radiusGather = g_radius * 0.45f * sqrtf((BOX_RIGHT - BOX_LEFT) * (BOX_BOTTOM - BOX_TOP));

UINT						g_iGridLevel = g_iPointLevel / 2 - 2 ;
UINT						g_iGridSize = (1 << g_iGridLevel);

FLOAT						g_fScale = 0.0f;
FLOAT						g_fScale2 = 0.0f;

FLOAT						g_fScroll = 1.1f;
FLOAT						g_fRandom = 0.025f;

enum						RENDER_MODE {
	RM_STATIC,
	RM_RELAX,
	RM_ALWAYSDARTTHROW,
	RM_COUNT
};

RENDER_MODE					g_rendermode = RM_STATIC;

BOOL						g_bDisplay = TRUE;
BOOL						g_bShowDDA = TRUE;
// Direct3D 11 resources

//--------------------------------------------------------------------------------------
// Constant buffers
//--------------------------------------------------------------------------------------
#pragma pack(push,1)
struct CB_VS_PER_OBJECT
{
	D3DXVECTOR4			g_fParam0;
	D3DXVECTOR4			g_fParam1;
	D3DXVECTOR4			g_fParam2;
	UINT				g_iParam0[4];
	UINT				g_iParam1[4];
};

#pragma pack(pop)

ID3D11Buffer*                       g_pcbVSPerObject11 = NULL;

//--------------------------------------------------------------------------------------
// UI control IDs
//--------------------------------------------------------------------------------------
#define IDC_TOGGLEFULLSCREEN    1
#define IDC_TOGGLEREF           2
#define IDC_CHANGEDEVICE        3
#define IDC_SAVESAMPLE			4
#define IDC_SCROLLSAMPLE		5
#define IDC_SCROLLRANDOM		6
#define IDC_SHOWDDA				7


//--------------------------------------------------------------------------------------
// Forward declarations 
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                          void* pUserContext );
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext );
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext );
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext );
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext );

bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext );
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                     void* pUserContext );
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext );
void CALLBACK OnD3D11DestroyDevice( void* pUserContext );
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime,
                                 float fElapsedTime, void* pUserContext );
HRESULT InitConstBuffer(ID3D11DeviceContext* pd3dImmediateContext);

void InitApp();
void RenderText();

namespace Utility {
HRESULT CompileShaderFromFile( WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut, const D3D10_SHADER_MACRO* pMacro )
{
	HRESULT hr = S_OK;

	DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
	// Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
	// Setting this flag improves the shader debugging experience, but still allows 
	// the shaders to be optimized and to run exactly the way they will run in 
	// the release configuration of this program.
	dwShaderFlags |= D3DCOMPILE_DEBUG;
#else
	dwShaderFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	ID3DBlob* pErrorBlob;
	hr = D3DX11CompileFromFile( szFileName, pMacro, NULL, szEntryPoint, szShaderModel, 
		dwShaderFlags, 0, NULL, ppBlobOut, &pErrorBlob, NULL );
	if( FAILED(hr) )
	{
		if( pErrorBlob != NULL )
			printf( (char*)pErrorBlob->GetBufferPointer() );
		if( pErrorBlob ) pErrorBlob->Release();
		return hr;
	}
	if( pErrorBlob ) pErrorBlob->Release();

	return S_OK;
}

VOID CheckBuffer(ID3D11Buffer* pBuffer) {
#ifdef _DEBUG
	DXUTGetD3D11DeviceContext()->Flush();
	D3D11_BUFFER_DESC bufdesc;
	pBuffer->GetDesc(&bufdesc);
	bufdesc.BindFlags = 0;
	bufdesc.Usage = D3D11_USAGE_STAGING;
	bufdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	bufdesc.StructureByteStride = 0;
	bufdesc.MiscFlags = 0;

	ID3D11Buffer* pStag;
	DXUTGetD3D11Device()->CreateBuffer(&bufdesc, NULL, &pStag);
	DXUTGetD3D11DeviceContext()->CopySubresourceRegion(pStag, 0, 0, 0, 0, pBuffer, 0, NULL);
	D3D11_MAPPED_SUBRESOURCE ms;
	DXUTGetD3D11DeviceContext()->Map(pStag, 0, D3D11_MAP_READ_WRITE, 0, &ms);

	double* pDouble = (double*)ms.pData;
	UINT* pUINT = (UINT*)ms.pData;
	FLOAT* pFloat = (FLOAT*)ms.pData;
	unsigned __int64* pUINT64 = (unsigned __int64*)ms.pData;

	DXUTGetD3D11DeviceContext()->Unmap(pStag, 0);
	pStag->Release();
#endif
}

VOID CheckBuffer(ID3D11Texture2D* pBuffer, UINT iMipLevel, UINT iLayer) {
#ifdef _DEBUG
	DXUTGetD3D11DeviceContext()->Flush();
	D3D11_TEXTURE2D_DESC bufdesc;
	pBuffer->GetDesc(&bufdesc);
	bufdesc.BindFlags = 0;
	bufdesc.Usage = D3D11_USAGE_STAGING;
	bufdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

	ID3D11Texture2D* pStag;
	DXUTGetD3D11Device()->CreateTexture2D(&bufdesc, NULL, &pStag);
	DXUTGetD3D11DeviceContext()->CopyResource(pStag, pBuffer);
	D3D11_MAPPED_SUBRESOURCE ms;
	UINT isr = D3D11CalcSubresource(iMipLevel, iLayer, bufdesc.MipLevels);
	DXUTGetD3D11DeviceContext()->Map(pStag, isr, D3D11_MAP_READ_WRITE, 0, &ms);

	double* pDouble = (double*)ms.pData;
	UINT* pUINT = (UINT*)ms.pData;
	FLOAT* pFloat = (FLOAT*)ms.pData;
	unsigned __int64* pUINT64 = (unsigned __int64*)ms.pData;



	DXUTGetD3D11DeviceContext()->Unmap(pStag, isr);
	pStag->Release();
#endif
}

VOID UpdateRNG(curandGenerator_t prng, UINT numRandom, cudaGraphicsResource_t buffer) {
	cudaGraphicsMapResources(1, &buffer);

	void* devPtr = NULL;
	size_t nRNG = 0;
	cudaGraphicsResourceGetMappedPointer(&devPtr, &nRNG, buffer);

	curandGenerateUniform(prng, (float *) devPtr, numRandom);

	cudaGraphicsUnmapResources(1, &buffer);
}

};

//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );

	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	freopen("CONIN$", "r+t", stdin);
	freopen("CONOUT$", "w+t", stdout);
#endif
	g_fScale2 = logf((FLOAT)GetSystemMetrics(SM_CYSCREEN) / 240.0f);
	UINT wSize = (UINT)((FLOAT)GetSystemMetrics(SM_CYSCREEN) * 0.8f);
    // DXUT will create and use the best device (either D3D9 or D3D11) 
    // that is available on the system depending on which D3D callbacks are set below

    // Set DXUT callbacks
    DXUTSetCallbackMsgProc( MsgProc );
    DXUTSetCallbackKeyboard( OnKeyboard );
    DXUTSetCallbackFrameMove( OnFrameMove );
    DXUTSetCallbackDeviceChanging( ModifyDeviceSettings );

    DXUTSetCallbackD3D11DeviceAcceptable( IsD3D11DeviceAcceptable );
    DXUTSetCallbackD3D11DeviceCreated( OnD3D11CreateDevice );
    DXUTSetCallbackD3D11SwapChainResized( OnD3D11ResizedSwapChain );
    DXUTSetCallbackD3D11SwapChainReleasing( OnD3D11ReleasingSwapChain );
    DXUTSetCallbackD3D11DeviceDestroyed( OnD3D11DestroyDevice );
    DXUTSetCallbackD3D11FrameRender( OnD3D11FrameRender );

    InitApp();
    DXUTInit( true, true, NULL ); // Parse the command line, show msgboxes on error, no extra command line params
    DXUTSetCursorSettings( true, true );
    DXUTCreateWindow( L"GPURelaxer" );

    // Only require 10-level hardware, change to D3D_FEATURE_LEVEL_11_0 to require 11-class hardware
    // Switch to D3D_FEATURE_LEVEL_9_x for 10level9 hardware
    DXUTCreateDevice( D3D_FEATURE_LEVEL_11_0, true, wSize, wSize
		);

    DXUTMainLoop(); // Enter into the DXUT render loop

    return DXUTGetExitCode();
}


//--------------------------------------------------------------------------------------
// Initialize the app 
//--------------------------------------------------------------------------------------
void InitApp()
{
    g_SettingsDlg.Init( &g_DialogResourceManager );
    g_HUD.Init( &g_DialogResourceManager );
    g_SampleUI.Init( &g_DialogResourceManager );

    g_HUD.SetCallback( OnGUIEvent );
    int iY = 30;
    int iYo = 26;
    g_HUD.AddButton( IDC_TOGGLEFULLSCREEN, L"Toggle full screen", 0, iY, 170, 22 );
    g_HUD.AddButton( IDC_TOGGLEREF, L"Toggle REF (F3)", 0, iY += iYo, 170, 22, VK_F3 );
    g_HUD.AddButton( IDC_CHANGEDEVICE, L"Change device (F2)", 0, iY += iYo, 170, 22, VK_F2 );
	g_HUD.AddButton( IDC_SAVESAMPLE, L"Save Sample (F4)", 0, iY += iYo, 170, 22, VK_F4);
#ifdef SCROLL_VALUE
	g_HUD.AddSlider( IDC_SCROLLSAMPLE, -170, iY += iYo, 340, 22, 0, 1100, 1100);
#endif
#ifdef RANDOM_DISTURBING
	g_HUD.AddSlider( IDC_SCROLLRANDOM, -170, iY += iYo, 340, 22, 0, 1000, 500);	
#endif
	g_HUD.AddCheckBox(IDC_SHOWDDA, L"Display DDA (F5)", 0, iY += iYo, 170, 22, true, VK_F5);
    g_SampleUI.SetCallback( OnGUIEvent ); iY = 10;
	
}


//--------------------------------------------------------------------------------------
// Render the help and statistics text. This function uses the ID3DXFont interface for 
// efficient text rendering.
//--------------------------------------------------------------------------------------
void RenderText()
{
    g_pTxtHelper->Begin();
    g_pTxtHelper->SetInsertionPos( 5, 5 );
    g_pTxtHelper->SetForegroundColor( D3DXCOLOR( 0.0f, 0.0f, 1.0f, 1.0f ) );
    g_pTxtHelper->DrawTextLine( DXUTGetFrameStats( DXUTIsVsyncEnabled() ) );
    g_pTxtHelper->DrawTextLine( DXUTGetDeviceStats() );
	WCHAR buf[50];
	swprintf_s<50>(buf, L"Points: %d, r = %f", g_iNumPoints, g_radius);
	g_pTxtHelper->DrawTextLine( buf );
	g_pTxtHelper->DrawTextLine( 
		L"Keyboard Func: \n'1' - View Mode, \n'2' - Relaxation Mode, \n'3' - Dart Throwing Mode, \n\
		Mouse Wheel - Zoom in/out, \nCtrl+Mouse Wheel - Adjust point size.");

#ifdef BIN_DENSITY
	FLOAT bindata[BIN_DENSITY] = {0};
	GridSorter::GetBufBinData(bindata);

	WCHAR buf2[50] = L"Bin: ";

	for(UINT i = 0; i < BIN_DENSITY; i++) {
		swprintf_s<50>(buf, L"%f, ", bindata[i]);
		wcscat_s(buf2, buf);
	}
	g_pTxtHelper->DrawTextLine( buf2 );
#endif

    g_pTxtHelper->End();
}


//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext )
{
    return true;
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                     void* pUserContext )
{
    HRESULT hr;

    ID3D11DeviceContext* pd3dImmediateContext = DXUTGetD3D11DeviceContext();
    V_RETURN( g_DialogResourceManager.OnD3D11CreateDevice( pd3dDevice, pd3dImmediateContext ) );
    V_RETURN( g_SettingsDlg.OnD3D11CreateDevice( pd3dDevice ) );
    g_pTxtHelper = new CDXUTTextHelper( pd3dDevice, pd3dImmediateContext, &g_DialogResourceManager, 15 );

    // Create state objects
    D3D11_SAMPLER_DESC samDesc;
    ZeroMemory( &samDesc, sizeof(samDesc) );
    samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samDesc.AddressU = samDesc.AddressV = samDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samDesc.MaxAnisotropy = 1;
    samDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samDesc.MaxLOD = D3D11_FLOAT32_MAX;
    V_RETURN( pd3dDevice->CreateSamplerState( &samDesc, &g_pSamLinear ) );
    DXUT_SetDebugName( g_pSamLinear, "Linear" );

    // Create other render resources here
	D3D11_BUFFER_DESC cbDesc;
	ZeroMemory( &cbDesc, sizeof(cbDesc) );
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	cbDesc.ByteWidth = sizeof( CB_VS_PER_OBJECT );
	V_RETURN( pd3dDevice->CreateBuffer( &cbDesc, NULL, &g_pcbVSPerObject11 ) );
	DXUT_SetDebugName( g_pcbVSPerObject11, "CB_VS_PER_OBJECT" );

    // Setup the camera's view parameters
    D3DXVECTOR3 vecEye( 0.0f, 0.0f, -5.0f );
    D3DXVECTOR3 vecAt ( 0.0f, 0.0f, -0.0f );
    g_Camera.SetViewParams( &vecEye, &vecAt );



	DartThrower::Create(L"DartThrower.hlsl.cpp", pd3dDevice, g_iNumPoints, g_radius, 
#ifdef FUZZY_BLUE_NOISE
		FUZZY_BLUE_NOISE
#else
		1
#endif
		);

	g_iNumPoints = DartThrower::DartThrow(DXUTGetD3D11DeviceContext());
	if(g_bShowDDA) {
		g_iNumPoints = DartThrower::GetNumPoints(pd3dImmediateContext);
		InitConstBuffer(pd3dImmediateContext);
	}

	GridSorter::Create(L"Shader.hlsl.cpp", pd3dDevice, g_iGridSize, g_iNumPoints, g_bShowDDA, DartThrower::GetRandomGen());

	if(g_bShowDDA)
		GridSorter::BuildGrid(pd3dImmediateContext, DartThrower::GetSRVPos(), g_iNumPoints, DartThrower::GetUAVPos(), false);
    return S_OK;
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that depend on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext )
{
    HRESULT hr;

    V_RETURN( g_DialogResourceManager.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );
    V_RETURN( g_SettingsDlg.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );

    // Setup the camera's projection parameters
    float fAspectRatio = pBackBufferSurfaceDesc->Width / ( FLOAT )pBackBufferSurfaceDesc->Height;
    g_Camera.SetProjParams( D3DX_PI / 4, fAspectRatio, 0.1f, 1000.0f );
    g_Camera.SetWindow( pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height );
    g_Camera.SetButtonMasks( MOUSE_LEFT_BUTTON, MOUSE_WHEEL, MOUSE_MIDDLE_BUTTON );

    g_HUD.SetLocation( pBackBufferSurfaceDesc->Width - 170, 0 );
    g_HUD.SetSize( 170, 170 );
    g_SampleUI.SetLocation( pBackBufferSurfaceDesc->Width - 170, pBackBufferSurfaceDesc->Height - 300 );
    g_SampleUI.SetSize( 170, 300 );

    return S_OK;
}

HRESULT InitConstBuffer(ID3D11DeviceContext* pd3dImmediateContext) {
	HRESULT hr;

	D3D11_MAPPED_SUBRESOURCE MappedResource;
	V( pd3dImmediateContext->Map( g_pcbVSPerObject11, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource ) );
	CB_VS_PER_OBJECT* pVSPerObject = ( CB_VS_PER_OBJECT* )MappedResource.pData;

	pVSPerObject->g_fParam0 = D3DXVECTOR4(1.0f / (float)DXUTGetWindowWidth(), 1.0f / (float)DXUTGetWindowHeight(), g_iGridSize, g_radiusGather * g_radiusGather);
	pVSPerObject->g_fParam1 = D3DXVECTOR4(sqrt(mDensity / g_iNumPoints
#ifndef FUZZY_BLUE_NOISE
		* 0.09f
#else
		* 0.36f
#endif
		), expf(g_fScale), expf(g_fScale2), g_fScroll);
	pVSPerObject->g_fParam2 = D3DXVECTOR4(g_fRandom, 0, 0, 0);
	pVSPerObject->g_iParam0[0] = g_iNumPoints;
	pVSPerObject->g_iParam0[1] = g_iGridSize;
	pVSPerObject->g_iParam0[2] = g_iGridLevel;
	pVSPerObject->g_iParam0[3] = g_iGridSize * g_iGridSize;
	pVSPerObject->g_iParam1[0] = DXUTGetWindowWidth();
	pVSPerObject->g_iParam1[1] = DXUTGetWindowHeight();

	pd3dImmediateContext->Unmap( g_pcbVSPerObject11, 0 );
	pd3dImmediateContext->VSSetConstantBuffers( 0, 1, &g_pcbVSPerObject11 );
	pd3dImmediateContext->GSSetConstantBuffers( 0, 1, &g_pcbVSPerObject11 );
	pd3dImmediateContext->CSSetConstantBuffers( 0, 1, &g_pcbVSPerObject11 );
	pd3dImmediateContext->PSSetConstantBuffers( 0, 1, &g_pcbVSPerObject11 );
	return hr;
}
//--------------------------------------------------------------------------------------
// Render the scene using the D3D11 device
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime,
                                 float fElapsedTime, void* pUserContext )
{
	HRESULT hr;
    // If the settings dialog is being shown, then render it instead of rendering the app's scene
    if( g_SettingsDlg.IsActive() )
    {
        g_SettingsDlg.OnRender( fElapsedTime );
        return;
    }       

	float ClearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	ID3D11RenderTargetView* pRTV = DXUTGetD3D11RenderTargetView();
	pd3dImmediateContext->ClearRenderTargetView( pRTV, ClearColor );
	ID3D11DepthStencilView* pDSV = DXUTGetD3D11DepthStencilView();
	pd3dImmediateContext->ClearDepthStencilView( pDSV, D3D11_CLEAR_DEPTH, 1.0, 0 );

	InitConstBuffer(pd3dImmediateContext);
	
	if(g_bDisplay)
		DartThrower::Visualize(pd3dImmediateContext);
    // Render objects here...

	switch(g_rendermode) {
	case RM_STATIC:
		if(g_bShowDDA)
			GridSorter::ShowDDA(pd3dImmediateContext);
		break;
	case RM_RELAX:
		if(!GridSorter::IsInitialized()) {
			g_iNumPoints = DartThrower::GetNumPoints(pd3dImmediateContext);
			InitConstBuffer(pd3dImmediateContext);
			GridSorter::RecreateResources(pd3dDevice, g_iGridSize, g_iNumPoints);
		}
		GridSorter::BuildGrid(pd3dImmediateContext, DartThrower::GetSRVPos(), g_iNumPoints, DartThrower::GetUAVPos(), true);
		if(g_bShowDDA)
			GridSorter::ShowDDA(pd3dImmediateContext);
		break;
	case RM_ALWAYSDARTTHROW:
		DartThrower::DartThrow(pd3dImmediateContext, TRUE, g_bDisplay);
		GridSorter::SetInitialized(FALSE);
		if(g_bShowDDA) {
			g_iNumPoints = DartThrower::GetNumPoints(pd3dImmediateContext);
			InitConstBuffer(pd3dImmediateContext);
			GridSorter::RecreateResources(pd3dDevice, g_iGridSize, g_iNumPoints);
			GridSorter::BuildGrid(pd3dImmediateContext, DartThrower::GetSRVPos(), g_iNumPoints, DartThrower::GetUAVPos(), false);
			GridSorter::ShowDDA(pd3dImmediateContext);
		}
		break;
	}

    DXUT_BeginPerfEvent( DXUT_PERFEVENTCOLOR, L"HUD / Stats" );
 	if(g_bDisplay) {
		g_HUD.OnRender( fElapsedTime );
		g_SampleUI.OnRender( fElapsedTime );
	}
    RenderText();
    DXUT_EndPerfEvent();

    static DWORD dwTimefirst = GetTickCount();
    if ( GetTickCount() - dwTimefirst > 5000 )
    {    
        OutputDebugString( DXUTGetFrameStats( DXUTIsVsyncEnabled() ) );
        OutputDebugString( L"\n" );
        dwTimefirst = GetTickCount();
    }

}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext )
{
    g_DialogResourceManager.OnD3D11ReleasingSwapChain();
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11DestroyDevice( void* pUserContext )
{
    g_DialogResourceManager.OnD3D11DestroyDevice();
    g_SettingsDlg.OnD3D11DestroyDevice();
    DXUTGetGlobalResourceCache().OnDestroyDevice();
    SAFE_DELETE( g_pTxtHelper );

    SAFE_RELEASE( g_pSamLinear );

    // Delete additional render resources here...

    SAFE_RELEASE( g_pcbVSPerObject11 );
	DartThrower::Destroy();
	if(GridSorter::IsInitialized())
		GridSorter::Destroy();
}


//--------------------------------------------------------------------------------------
// Called right before creating a D3D9 or D3D11 device, allowing the app to modify the device settings as needed
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext )
{
    if( pDeviceSettings->ver == DXUT_D3D9_DEVICE )
    {
        IDirect3D9* pD3D = DXUTGetD3D9Object();
        D3DCAPS9 Caps;
        pD3D->GetDeviceCaps( pDeviceSettings->d3d9.AdapterOrdinal, pDeviceSettings->d3d9.DeviceType, &Caps );

        // If device doesn't support HW T&L or doesn't support 1.1 vertex shaders in HW 
        // then switch to SWVP.
        if( ( Caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT ) == 0 ||
            Caps.VertexShaderVersion < D3DVS_VERSION( 1, 1 ) )
        {
            pDeviceSettings->d3d9.BehaviorFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        }

        // Debugging vertex shaders requires either REF or software vertex processing 
        // and debugging pixel shaders requires REF.  
#ifdef DEBUG_VS
        if( pDeviceSettings->d3d9.DeviceType != D3DDEVTYPE_REF )
        {
            pDeviceSettings->d3d9.BehaviorFlags &= ~D3DCREATE_HARDWARE_VERTEXPROCESSING;
            pDeviceSettings->d3d9.BehaviorFlags &= ~D3DCREATE_PUREDEVICE;
            pDeviceSettings->d3d9.BehaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        }
#endif
#ifdef DEBUG_PS
        pDeviceSettings->d3d9.DeviceType = D3DDEVTYPE_REF;
#endif
    }

    // For the first device created if its a REF device, optionally display a warning dialog box
    static bool s_bFirstTime = true;
    if( s_bFirstTime )
    {
        s_bFirstTime = false;
        if( ( DXUT_D3D9_DEVICE == pDeviceSettings->ver && pDeviceSettings->d3d9.DeviceType == D3DDEVTYPE_REF ) ||
            ( DXUT_D3D11_DEVICE == pDeviceSettings->ver &&
            pDeviceSettings->d3d11.DriverType == D3D_DRIVER_TYPE_REFERENCE ) )
        {
            DXUTDisplaySwitchingToREFWarning( pDeviceSettings->ver );
        }

    }

    return true;
}


//--------------------------------------------------------------------------------------
// Handle updates to the scene.  This is called regardless of which D3D API is used
//--------------------------------------------------------------------------------------
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext )
{
    // Update the camera's position based on user input 
    g_Camera.FrameMove( fElapsedTime );
}


//--------------------------------------------------------------------------------------
// Handle messages to the application
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                          void* pUserContext )
{
    // Pass messages to dialog resource manager calls so GUI state is updated correctly
    *pbNoFurtherProcessing = g_DialogResourceManager.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass messages to settings dialog if its active
    if( g_SettingsDlg.IsActive() )
    {
        g_SettingsDlg.MsgProc( hWnd, uMsg, wParam, lParam );
        return 0;
    }

    // Give the dialogs a chance to handle the message first
    *pbNoFurtherProcessing = g_HUD.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;
    *pbNoFurtherProcessing = g_SampleUI.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass all remaining windows messages to camera so it can respond to user input
    g_Camera.HandleMessages( hWnd, uMsg, wParam, lParam );

	switch(uMsg) {
	case WM_MOUSEWHEEL:
		if(LOWORD(wParam) & MK_CONTROL) {
			g_fScale2 += (float)((short)HIWORD(wParam)) / 120.0f * 0.01f;
		} else {
			g_fScale += (float)((short)HIWORD(wParam)) / 120.0f * 0.01f;
		}
		break;
	}

    return 0;
}


//--------------------------------------------------------------------------------------
// Handle key presses
//--------------------------------------------------------------------------------------
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext )
{
	if(!bKeyDown)
		return;

	switch(nChar) {
	case '1':
		g_rendermode = RM_STATIC;
		break;
	case '2':
		g_rendermode = RM_RELAX;
		break;
	case '3':
		g_rendermode = RM_ALWAYSDARTTHROW;
		break;
	case 'I':
		g_bDisplay = !g_bDisplay;
		break;
	}
}

inline void h2rgb( float h, float& r, float& g, float& b ) {
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

//--------------------------------------------------------------------------------------
// Handles the GUI events
//--------------------------------------------------------------------------------------
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext )
{
    switch( nControlID )
    {
        case IDC_TOGGLEFULLSCREEN:
            DXUTToggleFullScreen();
            break;
        case IDC_TOGGLEREF:
            DXUTToggleREF();
            break;
        case IDC_CHANGEDEVICE:
            g_SettingsDlg.SetActive( !g_SettingsDlg.IsActive() );
            break;
		case IDC_SAVESAMPLE:
			{
				g_iNumPoints = DartThrower::GetNumPoints(DXUTGetD3D11DeviceContext());
				ID3D11Buffer* pBuffer = DartThrower::GetStagingBuffer();

				D3D11_MAPPED_SUBRESOURCE ms;
				DXUTGetD3D11DeviceContext()->Map(pBuffer, 0, D3D11_MAP_READ, 0, &ms);

				D3DXVECTOR3* pVec = (D3DXVECTOR3*)ms.pData;

				std::ofstream f2("sample.off");
				f2 << "COFF\n";
				f2 << g_iNumPoints << " 0 0\n";
				for (UINT i = 0; i < g_iNumPoints; ++i)
				{
					if(pVec[i].x > BOX_LEFT && pVec[i].x < BOX_RIGHT && pVec[i].y > BOX_TOP && pVec[i].y < BOX_BOTTOM)
					{
						float r, g, b;
						h2rgb(pVec[i].z * 0.666f, r, g, b);

						f2 << pVec[i].x << ", " << pVec[i].y
							<< ", " <<  r << ", " << g << ", " << b << ",\n";
					}
				}
				f2.close();
			
				DXUTGetD3D11DeviceContext()->Unmap(pBuffer, 0);
				pBuffer->Release();
			}
			break;
		case IDC_SCROLLSAMPLE: 
			g_fScroll = (FLOAT)(g_HUD.GetSlider(IDC_SCROLLSAMPLE)->GetValue()) * 0.001f;
			break;
		case IDC_SCROLLRANDOM: 
			g_fRandom = (FLOAT)(g_HUD.GetSlider(IDC_SCROLLRANDOM)->GetValue()) * 0.00005f;
			break;
		case IDC_SHOWDDA:
			g_bShowDDA = g_HUD.GetCheckBox(IDC_SHOWDDA)->GetChecked();
			GridSorter::RecompileDDAShader(L"Shader.hlsl.cpp", g_bShowDDA);
			break;
    }
}
