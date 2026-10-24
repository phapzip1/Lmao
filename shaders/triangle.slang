// M1 triangle: positions and colors come from SV_VertexID, so no vertex buffer is needed.
// Clip space is Y-up: the Vulkan backend flips Y with a negative viewport height.
// Counter-clockwise (top, bottom-left, bottom-right), i.e. front-facing in the engine convention.

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

static const float2 positions[3] = { float2(0.0, 0.6), float2(-0.6, -0.5), float2(0.6, -0.5) };
static const float3 colors[3] = { float3(1.0, 0.1, 0.1), float3(0.1, 0.2, 1.0), float3(0.1, 1.0, 0.1) };

[shader("vertex")]
VertexOutput vertexMain(uint vertex_id: SV_VertexID)
{
    VertexOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.color = colors[vertex_id];
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0); // linear; the sRGB swapchain encodes on store
}
