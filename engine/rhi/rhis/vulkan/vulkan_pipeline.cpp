// VulkanDevice: graphics pipeline creation and deferred destruction.

#include "rhi/rhis/vulkan/vulkan_device_internal.h"

#include "core/profile/profile.h"

#include <cstring>
#include <string>
#include <vector>

namespace Lmao::Rhi::Vulkan
{
    namespace
    {

        VkPrimitiveTopology to_vk(PrimitiveTopology topology)
        {
            switch (topology)
            {
            case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            }
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }

        VkCullModeFlags to_vk(CullMode cull)
        {
            switch (cull)
            {
            case CullMode::None: return VK_CULL_MODE_NONE;
            case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
            }
            return VK_CULL_MODE_NONE;
        }

        // VK_NULL_HANDLE on failure (logged).
        VkShaderModule create_shader_module(VkDevice device, std::span<const std::byte> code, const std::string &name)
        {
            if (code.empty() || code.size() % sizeof(uint32_t) != 0)
            {
                LMAO_LOG_ERROR(LogChannel(), "{}: shader code is empty or not SPIR-V ({} bytes)", name, code.size());
                return VK_NULL_HANDLE;
            }
            // pCode must be 4-byte aligned; the caller's bytes might not be.
            std::vector<uint32_t> words(code.size() / sizeof(uint32_t));
            std::memcpy(words.data(), code.data(), code.size());

            const VkShaderModuleCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size(),
                .pCode = words.data(),
            };
            VkShaderModule module = VK_NULL_HANDLE;
            if (!Check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule"))
                return VK_NULL_HANDLE;
            return module;
        }

    } // namespace

    PipelineHandle VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc &desc)
    {
        LMAO_PROFILE_ZONE();
        const std::string name = desc.DebugName.empty() ? std::string("Unnamed pipeline") : std::string(desc.DebugName);

        std::vector<VkFormat> color_formats;
        for (Format format : desc.ColorFormats)
        {
            const VkFormat vk_format = ToVkFormat(format);
            if (vk_format == VK_FORMAT_UNDEFINED)
            {
                LMAO_LOG_ERROR(LogChannel(), "{}: unsupported color format {}", name, ToString(format));
                return {};
            }
            color_formats.push_back(vk_format);
        }

        VkShaderModule vertex_module = create_shader_module(mDevice, desc.Vertex.Code, name + " (vertex)");
        VkShaderModule fragment_module = create_shader_module(mDevice, desc.Fragment.Code, name + " (fragment)");
        const std::string vertex_entry(desc.Vertex.EntryPoint);
        const std::string fragment_entry(desc.Fragment.EntryPoint);

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vertex_module != VK_NULL_HANDLE && fragment_module != VK_NULL_HANDLE)
        {
            const VkPipelineShaderStageCreateInfo stages[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = vertex_module,
                    .pName = vertex_entry.c_str(),
                },
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fragment_module,
                    .pName = fragment_entry.c_str(),
                },
            };

            // Vertices are pulled from buffers in the shader (bindless, M2): no vertex input state.
            const VkPipelineVertexInputStateCreateInfo vertex_input{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            };
            const VkPipelineInputAssemblyStateCreateInfo input_assembly{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = to_vk(desc.Topology),
            };
            const VkPipelineViewportStateCreateInfo viewport_state{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1,
            };
            const VkPipelineRasterizationStateCreateInfo rasterization{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = to_vk(desc.Cull),
                // Counter-clockwise in Y-up clip space; the negative viewport height keeps that true.
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f,
            };
            const VkPipelineMultisampleStateCreateInfo multisample{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            };
            const std::vector<VkPipelineColorBlendAttachmentState> blend_attachments(
                color_formats.size(),
                {.blendEnable = VK_FALSE,
                 .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                   VK_COLOR_COMPONENT_A_BIT});
            const VkPipelineColorBlendStateCreateInfo color_blend{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = static_cast<uint32_t>(blend_attachments.size()),
                .pAttachments = blend_attachments.data(),
            };
            const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            const VkPipelineDynamicStateCreateInfo dynamic{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states)),
                .pDynamicStates = dynamic_states,
            };
            const VkPipelineRenderingCreateInfo rendering{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                .colorAttachmentCount = static_cast<uint32_t>(color_formats.size()),
                .pColorAttachmentFormats = color_formats.data(),
            };

            const VkGraphicsPipelineCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pNext = &rendering,
                .stageCount = static_cast<uint32_t>(std::size(stages)),
                .pStages = stages,
                .pVertexInputState = &vertex_input,
                .pInputAssemblyState = &input_assembly,
                .pViewportState = &viewport_state,
                .pRasterizationState = &rasterization,
                .pMultisampleState = &multisample,
                .pColorBlendState = &color_blend,
                .pDynamicState = &dynamic,
                .layout = mPipelineLayout,
            };
            // TODO(M2): a VkPipelineCache persisted to disk.
            if (!Check(vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline),
                       "vkCreateGraphicsPipelines"))
                pipeline = VK_NULL_HANDLE;
        }

        // Modules are only needed while the pipeline is created.
        if (vertex_module != VK_NULL_HANDLE)
            vkDestroyShaderModule(mDevice, vertex_module, nullptr);
        if (fragment_module != VK_NULL_HANDLE)
            vkDestroyShaderModule(mDevice, fragment_module, nullptr);

        if (pipeline == VK_NULL_HANDLE)
        {
            LMAO_LOG_ERROR(LogChannel(), "Failed to create pipeline '{}'", name);
            return {};
        }
        SetName(VK_OBJECT_TYPE_PIPELINE, pipeline, name.c_str());
        LMAO_LOG_DEBUG(LogChannel(), "Created pipeline '{}'", name);
        return mPipelines.Insert({pipeline});
    }

    void VulkanDevice::DestroyPipeline(PipelineHandle handle)
    {
        const Pipeline *pipeline = mPipelines.Get(handle);
        LMAO_ASSERT(pipeline, "DestroyPipeline: stale or invalid handle");
        if (!pipeline)
            return;
        defer_destroy({.pipeline = pipeline->Handle});
        mPipelines.Remove(handle);
    }

} // namespace Lmao::Rhi::Vulkan
