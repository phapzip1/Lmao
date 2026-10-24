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

        VkCompareOp to_vk(CompareOp op)
        {
            switch (op)
            {
            case CompareOp::Never: return VK_COMPARE_OP_NEVER;
            case CompareOp::Less: return VK_COMPARE_OP_LESS;
            case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
            case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
            case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
            }
            return VK_COMPARE_OP_GREATER;
        }

        VkBlendFactor to_vk(BlendFactor factor)
        {
            switch (factor)
            {
            case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SourceColor: return VK_BLEND_FACTOR_SRC_COLOR;
            case BlendFactor::OneMinusSourceColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BlendFactor::SourceAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSourceAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DestinationColor: return VK_BLEND_FACTOR_DST_COLOR;
            case BlendFactor::OneMinusDestinationColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case BlendFactor::DestinationAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
            case BlendFactor::OneMinusDestinationAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            }
            return VK_BLEND_FACTOR_ONE;
        }

        VkBlendOp to_vk(BlendOp op)
        {
            switch (op)
            {
            case BlendOp::Add: return VK_BLEND_OP_ADD;
            case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
            case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
            case BlendOp::Min: return VK_BLEND_OP_MIN;
            case BlendOp::Max: return VK_BLEND_OP_MAX;
            }
            return VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendAttachmentState to_vk(const BlendState &blend)
        {
            VkColorComponentFlags mask = 0;
            if (HasAny(blend.WriteMask, ColorMask::R)) mask |= VK_COLOR_COMPONENT_R_BIT;
            if (HasAny(blend.WriteMask, ColorMask::G)) mask |= VK_COLOR_COMPONENT_G_BIT;
            if (HasAny(blend.WriteMask, ColorMask::B)) mask |= VK_COLOR_COMPONENT_B_BIT;
            if (HasAny(blend.WriteMask, ColorMask::A)) mask |= VK_COLOR_COMPONENT_A_BIT;
            return {
                .blendEnable = blend.Enable ? VK_TRUE : VK_FALSE,
                .srcColorBlendFactor = to_vk(blend.SourceColor),
                .dstColorBlendFactor = to_vk(blend.DestinationColor),
                .colorBlendOp = to_vk(blend.ColorOp),
                .srcAlphaBlendFactor = to_vk(blend.SourceAlpha),
                .dstAlphaBlendFactor = to_vk(blend.DestinationAlpha),
                .alphaBlendOp = to_vk(blend.AlphaOp),
                .colorWriteMask = mask,
            };
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
        if (!desc.Blend.empty() && desc.Blend.size() != desc.ColorFormats.size())
        {
            LMAO_LOG_ERROR(LogChannel(), "{}: {} blend states for {} color attachments (pass one per attachment, or none)",
                           name, desc.Blend.size(), desc.ColorFormats.size());
            return {};
        }
        const VkFormat depth_format = ToVkFormat(desc.DepthFormat);
        if (desc.DepthFormat != Format::Unknown && !IsDepthFormat(desc.DepthFormat))
        {
            LMAO_LOG_ERROR(LogChannel(), "{}: depth format {} isn't a depth format", name, ToString(desc.DepthFormat));
            return {};
        }

        VkShaderModule vertex_module = create_shader_module(mDevice, desc.Vertex.Code, name + " (vertex)");
        // No fragment shader: depth-only pipelines (shadow maps, depth pre-pass).
        const bool has_fragment = !desc.Fragment.Code.empty();
        if (!has_fragment && !desc.ColorFormats.empty())
        {
            vkDestroyShaderModule(mDevice, vertex_module, nullptr);
            LMAO_LOG_ERROR(LogChannel(), "{}: a fragment shader is required when there are color attachments", name);
            return {};
        }
        VkShaderModule fragment_module =
            has_fragment ? create_shader_module(mDevice, desc.Fragment.Code, name + " (fragment)") : VK_NULL_HANDLE;
        const std::string vertex_entry(desc.Vertex.EntryPoint);
        const std::string fragment_entry(desc.Fragment.EntryPoint);

        VkPipeline pipeline = VK_NULL_HANDLE;
        std::string cache_note; // " in 1.23 ms (cache hit)" when the driver gives feedback
        if (vertex_module != VK_NULL_HANDLE && (fragment_module != VK_NULL_HANDLE || !has_fragment))
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
            std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
            for (size_t i = 0; i < color_formats.size(); ++i)
                blend_attachments.push_back(to_vk(desc.Blend.empty() ? BlendState::Opaque() : desc.Blend[i]));
            const VkPipelineDepthStencilStateCreateInfo depth_stencil{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = desc.Depth.TestEnable ? VK_TRUE : VK_FALSE,
                .depthWriteEnable = desc.Depth.WriteEnable ? VK_TRUE : VK_FALSE,
                .depthCompareOp = to_vk(desc.Depth.Compare),
                .minDepthBounds = 0.0f,
                .maxDepthBounds = 1.0f,
            };
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
            // Reports whether the driver found the pipeline in mPipelineCache. Drivers with their own
            // shader disk cache (Mesa, NVIDIA) also report hits from it, even with an empty
            // mPipelineCache: MESA_SHADER_CACHE_DISABLE=true / __GL_SHADER_DISK_CACHE=0 to test ours.
            VkPipelineCreationFeedback feedback{};
            VkPipelineCreationFeedbackCreateInfo feedback_info{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO,
                .pPipelineCreationFeedback = &feedback,
            };
            const VkPipelineRenderingCreateInfo rendering{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                .pNext = &feedback_info,
                .colorAttachmentCount = static_cast<uint32_t>(color_formats.size()),
                .pColorAttachmentFormats = color_formats.data(),
                .depthAttachmentFormat = depth_format, // UNDEFINED when there is no depth attachment
            };

            const VkGraphicsPipelineCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pNext = &rendering,
                .stageCount = has_fragment ? 2u : 1u,
                .pStages = stages,
                .pVertexInputState = &vertex_input,
                .pInputAssemblyState = &input_assembly,
                .pViewportState = &viewport_state,
                .pRasterizationState = &rasterization,
                .pMultisampleState = &multisample,
                .pDepthStencilState = desc.DepthFormat != Format::Unknown ? &depth_stencil : nullptr,
                .pColorBlendState = &color_blend,
                .pDynamicState = &dynamic,
                .layout = mPipelineLayout,
            };
            if (!Check(vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1, &create_info, nullptr, &pipeline),
                       "vkCreateGraphicsPipelines"))
                pipeline = VK_NULL_HANDLE;
            else if (feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT)
            {
                const bool hit = feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
                ++(hit ? mPipelineCacheHits : mPipelineCacheMisses);
                cache_note = fmt::format(" in {:.2f} ms ({})", static_cast<double>(feedback.duration) / 1e6,
                                         hit ? "cache hit" : "cache miss");
            }
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
        LMAO_LOG_DEBUG(LogChannel(), "Created pipeline '{}'{}", name, cache_note);
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
