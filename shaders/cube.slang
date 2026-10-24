// Sandbox cube: indexed, depth-tested (reverse-Z, GREATER), textured through the binding
// convention. Vertices are pulled from a bindless buffer; SV_VertexID is the index value.

import common.bindless;

// Mirrored in game/sandbox/main.cpp (static_asserts there).
struct CubeVertex
{
    float4 position; // xyz, w = 1
    float4 color;
    float2 uv;
    float2 pad;
};

struct CubePush
{
    float4x4 model;
    BufferIndex vertices;
    TextureIndex texture;
    SamplerIndex sampler;
    uint pad;
};

[[vk::push_constant]] ConstantBuffer<CubePush> push;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertex_id: SV_VertexID)
{
    const CubeVertex vertex = LoadBuffer<CubeVertex>(push.vertices, vertex_id);
    VertexOutput output;
    output.position = mul(GetFrame().viewProjection, mul(push.model, float4(vertex.position.xyz, 1.0)));
    output.uv = vertex.uv;
    output.color = vertex.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return input.color * SampleTexture(push.texture, push.sampler, input.uv);
}
