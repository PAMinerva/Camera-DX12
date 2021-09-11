//***************************************************************************************
// d3dApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "d3dApp.h"
#include <WindowsX.h>

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// Forward hwnd on because we can get messages (e.g., WM_CREATE)
	// before CreateWindow returns, and thus before mhMainWnd is valid.
    return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

D3DApp* D3DApp::mApp = nullptr;
D3DApp* D3DApp::GetApp()
{
    return mApp;
}

D3DApp::D3DApp(HINSTANCE hInstance)
:	mhAppInst(hInstance)
{
    // Only one D3DApp can be constructed.
    assert(mApp == nullptr);
    mApp = this;
}

D3DApp::~D3DApp()
{
	if(md3dDevice != nullptr)
		FlushCommandQueue();
}

HINSTANCE D3DApp::AppInst()const
{
	return mhAppInst;
}

HWND D3DApp::MainWnd()const
{
	return mhMainWnd;
}

float D3DApp::AspectRatio()const
{
	return static_cast<float>(mClientWidth) / mClientHeight;
}

bool D3DApp::Get4xMsaaState()const
{
    return m4xMsaaState;
}

void D3DApp::Set4xMsaaState(bool value)
{
    if(m4xMsaaState != value)
    {
        m4xMsaaState = value;

        // Recreate the swapchain and buffers with new multisample settings.
        CreateSwapChain();
        OnResize();
    }
}

int D3DApp::Run()
{
	MSG msg = {0};
 
	mTimer.Reset();

	while(msg.message != WM_QUIT)
	{
		// Se  ci sono messaggi li processa...
		if(PeekMessage( &msg, 0, 0, 0, PM_REMOVE ))
		{
            TranslateMessage( &msg );
            DispatchMessage( &msg );
		}
		// ... altrimenti si procede con il disegno.
		else
        {	
			mTimer.Tick();

			if( !mAppPaused )
			{
				CalculateFrameStats();
				Update(mTimer);	
                Draw(mTimer);
			}
			else
			{
				Sleep(100);
			}
        }
    }

	return (int)msg.wParam;
}

bool D3DApp::Initialize()
{
	// Crea la finestra su cui renderizzare
	if(!InitMainWindow())
		return false;

	// Inizializza Direct3D 12
	if(!InitDirect3D())
		return false;

	// Alcuni passaggi per l'inizializzazione di Direct3D devono
	// essere eseguiti anche quando si ridimensiona la finestra
	// così si mette tale codice in un metodo separato (OnResize) 
	// per evitare duplicazioni di codice.
    OnResize();

	return true;
}

void D3DApp::CreateRtvAndDsvDescriptorHeaps()
{
	// Crea descriptor heap per i due descriptor (RTV) ai buffer nello swapchain.
	// RTV non possono condividere heap con altri tipi di descriptor.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));


	// Crea descriptor heap per il descriptor (DSV) al depth-stencil buffer.
	// DSV non possono condividere heap con altri tipi di descriptor.
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void D3DApp::OnResize()
{
	assert(md3dDevice);
	assert(mSwapChain);
    assert(mDirectCmdListAlloc);

	// Quando si ridimensiona la finestra tutto ciò che sta nella coda non ha più valore
	// e quindi bisogna svuotarla. Certo, svuotare la coda significa eseguirne i comandi ma 
	// l'effetto di tale esecuzione sarà visibile solo per qualche frazione di secondo.
	FlushCommandQueue();

	// Porta la command list allo stato iniziale. Reset necessita che command list sia chiusa:
	// In fase di inzializzazione viene chiusa in CreateCommandObjects, a sua volta invocata da InitDirect3D.
	// A runtime viene chiusa comunque perché in D3DApp::Run, se si decide di gestire messaggio resize (nel
	// blocco IF), vuol dire che la command list è stata precedentemente chiusa in Draw (che sta nel blocco ELSE).
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Rilascia i buffer dello swapchain e lo depth-stencil buffer in quanto devono essere ricreati
	// con le dimensioni aggiornate.
	// In D3D12 la gestione delle risorse è a carico del programmatore e, durante le fasi di disegno,
	// servirà fare riferimento sia ai buffer dello swapchain sia al depth-stencil buffer, quindi:
	// mSwapChainBuffer è un array che contiene i back buffer dello swapchain.
	// mDepthStencilBuffer conserva il depth-stencil buffer.
	for (int i = 0; i < SwapChainBufferCount; ++i)
		mSwapChainBuffer[i].Reset();
    mDepthStencilBuffer.Reset();
	
	// Per lo swapchain è sufficiente ridimensionare tutti i buffer in un colpo solo con ResizeBuffers.
    ThrowIfFailed(mSwapChain->ResizeBuffers(
		SwapChainBufferCount, 
		mClientWidth, mClientHeight, 
		mBackBufferFormat, 
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	// mCurrBackBuffer è l'indice del back buffer corrente (in mSwapChainBuffer), cioè
	// quello usato come render target per comporre il frame corrente (si legga il 
	// paragrafo sotto il listato di codice per maggiori info). 
	// Si riparte da capo quindi indice viene impostato a 0.
	mCurrBackBuffer = 0;
 
	// CreateRenderTargetView crea descriptor/RTV ai buffer dello swapchain e li mette
	// su un dato heap, ad una certa posizione, in base all'handle passato come ultimo param.
	// GetCPUDescriptorHandleForHeapStart restituisce un handle al primo descriptor in heap.
	// Usa wrapper CD3DX12 per poter usare la funzione di supporto Offset ed arrivare così
	// ad ottenere handle di altri descriptor nell'heap.
	// Il termine CPU specifica che tale handle verrà usato sulla timeline della CPU 
	// (cioè non usato in comandi registrati sulla command list)
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
		md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}

    // Crea depth/stencil buffer (con le dimensioni aggiornate) e la relativa DSV/descriptor.
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = mClientWidth;
    depthStencilDesc.Height = mClientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;

	// Di regola sarebbe DXGI_FORMAT_D24_UNORM_S8_UINT ma a volte è necessario collegare 
	// una seconda vista/descriptor al depth buffer (magari una SRV per 
	// leggerne i valori). 
	// Quando che si possono avere viste diverse alla stessa risorsa è necessario creare
	// la risorsa come TYPELESS e specificare il formato nella vista/descriptor.
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

    depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE; // per togliere warning se non usi depth-stencil in shader

	// Specifica come "ripulire" una risorsa, in questo caso il depth-stencil buffer.
	// Se i valori iniziali passati a ClearDepthStencilView, per inizializzre/ripulire il 
	// depth-stencil buffer, coincidono con quelli forniti in D3D12_CLEAR_VALUE allora 
	// tale operazione sarà più rapida.
    D3D12_CLEAR_VALUE optClear;
    optClear.Format = mDepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;

	// Alloca memoria sulla GPU per il depth-stencil buffer.
	// Lo fa su heap di default in quanto CPU non ha necessità di accedere a tale risorsa.
	// Lo stato iniziale indicato è COMMON, che è quello comunemente specificato quando si 
	// crea una texture, prima che venga utilizzata.
	// Usa wrapper CD3DX12 per evitare di dover creare D3D12_HEAP_PROPERTIES esplicitamente.
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

	// Crea una DSV/descriptor per il livello mipmap 0 (l'unico presente) della texture che 
	// rappresenta il depth-stencil buffer.
	// Come formato specifica mDepthStencilFormat (DXGI_FORMAT_D24_UNORM_S8_UINT).
	// DepthStencilView invoca semplicemente GetCPUDescriptorHandleForHeapStart, che restituisce 
	// handle a primo descriptor nell'heap (l'unico presente).
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
    md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	// Cambia stato da COMMON a DEPTH_WRITE, che indica lo stato di una risorsa che verra usata
	// come depth-stencil buffer.
	// Usa wrapper CD3DX12 per poter usare la funzione di supporto Transition, che evita di creare
	// esplicitamente un oggetto D3D12_RESOURCE_BARRIER al solo fine di impostarne i campi.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
	
	// La transizione di stato nelle risorse avviene su timeline della GPU quindi è necessario
	// inviare il comando alla coda (inviando cioè la command list che lo contiene).
    // Ricordarsi sempre di chiudere la command list prima.
	// ExecuteCommandLists prende un array di command list.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Attende finché il comando della transizione di stato non viene eseguito.
	FlushCommandQueue();

	// Aggiorna il viewport con le dimensioni aggiornate.
	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width    = static_cast<float>(mClientWidth);
	mScreenViewport.Height   = static_cast<float>(mClientHeight);
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;

	// Aggiorna lo scissor con le dimensioni aggiornate.
	// Per scissor si intende un'istanza di D3D12_RECT che definisce un rettangolo sul back
	// buffer al di fuori del quale il processo di rendering viene bloccato. In altre parole,
	// i vertici che cadono al di fuori di tale rettangolo non proseguono il loro cammino
	// dopo la fase di rasterizzazione. Di solito è usato per ottimizzare le performance se 
	// si sa in anticipo che il rendering è limitato ad una zona del back buffer (ad esempio, 
	// nel disegno di elementi per l'interfaccia utente, tipo pulsanti, testo, ecc.).
	// Si può impostare uno scissor anche in D3D11 ma in D3D12 è quasi obbligatorio farlo
	// perché se si usa Reset sulla command list è necessario reimpostare viewport e scissor.
	// Infatti, anche se viewport e scissor non fanno parte del PSO, sono comunque elementi
	// dello stato della pipeline. A tal proposito, quando si reimposta una command list
	// diretta, lo stato della pipeline non viene ereditato e deve quindi essere reimpostato. 
	// Al contrario, verrà usato uno stato con valori di default.
    mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}
 
LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch( msg )
	{
	// WM_ACTIVATE is sent when the window is activated or deactivated.  
	// We pause the game when the window is deactivated and unpause it 
	// when it becomes active.  
	case WM_ACTIVATE:
		if( LOWORD(wParam) == WA_INACTIVE )
		{
			mAppPaused = true;
			mTimer.Stop();
		}
		else
		{
			mAppPaused = false;
			mTimer.Start();
		}
		return 0;

	// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
		// Save the new client area dimensions.
		mClientWidth  = LOWORD(lParam);
		mClientHeight = HIWORD(lParam);
		if( md3dDevice )
		{
			if( wParam == SIZE_MINIMIZED )
			{
				mAppPaused = true;
				mMinimized = true;
				mMaximized = false;
			}
			else if( wParam == SIZE_MAXIMIZED )
			{
				mAppPaused = false;
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if( wParam == SIZE_RESTORED )
			{
				
				// Restoring from minimized state?
				if( mMinimized )
				{
					mAppPaused = false;
					mMinimized = false;
					OnResize();
				}

				// Restoring from maximized state?
				else if( mMaximized )
				{
					mAppPaused = false;
					mMaximized = false;
					OnResize();
				}
				else if( mResizing )
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					OnResize();
				}
			}
		}
		return 0;

	// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		mAppPaused = true;
		mResizing  = true;
		mTimer.Stop();
		return 0;

	// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
	// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		mAppPaused = false;
		mResizing  = false;
		mTimer.Start();
		OnResize();
		return 0;
 
	// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	// The WM_MENUCHAR message is sent when a menu is active and the user presses 
	// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
        // Don't beep when we alt-enter.
        return MAKELRESULT(0, MNC_CLOSE);

	// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200; 
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
    case WM_KEYUP:
        if(wParam == VK_ESCAPE)
        {
            PostQuitMessage(0);
        }
        else if((int)wParam == VK_F2)
            Set4xMsaaState(!m4xMsaaState);

        return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool D3DApp::InitMainWindow()
{
	WNDCLASS wc;
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = MainWndProc; 
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = mhAppInst;
	wc.hIcon         = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName  = 0;
	wc.lpszClassName = L"MainWnd";

	if( !RegisterClass(&wc) )
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, mClientWidth, mClientHeight };
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width  = R.right - R.left;
	int height = R.bottom - R.top;

	mhMainWnd = CreateWindow(L"MainWnd", mMainWndCaption.c_str(), 
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, mhAppInst, 0); 
	if( !mhMainWnd )
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	ShowWindow(mhMainWnd, SW_SHOW);
	UpdateWindow(mhMainWnd);

	return true;
}

bool D3DApp::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG) 
	// Abilita il debug layer di D3D12.
	// Se si compila il progetto in modalità Debug verranno visualizzati messaggi 
	// di debug nella finestra di output di Visual Studio
{
	//ComPtr<ID3D12Debug5> debugController; // mi sa che richiede aggiornamento dell'sdk
	ComPtr<ID3D12Debug> debugController;
	//ComPtr<ID3D12Debug1> debugController1;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
	//ThrowIfFailed(debugController->QueryInterface(IID_PPV_ARGS(&debugController1)));
	debugController->EnableDebugLayer();
	//debugController1->SetEnableGPUBasedValidation(true);
	//debugController->SetEnableAutoDebugName(TRUE);
}
#endif

	// Crea oggetto IDXGIFactory.
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

	// Prima tenta di creare un device per una scheda video hardware.
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,             // default adapter
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&md3dDevice));

	// Altrimenti prova a creare un device al WARP.
	if(FAILED(hardwareResult))
	{
		ComPtr<IDXGIAdapter> pWarpAdapter;
		ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&md3dDevice)));
	}

	// Crea un oggetto fence per la sincronizzazione tra CPU e GPU.
	ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&mFence)));

	// Per accedere ai descriptor in descriptor heap, alcuni metodi di supporto usano aritmentica 
	// dei puntatori e quindi serve passare dimensione di descriptor come param. 
	// Tale informazione deve essere richiesta al device poiché cambia da GPU a GPU.
	mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Controlla livelli di qualità in MSAA 4X.
	// Tutte le schede che supportano D3D_FEATURE_LEVEL_11_0 sono in grado di offrire 
	// MSAA 4X quindi non resta che controllare quali sono i relativi livelli di qualità.
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = mBackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(md3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

    m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");
	
#ifdef _DEBUG
    LogAdapters();
#endif

	CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();

	return true;
}

void D3DApp::CreateCommandObjects()
{
	// Crea command queue a cui verranno inviate command list di tipo diretto.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

	// Crea command allocator che gestirà memoria di command list di tipo diretto.
	ThrowIfFailed(md3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

	// Crea command list di tipo diretto.
	ThrowIfFailed(md3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		mDirectCmdListAlloc.Get(), // Command allocator associato a questa command list
		nullptr,                   // PSO iniziale associato a questa command list
		IID_PPV_ARGS(mCommandList.GetAddressOf())));

	// Chiude la command list perché la prima volta che si fa riferimento ad una command 
	// list (anche quando in realtà si vuole riutilizzarla dopo che è stata inviata alla coda) 
	// è bene invocare Reset per portarla allo stato iniziale: a tale scopo è necessario 
	// che la command list sia chiusa.
	mCommandList->Close();
}

void D3DApp::CreateSwapChain()
{

	// Rilascia nel caso si voglia ricreare lo swapchain.
	// Meglio usare sempre questa funzione per non duplicare codice.
    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd;
    sd.BufferDesc.Width = mClientWidth;
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = mhMainWnd;
    sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Per creare lo swapchain, dietro le quinte, viene usata una command list per registrare
	// alcuni comandi di cambio stato per le risorse coinvolte (si vedrà la cosa in maniera 
	// esplicita a breve, quando si parlerà della creazione del depth-stencil buffer) e quindi 
	// viene passata la coda in modo da accodarvi tale command list.
    ThrowIfFailed(mdxgiFactory->CreateSwapChain(
		mCommandQueue.Get(),
		&sd, 
		mSwapChain.GetAddressOf()));

	//DXGISetDebugObjectName(mSwapChain.Get(), "SwapChain");
}

void D3DApp::FlushCommandQueue()
{
	// Aumenta, lato CPU, il valore della fence.
    mCurrentFence++;

	// Signal aggiunge una fence alla fine della coda con il valore passato
	// come secondo param. Poiché la timeline è quella della GPU non si incontrerà
	// tale fence finché non verranno prima eseguiti tutti i comandi precedenti
	// alla fence inserita con Signal.
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

	// In questo blocco IF si attende finché la GPU completa tutti i comandi fino alla 
	// fence aggiunta con Signal all'istruzione precedente.
	// GetCompletedValue ritorna il valore dell'ultima fence incontrata dalla GPU nella coda.
	// Quindi se tale valore è >= al valore lato CPU che ha la fence, vuol dire che tutti i
	// comandi sono già stati eseguiti ed è inutile aspettare.
    if(mFence->GetCompletedValue() < mCurrentFence)
	{
		// Crea un evento da generare quando la GPU incontrerà la fence nella coda.
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		// Imposta, sulla fence, l'evento da generare quando la GPU incontrerà, nella coda,
		// la fence, nel caso in cui questa avesse un preciso valore (passato come secondo param).
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

		// Si mette in attesa sull'evento creato, che verrà generato quando la GPU incontrerà
		// la fence nella coda con il valore uguale a quello aggiornato lato CPU.
		WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
	}
}

ID3D12Resource* D3DApp::CurrentBackBuffer()const
{
	return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView()const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrBackBuffer,
		mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView()const
{
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3DApp::CalculateFrameStats()
{
	// Code computes the average frames per second, and also the 
	// average time it takes to render one frame.  These stats 
	// are appended to the window caption bar.
    
	static int frameCnt = 0;
	static float timeElapsed = 0.0f;

	frameCnt++;

	// Compute averages over one second period.
	if( (mTimer.TotalTime() - timeElapsed) >= 1.0f )
	{
		float fps = (float)frameCnt; // fps = frameCnt / 1
		float mspf = 1000.0f / fps;

        wstring fpsStr = to_wstring(fps);
        wstring mspfStr = to_wstring(mspf);

        wstring windowText = mMainWndCaption +
            L"    fps: " + fpsStr +
            L"   mspf: " + mspfStr;

        SetWindowText(mhMainWnd, windowText.c_str());
		
		// Reset for next average.
		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}

void D3DApp::LogAdapters()
{
    UINT i = 0;
    IDXGIAdapter* adapter = nullptr;
    std::vector<IDXGIAdapter*> adapterList;
    while(mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC desc;
        adapter->GetDesc(&desc);

        std::wstring text = L"***Adapter: ";
        text += desc.Description;
        text += L"\n";

        OutputDebugString(text.c_str());

        adapterList.push_back(adapter);
        
        ++i;
    }

    for(size_t i = 0; i < adapterList.size(); ++i)
    {
        LogAdapterOutputs(adapterList[i]);
        ReleaseCom(adapterList[i]);
    }
}

void D3DApp::LogAdapterOutputs(IDXGIAdapter* adapter)
{
    UINT i = 0;
    IDXGIOutput* output = nullptr;
    while(adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);
        
        std::wstring text = L"***Output: ";
        text += desc.DeviceName;
        text += L"\n";
        OutputDebugString(text.c_str());

        LogOutputDisplayModes(output, mBackBufferFormat);

        ReleaseCom(output);

        ++i;
    }
}

void D3DApp::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
{
    UINT count = 0;
    UINT flags = 0;

    // Call with nullptr to get list count.
    output->GetDisplayModeList(format, flags, &count, nullptr);

    std::vector<DXGI_MODE_DESC> modeList(count);
    output->GetDisplayModeList(format, flags, &count, &modeList[0]);

    for(auto& x : modeList)
    {
        UINT n = x.RefreshRate.Numerator;
        UINT d = x.RefreshRate.Denominator;
        std::wstring text =
            L"Width = " + std::to_wstring(x.Width) + L" " +
            L"Height = " + std::to_wstring(x.Height) + L" " +
            L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
            L"\n";

        ::OutputDebugString(text.c_str());
    }
}