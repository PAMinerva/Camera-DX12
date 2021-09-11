#pragma once

#include "d3dUtil.h"

template<typename T>
class UploadBuffer
{
public:
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) : 
        mIsConstantBuffer(isConstantBuffer)
    {
        mElementByteSize = sizeof(T);

        // Nella documentazione si richiede che i constant buffer siano allineati a 256 byte.
        // Questo non sarebbe un problema se si volesse creare un solo CB perché ci pensa
        // CreateCommittedResource ad assicurare tale limitazione, allocando spazio sulla
        // GPU ad un indirizzo appropriato (si veda campo alignment di D3D12_RESOURCE_DESC). 
        // Se però si vogliono creare più CB (dello stesso tipo), disponendoli in modo contiguo, 
        // è necessario arrotondare la loro dimensione ad un multiplo di 256.
        //
        // typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
        // UINT64 OffsetInBytes; // multiplo di 256
        // UINT   SizeInBytes;   // multiplo di 256
        // } D3D12_CONSTANT_BUFFER_VIEW_DESC;
        //
        if(isConstantBuffer)
            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));

        // Usa wrapper CD3DX12 per evitare di dover creare D3D12_HEAP_PROPERTIES e
        // D3D12_RESOURCE_DESC esplicitamente.
        // RESOURCE STATE indica lo stato della risorsa dal punto di vista della GPU
        // quindi STATE_GENERIC_READ indica che la GPU leggerà da questa risorsa. 
        // La CPU ha accesso usando Map/Unmap dato che la risorsa è sull'heap di UPLOAD.
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize*elementCount),
			D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer)));

        // Mappa la risorsa dalla memoria GPU a quella della CPU.
        // L'implementazione di Map/Unmap è cambiata da D3D11 a D3D12.
        // In D3D11 Map serve sia a mappare una risorsa dalla memoria della GPU a quella della CPU 
        // che ad impedire alla GPU l'accesso ad essa (dato che la CPU può modificarla). 
        // In D3D12 la sincronizzazione tra CPU e GPU è a carico del programmatore quindi la risorsa 
        // può restare mappata permanentemente (non è necessario invocare Unmap) fino a quando si
        // ritiene opportuno (cioè fino a quando ci sono operazioni da fare sulla risorsa lato CPU).
        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));
    }

    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
    ~UploadBuffer()
    {
        if(mUploadBuffer != nullptr)
            mUploadBuffer->Unmap(0, nullptr);

        mMappedData = nullptr;
    }

    ID3D12Resource* Resource()const
    {
        return mUploadBuffer.Get();
    }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex*mElementByteSize], &data, sizeof(T));
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
    BYTE* mMappedData = nullptr;

    UINT mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};