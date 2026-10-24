// glTF model: vertices pulled from a bindless buffer (Resource::MeshVertex), base color from a
// bindless texture, alpha cutout for masked materials, and simple directional + ambient light
// until PBR arrives in M5.

import common.bindless;

// Mirrors Resource::MeshVertex (engine/resource/model.h): the UV is packed into the w components.
struct MeshVertex
{
    float4 positionU;
    float4 normalV;
};

// Mirrored in game/sandbox/main.cpp (static_asserts there). 96 bytes.
struct ModelPush
{
    float4x4 model;         // model to world; uniform scale (normals use it directly)
    float4 baseColorFactor; // linear
    BufferIndex vertices;
    TextureIndex baseColor; // sRGB texture, or a white one when the material has none
    SamplerIndex sampler;
    float alphaCutoff;      // 0 for opaque materials
};

[[vk::push_constant]] ConstantBuffer<ModelPush> push;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertex_id: SV_VertexID)
{
    const MeshVertex vertex = LoadBuffer<MeshVertex>(push.vertices, vertex_id);
    VertexOutput output;
    output.position = mul(GetFrame().viewProjection, mul(push.model, float4(vertex.positionU.xyz, 1.0)));
    output.normal = mul(push.model, float4(vertex.normalV.xyz, 0.0)).xyz;
    output.uv = float2(vertex.positionU.w, vertex.normalV.w);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool front_face: SV_IsFrontFace) : SV_Target
{
    const float4 base = push.baseColorFactor * SampleTexture(push.baseColor, push.sampler, input.uv);
    if (base.a < push.alphaCutoff)
        discard;

    // Double-sided surfaces light their back side with the flipped normal. Zero normals (the
    // source had none) are lit as if facing the light.
    float3 normal = input.normal;
    const float normal_length = length(normal);
    const float3 light = normalize(float3(0.4, 1.0, 0.3));
    float diffuse = 1.0;
    if (normal_length > 0.0)
    {
        normal = (front_face ? normal : -normal) / normal_length;
        diffuse = saturate(dot(normal, light));
    }
    const float ambient = 0.25 + 0.1 * normal.y; // a little brighter facing the sky
    return float4(base.rgb * (ambient + 0.75 * diffuse), 1.0);
}
