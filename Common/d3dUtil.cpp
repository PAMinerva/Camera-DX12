
#include "d3dUtil.h"
#include <comdef.h>
#include <fstream>

using Microsoft::WRL::ComPtr;

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
    ErrorCode(hr),
    FunctionName(functionName),
    Filename(filename),
    LineNumber(lineNumber)
{
}

bool d3dUtil::IsKeyDown(int vkeyCode)
{
    return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}

ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
    std::ifstream fin(filename, std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int)fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

    fin.read((char*)blob->GetBufferPointer(), size);
    fin.close();

    return blob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;

    // Crea la risorsa sull'heap di default.
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    // Se si vogliono scrivere dati forniti dall'applicazionea nella risorsa sull'heap 
    // di default è necessario creare una risorsa intermedia sull'heap di upload,
    // accessibile dalla CPU.
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


    // Descrive i dati che si vogliono copiare nella risorsa intermedia
    // (in attesa di essere poi copiati da questo a quello sull'heap di default).
    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = byteSize;
    subResourceData.SlicePitch = subResourceData.RowPitch;

    // Cambio di stato necessario per la risorsa sull'heap di default prima di copiare 
    // i dati presi dalla risorsa intermedia (poiché cambia l'uso che ne fa la GPU).
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(), 
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

    // UpdateSubresources prima copia i dati forniti dall'applicazione nella risorsa intermedia 
    // tramite Map/Unmap e poi, in base al tipo di risorsa, registra CopyTextureRegion o 
    // CopyBufferRegion nella command list per disporre la copia dei dati nel buffer 
    // sull'heap di default.
    // Un po' come accadeva in DX11 dove con D3D11_USAGE_DYNAMIC si poteva usare Map/Unmap 
    // mentre con D3D11_USAGE_DEFAULT si usava UpdateSubresource, che creava ed usava un buffer 
    // intermedio e su questo veniva usato Map/Unmap prima di copiare il risultato dal buffer 
    // intermedio a quello destinazione.
    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

    // Risorsa torna a stato precedente.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    // Nota: dopo l'invocazione di questa funzione è necessario mantenere il riferimento
    // al param. uploadBuffer perché la command list è ancora aperta ed i suoi comandi non 
    // ancora eseguiti (compreso quello di copia che fa riferimento a tale risorsa).
    // Una volta che la command list viene chiusa e passata alla coda e si ha la certezza 
    // che tutti i suoi comandi sono stati eseguiti allora è possibile rilasciare uploadBuffer 
    // dato che si è sicuri che il comando di copia è stato eseguito e non ci sono più 
    // comandi che fanno riferimento alla risorsa intermedia.

    return defaultBuffer;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if(errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}

std::wstring DxException::ToString()const
{
    // Get the string description of the error code.
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

#ifndef GRAPHICS_DEBUGGER_OBJECT_NAME
#define GRAPHICS_DEBUGGER_OBJECT_NAME (1)
#endif

// Per tutti gli oggetti creati dal device (in pratica qualsiasi cosa sia passato come 
// parametro di output ad uno dei suoi metodi di creazione) si può usare SetName (che
// funziona tipo il nostro vecchio metodo di supporto, D3D11SetDebugObjectName.
// Nota: se vuoi implementare D3D12SetDebugObjectName da solo ricorda che GUID
// è WKPDID_D3DDebugObjectNameW. La W alla fine indica che la stringa deve essere unicode.
template<UINT TNameLength>
inline void DXGISetDebugObjectName(_In_ IDXGIObject* resource, _In_ const char(&name)[TNameLength])
{
#if (defined(DEBUG) || defined(_DEBUG)) && (GRAPHICS_DEBUGGER_OBJECT_NAME)
    resource->SetPrivateData(WKPDID_D3DDebugObjectName, TNameLength - 1, name);
#else
    UNREFERENCED_PARAMETER(resource);
    UNREFERENCED_PARAMETER(name);
#endif
}