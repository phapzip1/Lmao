// Dear ImGui through the RHI (engine/render/imgui/): vertices pulled from a per-frame bindless
// buffer, the texture by bindless index (ImTextureID), alpha-blended, scissored per command.

import common.bindless;

// ImDrawVert: 20 bytes.
struct ImGuiVertex
{
    float2 position; // pixels, origin top-left
    float2 uv;
    uint color;      // RGBA8 in sRGB (gamma) space, R in the low byte
};

// Mirrored in engine/render/imgui/imgui_backend.cpp (static_asserts there).
struct ImGuiPush
{
    float2 scale;      // pixels -> clip space (Y-up)
    float2 translate;
    BufferIndex vertices;
    TextureIndex texture;
    SamplerIndex sampler;
    uint vertexBase;   // ImDrawCmd::VtxOffset + the draw list's offset in the shared buffer
    uint linearizeColor; // 1 when rendering to an sRGB target: convert vertex colors to linear
};

[[vk::push_constant]] ConstantBuffer<ImGuiPush> push;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float3 srgb_to_linear(float3 c)
{
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

[shader("vertex")]
VertexOutput vertexMain(uint vertex_id: SV_VertexID)
{
    const ImGuiVertex vertex = LoadBuffer<ImGuiVertex>(push.vertices, push.vertexBase + vertex_id);
    float4 color = float4(vertex.color & 0xFF, (vertex.color >> 8) & 0xFF, (vertex.color >> 16) & 0xFF,
                          vertex.color >> 24) / 255.0;
    if (push.linearizeColor != 0)
        color.rgb = srgb_to_linear(color.rgb); // the sRGB target encodes it back on store

    VertexOutput output;
    output.position = float4(vertex.position * push.scale + push.translate, 0.0, 1.0);
    output.uv = vertex.uv;
    output.color = color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return input.color * SampleTexture(push.texture, push.sampler, input.uv);
}
