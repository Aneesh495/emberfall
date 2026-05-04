#include "common.hpp"

#include <cstring>

namespace ember::gpu {

std::array<Vertex,36> cube() {
    constexpr std::array<Vec3,6> normals{
        Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,-1,0},Vec3{0,0,1},Vec3{0,0,-1}
    };
    std::array<Vertex,36> vertices{};
    std::size_t i = 0;
    for (const auto& n : normals) {
        const Vec3 u = cross(std::abs(n.y) > 0.5f ? Vec3{0,0,1} : Vec3{0,1,0},n);
        const Vec3 v = cross(n,u);
        const std::array corners{(n-u-v)*0.5f,(n+u-v)*0.5f,(n+u+v)*0.5f,(n-u+v)*0.5f};
        for (unsigned index : {0U,1U,2U,0U,2U,3U}) vertices[i++] = {corners[index],n};
    }
    return vertices;
}

bool visible(const Mat4& projection, const Mat4& model) {
    const auto clip = projection*model;
    unsigned common = 63;
    for (float x : {-0.5f,0.5f}) for (float y : {-0.5f,0.5f}) for (float z : {-0.5f,0.5f}) {
        std::array<float,4> p{};
        for (int r=0; r<4; ++r) p[static_cast<std::size_t>(r)] = clip(r,0)*x+clip(r,1)*y+clip(r,2)*z+clip(r,3);
        const unsigned flags = (p[0] < -p[3] ? 1U : 0U) | (p[0] > p[3] ? 2U : 0U) |
                               (p[1] < -p[3] ? 4U : 0U) | (p[1] > p[3] ? 8U : 0U) |
                               (p[2] < 0 ? 16U : 0U) | (p[2] > p[3] ? 32U : 0U);
        common &= flags;
    }
    return common == 0;
}

namespace {

class Module {
public:
    Module(VkDevice device, const std::filesystem::path& path) : device_(device) {
        const auto bytes = read_bounded(path, 256*1024);
        if (bytes.size() < 20 || bytes.size()%4) throw std::runtime_error("Invalid SPIR-V: " + path.string());
        std::vector<std::uint32_t> words(bytes.size()/4);
        std::memcpy(words.data(),bytes.data(),bytes.size());
        if (words.front() != 0x07230203U) throw std::runtime_error("Invalid SPIR-V magic: " + path.string());
        auto info = vk<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        info.codeSize = bytes.size();
        info.pCode = words.data();
        check(vkCreateShaderModule(device,&info,nullptr,&module), "Create shader module");
    }
    ~Module() { vkDestroyShaderModule(device_,module,nullptr); }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    VkShaderModule module{};
private:
    VkDevice device_;
};

}

VkPipeline pipeline(VkDevice device, const std::filesystem::path& dir,
                    VkPipelineLayout layout, VkFormat color, VkFormat depth, Pipeline kind) {
    const bool scene = kind == Pipeline::scene;
    const bool overlay = kind == Pipeline::overlay;
    Module vertex(device,dir / (scene ? "scene.vert.spv" : overlay ? "overlay.vert.spv" : "screen.vert.spv"));
    Module fragment(device,dir / (scene ? "scene.frag.spv" : overlay ? "overlay.frag.spv" : "tonemap.frag.spv"));
    std::array stages{
        vk<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO),
        vk<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO)
    };
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex.module;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment.module;
    for (auto& stage : stages) stage.pName = "main";

    const std::array<VkVertexInputBindingDescription,2> bindings{{
        {0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}, {1,sizeof(RenderItem),VK_VERTEX_INPUT_RATE_INSTANCE}
    }};
    const std::array<VkVertexInputAttributeDescription,7> attributes{{
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,position)},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,normal)},
        {2,1,VK_FORMAT_R32G32B32A32_SFLOAT,0},
        {3,1,VK_FORMAT_R32G32B32A32_SFLOAT,16},
        {4,1,VK_FORMAT_R32G32B32A32_SFLOAT,32},
        {5,1,VK_FORMAT_R32G32B32A32_SFLOAT,48},
        {6,1,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(RenderItem,color)}
    }};
    static_assert(offsetof(RenderItem,emission) == offsetof(RenderItem,color)+12);
    constexpr VkVertexInputBindingDescription ui_binding{0,sizeof(OverlayVertex),VK_VERTEX_INPUT_RATE_VERTEX};
    constexpr std::array<VkVertexInputAttributeDescription,2> ui_attributes{{
        {0,0,VK_FORMAT_R32G32_SFLOAT,offsetof(OverlayVertex,x)},
        {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(OverlayVertex,color)}
    }};
    auto input = vk<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    if (scene) {
        input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
        input.pVertexBindingDescriptions = bindings.data();
        input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        input.pVertexAttributeDescriptions = attributes.data();
    } else if (overlay) {
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &ui_binding;
        input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(ui_attributes.size());
        input.pVertexAttributeDescriptions = ui_attributes.data();
    }
    auto assembly = vk<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport = vk<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    auto raster = vk<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    auto sampling = vk<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    sampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto testing = vk<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    testing.depthTestEnable = scene;
    testing.depthWriteEnable = scene;
    testing.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (overlay) {
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    auto blend = vk<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array states{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic = vk<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(states.size());
    dynamic.pDynamicStates = states.data();
    auto rendering = vk<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color;
    rendering.depthAttachmentFormat = depth;
    auto info = vk<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    info.pNext = &rendering;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &sampling;
    info.pDepthStencilState = &testing;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline result{};
    check(vkCreateGraphicsPipelines(device,nullptr,1,&info,nullptr,&result), "Create graphics pipeline");
    return result;
}

}
