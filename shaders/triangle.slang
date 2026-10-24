// Sandbox overlay triangle, drawn through the binding convention (common/bindless.slang):
// vertices from a bindless storage buffer, a texture + sampler by bindless index, the indices
// in push constants, and time / resolution from the per-frame constants (set 1). Drawn in 2D
// over the 3D scene with alpha blending (vertex alpha).
// Clip space is Y-up: the Vulkan backend flips Y with a negative viewport height.

import common.bindless;

// Mirrored in game/sandbox/main.cpp (static_asserts there).
struct Vertex
{
    float2 position;
    float2 uv;
    float4 color;
};

struct TrianglePush
{
    BufferIndex vertices;
    TextureIndex texture;
    SamplerIndex sampler;
    float spin; // radians per second
};

[[vk::push_constant]] ConstantBuffer<TrianglePush> push;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertex_id: SV_VertexID)
{
    const Vertex vertex = LoadBuffer<Vertex>(push.vertices, vertex_id);
    const FrameConstants frame = GetFrame();

    // Counter-clockwise rotation keeps the counter-clockwise (front-facing) winding.
    const float angle = frame.time * push.spin;
    const float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));

    // 2D: keep the shape on any aspect ratio (the frame's viewProjection is the 3D camera's).
    const float aspect = float(frame.resolution.x) / float(frame.resolution.y);
    const float2 position = mul(rotation, vertex.position);
    VertexOutput output;
    output.position = float4(position.x / aspect, position.y, 0.0, 1.0);
    output.uv = vertex.uv;
    output.color = vertex.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return input.color * SampleTexture(push.texture, push.sampler, input.uv);
}
