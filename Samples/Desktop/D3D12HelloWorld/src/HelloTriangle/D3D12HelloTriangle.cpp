//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12HelloTriangle.h"

D3D12HelloTriangle::D3D12HelloTriangle(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_rtvDescriptorSize(0),
    m_srvUavDescriptorSize(0),
    m_fenceValue(0),
    m_fenceEvent(nullptr),
    m_smpDescriptorSize(0),
    m_feedbackMapFormat(DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE)
{
}

void D3D12HelloTriangle::OnInit()
{
    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12HelloTriangle::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_12_2,
            IID_PPV_ARGS(&m_device)
        ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_12_2,
            IID_PPV_ARGS(&m_device)
        ));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        // Describe and create a shader resource view (SRV) heap for the texture.
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 4;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

        D3D12_DESCRIPTOR_HEAP_DESC smpHeapDsc = {};
        smpHeapDsc.NumDescriptors = 1;
        smpHeapDsc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        smpHeapDsc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&smpHeapDsc, IID_PPV_ARGS(&m_smpHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_srvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_smpDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

inline std::vector<uint8_t> ReadData(const wchar_t* name)
{
    std::ifstream inFile(name, std::ios::in | std::ios::binary | std::ios::ate);

    if (!inFile)
        throw std::runtime_error("ReadData");

    const std::streampos len = inFile.tellg();
    if (!inFile)
        throw std::runtime_error("ReadData");

    std::vector<uint8_t> blob;
    blob.resize(size_t(len));

    inFile.seekg(0, std::ios::beg);
    if (!inFile)
        throw std::runtime_error("ReadData");

    inFile.read(reinterpret_cast<char*>(blob.data()), len);
    if (!inFile)
        throw std::runtime_error("ReadData");

    inFile.close();

    return blob;
}

void D3D12HelloTriangle::ClearFeedbackMap()
{

}

// Load the sample assets.
void D3D12HelloTriangle::LoadAssets()
{

    // create command allocator and command list
    {
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocator.ReleaseAndGetAddressOf())));
        m_commandAllocator->SetName(L"Compute Generated Texture");

        ThrowIfFailed(
            m_device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                m_commandAllocator.Get(),
                m_pipelineState.Get(),
                IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf()))
        );
        m_commandList->SetName(L"Command List");
    }

    // create output uav (u0)
    {
        // Describe and create a Texture2D.
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDesc.Width = m_width;
        textureDesc.Height = m_height;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ThrowIfFailed(
            m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&m_outputTexture)));
        m_outputTexture->SetName(L"Output UAV (register : u1)");

        CD3DX12_CPU_DESCRIPTOR_HANDLE uavCpuHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_srvUavDescriptorSize);

        m_device->CreateUnorderedAccessView(m_outputTexture.Get(), nullptr, nullptr, uavCpuHandle);
    }

    ComPtr<ID3D12Resource> textureUploadHeap;
    DXGI_FORMAT inputTextureFormat;
    TexMetadata info;
    FLOAT imageMaxLOD;
    // create input texture (t0)
    {
        auto image = std::make_unique<ScratchImage>();

        ThrowIfFailed(
            LoadFromDDSFile(L"MyFile3.dds", DDS_FLAGS_NONE, &info, *image)
        );

        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        ThrowIfFailed(
            PrepareUpload(m_device.Get(), image->GetImages(), image->GetImageCount(), info, subresources)
        );

        m_width = info.width;
        m_height = info.height;
        inputTextureFormat = info.format;
        imageMaxLOD = static_cast<FLOAT>(info.mipLevels);

        // Describe and create a Texture2D.
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = info.mipLevels;
        textureDesc.Format = info.format;
        textureDesc.Width = info.width;
        textureDesc.Height = info.height;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ThrowIfFailed(
            m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_inputTexture)));

        m_inputTexture->SetName(L"Input Texture (t0)");

        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_inputTexture.Get(), 0, static_cast<unsigned int>(subresources.size()));
        ThrowIfFailed(
            m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(textureUploadHeap.GetAddressOf())));

        UpdateSubresources(
            m_commandList.Get(),
            m_inputTexture.Get(),
            textureUploadHeap.Get(),
            0,
            0,
            static_cast<unsigned int>(subresources.size()),
            subresources.data());

        D3D12_RESOURCE_BARRIER resourceBarrierDstToResource = {};
        resourceBarrierDstToResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        resourceBarrierDstToResource.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        resourceBarrierDstToResource.Transition.pResource = m_inputTexture.Get();
        resourceBarrierDstToResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        resourceBarrierDstToResource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        resourceBarrierDstToResource.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        m_commandList->ResourceBarrier(1, &resourceBarrierDstToResource);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = inputTextureFormat;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = imageMaxLOD;

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvCpuHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), 2, m_srvUavDescriptorSize);
        m_device->CreateShaderResourceView(m_inputTexture.Get(), &srvDesc, srvCpuHandle);
    }

    // ComPtr<ID3D12Resource> residencyMapUploadHeap;
    UINT minMipWidth = 0;
    UINT minMipHeight = 0;
    // create resident min mip texture (t1)
    {
        UINT bytesPerPixel = BitsPerPixel(inputTextureFormat) >> 3;

        DOUBLE sqWidth = m_width * m_width * bytesPerPixel;
        DOUBLE sqHeight = m_height * m_height * bytesPerPixel;

        // Assume 64KB tiles
        // For each 64KB tile in the input texture, we'll have one texel in the min mip map
        // Instead of dividing by 256, we'll shift right by 8
        minMipWidth = static_cast<UINT>(ceil(sqrt(sqWidth))) >> m_shift;
        minMipHeight = static_cast<UINT>(ceil(sqrt(sqHeight))) >> m_shift;

        DXGI_FORMAT minMipFormat = DXGI_FORMAT_R8_UINT;

        // Describe and create a Texture2D.
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = minMipFormat;
        textureDesc.Width = minMipWidth;
        textureDesc.Height = minMipHeight;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ThrowIfFailed(textureDesc.Width > 0 && textureDesc.Height > 0);

        UINT minMipSize = minMipHeight * minMipWidth;
        m_residencyMapData = std::vector<UINT8>(minMipSize, 0);

        ThrowIfFailed(
            m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_residencyMap)));

        ThrowIfFailed(
            m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(m_residencyMapData.capacity() * sizeof(UINT8)),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(textureUploadHeap.GetAddressOf())));

        D3D12_SUBRESOURCE_DATA minMipSubresource = {};
        minMipSubresource.pData = m_residencyMapData.data();
        minMipSubresource.RowPitch = minMipWidth * (BitsPerPixel(minMipFormat) >> 3);
        minMipSubresource.SlicePitch = minMipSubresource.RowPitch * minMipHeight;

        UpdateSubresources(
            m_commandList.Get(),
            m_residencyMap.Get(),
            textureUploadHeap.Get(),
            0, 0, 1,
            &minMipSubresource);

        D3D12_RESOURCE_BARRIER resourceBarrierDstToResource = {};
        resourceBarrierDstToResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        resourceBarrierDstToResource.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        resourceBarrierDstToResource.Transition.pResource = m_residencyMap.Get();
        resourceBarrierDstToResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        resourceBarrierDstToResource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        resourceBarrierDstToResource.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        m_commandList->ResourceBarrier(1, &resourceBarrierDstToResource);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvCpuHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), 3, m_srvUavDescriptorSize);
        m_device->CreateShaderResourceView(m_residencyMap.Get(), &srvDesc, srvCpuHandle);
    }

    // create sampler (s0)
    {
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MipLODBias = 0;
        samplerDesc.MaxAnisotropy = 0;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        *samplerDesc.BorderColor = { 0.0 };
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = imageMaxLOD - 1;

        CD3DX12_CPU_DESCRIPTOR_HANDLE smpHandle(m_smpHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_smpDescriptorSize);
        m_device->CreateSampler(&samplerDesc, smpHandle);
    }

    // create feedback map texture (u1)
    {
        D3D12_RESOURCE_DESC1 feedbackResourceDesc = {};
        // Populate some fields directly from the paired resource.
        feedbackResourceDesc.Width = info.width;
        feedbackResourceDesc.Height = info.height;
        feedbackResourceDesc.MipLevels = info.mipLevels;
        feedbackResourceDesc.DepthOrArraySize = info.arraySize;

        // Populate fields based on usage constraint
        feedbackResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        feedbackResourceDesc.SampleDesc.Count = 1;
        feedbackResourceDesc.SampleDesc.Quality = 0;
        feedbackResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        feedbackResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // Set format based on desired sampler feedback type
        feedbackResourceDesc.Format = m_feedbackMapFormat;

        // Set desired mip region
        // The Depth field of D3D12_MIP_REGION exists for future proofing and extension purposes.
        // In the implementation described in this document, applications always set Depth to 0,
        // or equivalently to 1 when sampler feedback is used.
        feedbackResourceDesc.SamplerFeedbackMipRegion = m_tileDimensions;

        m_device->CreateCommittedResource2(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &feedbackResourceDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, // clear value
            nullptr,
            IID_PPV_ARGS(m_feedbackMap.ReleaseAndGetAddressOf()));

        CD3DX12_CPU_DESCRIPTOR_HANDLE feedbackMapCpuHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), 1, m_srvUavDescriptorSize);
        m_device->CreateSamplerFeedbackUnorderedAccessView(m_inputTexture.Get(), m_feedbackMap.Get(), feedbackMapCpuHandle);
    }

    // create root signature
    {
        CD3DX12_DESCRIPTOR_RANGE1 descRange[3] = {};
        descRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0); // u0, u1
        descRange[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0); // t0, t1
        descRange[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0); // s0

        CD3DX12_ROOT_PARAMETER1 rootParameters[3] = {};
        //rootParameters[0].InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[0].InitAsDescriptorTable(1, &descRange[0], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[1].InitAsDescriptorTable(1, &descRange[1], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[2].InitAsDescriptorTable(1, &descRange[2], D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignature(3, rootParameters);
        //CD3DX12_ROOT_SIGNATURE_DESC rootSignature(_countof(rootParameters), rootParameters);

        ComPtr<ID3DBlob> serializedSignature;
        ThrowIfFailed(
            D3D12SerializeVersionedRootSignature(&rootSignature, serializedSignature.GetAddressOf(), nullptr));

        // Create the root signature
        ThrowIfFailed(
            m_device->CreateRootSignature(
                0,
                serializedSignature->GetBufferPointer(),
                serializedSignature->GetBufferSize(),
                IID_PPV_ARGS(m_rootSignature.ReleaseAndGetAddressOf())));

        m_rootSignature->SetName(L"Compute RS");
    }

    // Compile Compute Shader and Create Compute PSO
    {
        ComPtr<ID3DBlob> computeShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        std::vector<uint8_t> computeShaderData = ReadData(L"ComputeShaderSampFeedback.dxil");
        //ThrowIfFailed(D3DCompileFromFile(L"C:/dev/temp/ComputeShader.hlsl", nullptr, nullptr, "main", "cs_6_0", compileFlags, 0, &computeShader, nullptr));

        // Create compute pipeline state
        D3D12_COMPUTE_PIPELINE_STATE_DESC descComputePSO = {};
        descComputePSO.pRootSignature = m_rootSignature.Get();
        descComputePSO.CS.pShaderBytecode = computeShaderData.data();
        descComputePSO.CS.BytecodeLength = computeShaderData.size();

        ThrowIfFailed(
            m_device->CreateComputePipelineState(&descComputePSO, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf()))
        );

        m_pipelineState->SetName(L"Comptue PSO");
    }

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create Fence between dispatches
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
        WaitForPreviousFrame();
    }
}

// Generate a simple black and white checkerboard texture.
void D3D12HelloTriangle::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
// command lists have finished execution on the GPU; apps should use 
// fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 5
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    ID3D12DescriptorHeap* pHeaps[] = { m_srvHeap.Get(), m_smpHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(pHeaps), pHeaps);
    m_commandList->SetComputeRootSignature(m_rootSignature.Get());

    CD3DX12_GPU_DESCRIPTOR_HANDLE uavGpuHandle(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), 0, m_srvUavDescriptorSize);
    m_commandList->SetComputeRootDescriptorTable(0, uavGpuHandle);

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), 2, m_srvUavDescriptorSize);
    m_commandList->SetComputeRootDescriptorTable(1, srvGpuHandle);

    m_commandList->SetPipelineState(m_pipelineState.Get());
    // for conventiently setting SetComputeRoot32BitConstants in "create root signature" block
    //UINT rootConstants[2] = { m_width, m_height };
    //m_commandList->SetComputeRoot32BitConstants(0, _countof(rootConstants), rootConstants, 0);
    m_commandList->Dispatch(480, 270, 1);

    D3D12_RESOURCE_BARRIER resourceBarrierWaitForUAV = {};
    resourceBarrierWaitForUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    resourceBarrierWaitForUAV.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierWaitForUAV.UAV.pResource = m_outputTexture.Get();

    D3D12_RESOURCE_BARRIER resourceBarrierSetToCpySrc = {};
    resourceBarrierSetToCpySrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierSetToCpySrc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierSetToCpySrc.Transition.pResource = m_outputTexture.Get();
    resourceBarrierSetToCpySrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resourceBarrierSetToCpySrc.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    resourceBarrierSetToCpySrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    D3D12_RESOURCE_BARRIER resourceBarrierSetToCpyDst = {};
    resourceBarrierSetToCpyDst.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierSetToCpyDst.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierSetToCpyDst.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    resourceBarrierSetToCpyDst.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resourceBarrierSetToCpyDst.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    resourceBarrierSetToCpyDst.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

    D3D12_RESOURCE_BARRIER resourceBarrierSetToPresent = {};
    resourceBarrierSetToPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierSetToPresent.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierSetToPresent.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    resourceBarrierSetToPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resourceBarrierSetToPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    resourceBarrierSetToPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

    D3D12_RESOURCE_BARRIER resourceBarrierSetToUAV = {};
    resourceBarrierSetToUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrierSetToUAV.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrierSetToUAV.Transition.pResource = m_outputTexture.Get();
    resourceBarrierSetToUAV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resourceBarrierSetToUAV.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    resourceBarrierSetToUAV.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);

    // Wait for compute shader to finish writes to uav then copy the uav to the render target
    m_commandList->ResourceBarrier(1, &resourceBarrierWaitForUAV);
    m_commandList->ResourceBarrier(1, &resourceBarrierSetToCpySrc);
    m_commandList->ResourceBarrier(1, &resourceBarrierSetToCpyDst);
    m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), m_outputTexture.Get());
    m_commandList->ResourceBarrier(1, &resourceBarrierSetToPresent);
    m_commandList->ResourceBarrier(1, &resourceBarrierSetToUAV);

    m_commandList->Close();
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
}

// Render the scene.
void D3D12HelloTriangle::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    WaitForPreviousFrame();
}

void D3D12HelloTriangle::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

void D3D12HelloTriangle::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
