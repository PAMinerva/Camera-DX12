//***************************************************************************************
// d3dUtil.h by Frank Luna (C) 2015 All Rights Reserved.
//
// General helper code.
//***************************************************************************************

#pragma once

#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cassert>
#include "d3dx12.h"
#include "DDSTextureLoader.h"
#include "MathHelper.h"

extern const int gNumFrameResources;

inline void d3dSetDebugName(IDXGIObject* obj, const char* name)
{
    if(obj)
    {
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
    }
}
inline void d3dSetDebugName(ID3D12Device* obj, const char* name)
{
    if(obj)
    {
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
    }
}
inline void d3dSetDebugName(ID3D12DeviceChild* obj, const char* name)
{
    if(obj)
    {
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
    }
}

inline std::wstring AnsiToWString(const std::string& str)
{
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
    return std::wstring(buffer);
}

/*
#if defined(_DEBUG)
    #ifndef Assert
    #define Assert(x, description)                                  \
    {                                                               \
        static bool ignoreAssert = false;                           \
        if(!ignoreAssert && !(x))                                   \
        {                                                           \
            Debug::AssertResult result = Debug::ShowAssertDialog(   \
            (L#x), description, AnsiToWString(__FILE__), __LINE__); \
        if(result == Debug::AssertIgnore)                           \
        {                                                           \
            ignoreAssert = true;                                    \
        }                                                           \
                    else if(result == Debug::AssertBreak)           \
        {                                                           \
            __debugbreak();                                         \
        }                                                           \
        }                                                           \
    }
    #endif
#else
    #ifndef Assert
    #define Assert(x, description) 
    #endif
#endif 		
    */

class d3dUtil
{
public:

    static bool IsKeyDown(int vkeyCode);

    static std::string ToString(HRESULT hr);

    static UINT CalcConstantBufferByteSize(UINT byteSize)
    {
        // I constant buffer devono avere allineamento a 256 byte quindi se si 
        // si vogliono creare più CB (dello stesso tipo) disponendoli in modo contiguo 
        // è necessario arrotondare la loro dimensione ad un multiplo di 256.
        // A tale scopo si può sommare 255 alla dimensione non ancora arrotondata
        // ed in seguito azzerare il byte meno significativo, che serve per i valori < 256
        // Ad esempio: Supponiamo che byteSize = 300.
        // (300 + 255) & ~255
        // 555 & ~255
        // 0x022B & ~0x00ff
        // 0x022B & 0xff00
        // 0x0200
        // 512
        return (byteSize + 255) & ~255;
    }

    static Microsoft::WRL::ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);

    static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const void* initData,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);

	static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);
};

class DxException
{
public:
    DxException() = default;
    DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

    std::wstring ToString()const;

    HRESULT ErrorCode = S_OK;
    std::wstring FunctionName;
    std::wstring Filename;
    int LineNumber = -1;
};

// Definisce una parte (Submesh) di una geometria completa (MeshGeometry).
// Utile sopratutto quando si hanno geometrie multiple all'interno dello stesso
// vertex buffer (e che condividono eventualmente anche lo stesso index buffer).
// Fornisce gli offset ed i dati necessari a disegnare la parte di geometria
// conservata nel vertex ed index buffer condivisi da tutte le parti che compongono
// la geometria completa.
struct SubmeshGeometry
{
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;

    // Bounding box della submesh.
	DirectX::BoundingBox Bounds;
};

struct MeshGeometry
{
	// Stringa con cui si può ricercare la geometria se conservata in una hash table/map.
	std::string Name;

    // Copie degli array di vertici ed indici.
    // ID3DBlob è neutrale rispetto alla versione di Direct3D e non viene usato solo per 
    // conservare codice oggetto durante compilazione di shader ma anche come buffer di 
    // dati tipizzati: ad esempio vertex o index buffer costruiti dall'applicazione. 
    // Poi sta al programmatore effettuare i cast opportuni.
	Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU  = nullptr;

    // Riferimenti a risorse GPU (vertex ed index buffer su heap di default)
	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

    // Riferimenti a risorse GPU (vertex ed index buffer intermedi, cioè su heap di upload)
	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

    // Info utili riguardo vertex ed index buffer che contengono e descrivono la geometria.
	UINT VertexByteStride = 0;
	UINT VertexBufferByteSize = 0;
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
	UINT IndexBufferByteSize = 0;

	// Una MeshGeometry può essere composta da diverse SubmeshGeometry (che condividono
    // lo stesso vertex/index buffer).
	// Questa map verrà usata per disegnare le varie Submesh individualmente.
	std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

    // Restituisce indirizzo virtuale ed altre info di VB in GPU
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
		vbv.StrideInBytes = VertexByteStride;
		vbv.SizeInBytes = VertexBufferByteSize;

		return vbv;
	}

    // Restituisce indirizzo virtuale ed altre info di IB in GPU
	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
	{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = IndexBufferByteSize;

		return ibv;
	}

    // Elimina riferimenti alle risorse intermedie.
	void DisposeUploaders()
	{
		VertexBufferUploader = nullptr;
		IndexBufferUploader = nullptr;
	}
};

struct Light
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };  // Intensità (RGB perché luce può essere colorata)
    float FalloffStart = 1.0f;                          // Per luci puntiformi e riflettori
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// Per luci direzionali e riflettori
    float FalloffEnd = 10.0f;                           // Per luci puntiformi e riflettori
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // Per luci puntiformi e riflettori
    float SpotPower = 64.0f;                            // Per riflettori
};

#define MaxLights 16

// Struttura che rappresenta (lato CPU) il CB del materiale.
struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f; // (1-Roughness) = levigatezza

    // Matrice usata per modificare le coordinate texture quando si vuole creare
    // un'animazione della texture.
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

// Struttura per gestire il materiale nell'applicazione.
struct Material
{
    // Stringa con cui si può ricercare il materiale se conservato in una hash table/map.
	std::string Name;

	// Indice (all'interno del relativo array) del constant buffer corrispondente a 
    // questo materiale.
    // Ogni frame deve avere la sua copia di tutti i materiali da usare nel rendering
    // degli oggetti perché è possibile modificare i dati nei relativi constant buffer,
    // che dunque possono cambiare da un frame all'altro.
	int MatCBIndex = -1;

	// Indice (all'interno del relativo heap descriptor) della SRV alla texture che 
    // rappresenta la componente diffusa del materiale.
	int DiffuseSrvHeapIndex = -1;

    // Indice (all'interno del relativo heap descriptor) della SRV alla texture che 
    // conserva le normali dell'oggetto a cui si applica il materiale
	// (l'argomento verrà approfondito in una prossima lezione).
	int NormalSrvHeapIndex = -1;

    // Variabile che indica se e quanti constant buffer bisogna aggiornare in caso 
    // il materiale subisca una modifica. 
    // Dato che ci sono più frame ed ognuno ha la sua copia di questo materiale 
    // come constant buffer, è necessario aggiornarli tutti.
    // In questo modo, ogni volta che si modifica un materiale bisogna anche impostare
    // NumFramesDirty = gNumFrameResources così che ogni frame ha la sua copia del
    // relativo constant buffer aggiornata.
	int NumFramesDirty = gNumFrameResources;

    // Dati lato CPU da passare al constant buffer corrispondente a questo materiale.
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = .25f;
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

struct Texture
{
    // Stringa con cui si può ricercare la texture se conservata in una hash table/map.
	std::string Name;

    // Percorso del file DDS
	std::wstring Filename;

    // Risorsa intermedia (in upload heap) in cui caricare i dati dal file DDS.
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;

    // Risorsa (in default heap) in cui copiare i dati da risorsa intermedia.
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
};

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif

#ifndef ReleaseCom
#define ReleaseCom(x) { if(x){ x->Release(); x = 0; } }
#endif