// Binding convention (docs/decisions/0010-bindless-descriptors.md). Every shader that uses
// resources imports this module:
//
//     import common.bindless;
//
//   push constants  up to 128 bytes per draw: usually descriptor indices (see below)
//   set 0           global bindless arrays, indexed by an RHI handle's index
//                   (Rhi::DescriptorIndex(handle) on the C++ side, which returns the same
//                   TextureIndex / BufferIndex / SamplerIndex types as below)
//   set 1           per-frame constants (CommandList::SetFrameConstants)
//
// Indices are wrapped in NonUniformResourceIndex, so they may differ between invocations of
// a draw (e.g. per material). Every slot always holds a valid descriptor: unused and freed
// slots point at index 0, the device's fallback (magenta texture; a buffer of NaNs in Debug
// builds, zeros in Release; a linear sampler), so a stale or forgotten index shows up instead
// of reading garbage.
module bindless;

// --- Typed indices: a buffer index can't be passed where a texture index is expected --------

public struct TextureIndex
{
    public uint value;
};

public struct BufferIndex
{
    public uint value;
};

public struct SamplerIndex
{
    public uint value;
};

// --- Set 0: global bindless arrays ----------------------------------------------------------

[[vk::binding(0, 0)]] Texture2D gTextures[];
[[vk::binding(1, 0)]] RWTexture2D<float4> gStorageImages[];
[[vk::binding(2, 0)]] RWByteAddressBuffer gBuffers[];
[[vk::binding(3, 0)]] SamplerState gSamplers[];

public Texture2D GetTexture(TextureIndex texture) { return gTextures[NonUniformResourceIndex(texture.value)]; }
public RWTexture2D<float4> GetStorageImage(TextureIndex texture) { return gStorageImages[NonUniformResourceIndex(texture.value)]; }
public SamplerState GetSampler(SamplerIndex sampler) { return gSamplers[NonUniformResourceIndex(sampler.value)]; }

public float4 SampleTexture(TextureIndex texture, SamplerIndex sampler, float2 uv)
{
    return GetTexture(texture).Sample(GetSampler(sampler), uv);
}

// Element `element` of a buffer holding an array of T (scalar layout: T as laid out in C++
// with no padding beyond its own alignment; no float3 / vec3 members).
public T LoadBuffer<T>(BufferIndex buffer, uint element)
{
    return gBuffers[NonUniformResourceIndex(buffer.value)].Load<T>(element * sizeof(T));
}

public void StoreBuffer<T>(BufferIndex buffer, uint element, T value)
{
    gBuffers[NonUniformResourceIndex(buffer.value)].Store<T>(element * sizeof(T), value);
}

// --- Set 1: per-frame constants ---------------------------------------------------------------
// Mirrored in C++ with static_asserts on size and offsets; std140 rules, so no float3 members.

public struct FrameConstants
{
    public float4x4 viewProjection;
    public float4x4 view;
    public float4x4 projection;
    public float4 cameraPosition; // xyz, w unused
    public float time;            // seconds since start
    public float deltaTime;       // seconds since the previous frame
    public uint2 resolution;      // render target size in pixels
};

[[vk::binding(0, 1)]] ConstantBuffer<FrameConstants> gFrame;

public FrameConstants GetFrame() { return gFrame; }
