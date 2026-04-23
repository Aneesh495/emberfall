#include "state.hpp"

#include <cstring>
#include <limits>

namespace ember {
using gpu::check;
using gpu::vk;

namespace {

VkRenderingAttachmentInfo color_attachment(VkImageView view, std::array<float,4> clear) {
    auto info = vk<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    info.imageView = view;
    info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    std::copy(clear.begin(),clear.end(),info.clearValue.color.float32);
    return info;
}

}

bool Renderer::draw(std::span<const RenderItem> items, const View& view) { return state_->draw(items,view); }

void Renderer::State::wait(std::uint64_t value) const {
    if (!value) return;
    auto info = vk<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    info.semaphoreCount = 1;
    info.pSemaphores = &timeline;
    info.pValues = &value;
    check(vkWaitSemaphores(device,&info,5'000'000'000ULL),"Wait for frame completion timeline");
}

void Renderer::State::record(Frame& frame, Screen& screen, const View& view, const Mat4& camera, std::uint32_t count) {
    const auto cmd = frame.commands;
    auto begin = vk<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd,&begin),"Begin recording frame");
    if (frame.timer) {
        vkCmdResetQueryPool(cmd,frame.timer,0,2);
        vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,frame.timer,0);
    }
    const VkViewport viewport{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),0,1};
    const VkRect2D rect{{0,0},extent};
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&rect);
    gpu::Resource hdr{frame.hdr.image,VK_IMAGE_ASPECT_COLOR_BIT};
    gpu::Resource depth{frame.depth.image,VK_IMAGE_ASPECT_DEPTH_BIT};
    gpu::Resource backbuffer{screen.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,0};
    const gpu::RenderGraph graph(cmd);
    graph.pass({{hdr,gpu::ImageUse::hdr_output},{depth,gpu::ImageUse::depth_output}},[&] {
        auto color = color_attachment(frame.hdr.view,{0.028f,0.044f,0.072f,1});
        auto depth_attachment = vk<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        depth_attachment.imageView = frame.depth.view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.clearValue.depthStencil.depth = 1;
        auto rendering = vk<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea = rect;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &depth_attachment;
        vkCmdBeginRendering(cmd,&rendering);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,scene_pipeline);
        const std::array buffers{geometry.buffer,frame.instances.buffer};
        constexpr std::array<VkDeviceSize,2> offsets{};
        vkCmdBindVertexBuffers(cmd,0,static_cast<std::uint32_t>(buffers.size()),buffers.data(),offsets.data());
        struct Camera {
            Mat4 projection;
            std::array<float,4> eye;
        } constants{camera,{view.eye.x,view.eye.y,view.eye.z,0}};
        static_assert(sizeof(Camera) == 80);
        vkCmdPushConstants(cmd,scene_layout,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,sizeof(constants),&constants);
        if (count) vkCmdDraw(cmd,36,count,0,0);
        vkCmdEndRendering(cmd);
    });
    graph.pass({{hdr,gpu::ImageUse::sampled},{backbuffer,gpu::ImageUse::hdr_output}},[&] {
        auto color = color_attachment(screen.view,{0,0,0,1});
        auto rendering = vk<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea = rect;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmd,&rendering);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,tonemap_pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,tonemap_layout,0,1,&frame.sample,0,nullptr);
        const bool linear_target = screen_format == VK_FORMAT_B8G8R8A8_UNORM || screen_format == VK_FORMAT_R8G8B8A8_UNORM;
        struct Display { float exposure; std::int32_t antialias, srgb; };
        const Display display{std::clamp(view.exposure,0.05f,8.0f),view.antialias,linear_target};
        vkCmdPushConstants(cmd,tonemap_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(display),&display);
        vkCmdDraw(cmd,3,1,0,0);
        vkCmdEndRendering(cmd);
    });
    graph.pass({{backbuffer,gpu::ImageUse::present}},[]{});
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,1);
    check(vkEndCommandBuffer(cmd),"Finish recording frame");
}

bool Renderer::State::draw(std::span<const RenderItem> items, const View& view) {
    if (items.size() > instance_limit) throw std::runtime_error("Frame instance limit exceeded");
    if (dirty && !rebuild_swapchain()) return false;
    auto& frame = frames[serial % slots];
    wait(frame.finished);
    double elapsed = 0;
    if (frame.finished && frame.timer) {
        std::array<std::uint64_t,2> stamps{};
        check(vkGetQueryPoolResults(device,frame.timer,0,2,sizeof(stamps),stamps.data(),sizeof(stamps[0]),
                                    VK_QUERY_RESULT_64_BIT),"Read completed GPU frame timestamps");
        const auto mask = timestamp_bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (1ULL<<timestamp_bits)-1;
        elapsed = static_cast<double>((stamps[1]-stamps[0]) & mask)*properties.limits.timestampPeriod*1e-6;
    }
    std::uint32_t index = 0;
    const auto acquired = vkAcquireNextImageKHR(device,swapchain,1'000'000'000ULL,frame.acquire,nullptr,&index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        dirty = true;
        return false;
    }
    if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) return false;
    if (acquired != VK_SUBOPTIMAL_KHR) check(acquired,"Acquire swapchain image");
    auto& screen = screens.at(index);
    check(vkResetCommandPool(device,frame.pool,0),"Reset frame command pool");
    const auto aspect = static_cast<float>(extent.width)/static_cast<float>(extent.height);
    const auto camera = perspective(view.fov,aspect,0.1f,160.0f)*look_at(view.eye,view.target,{0,1,0});
    std::uint32_t count = 0;
    for (const auto& item : items) {
        if (!gpu::visible(camera,item.model)) continue;
        std::memcpy(static_cast<std::byte*>(frame.instances.mapped)+count*sizeof(item),&item,sizeof(item));
        ++count;
    }
    record(frame,screen,view,camera,count);
    auto acquire = vk<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    acquire.semaphore = frame.acquire;
    acquire.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    std::array signal{
        vk<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO),
        vk<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO)
    };
    signal[0].semaphore = screen.ready;
    signal[1].semaphore = timeline;
    signal[1].value = serial+1;
    for (auto& sem : signal) sem.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    auto commands = vk<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
    commands.commandBuffer = frame.commands;
    auto submit = vk<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &acquire;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commands;
    submit.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signal.size());
    submit.pSignalSemaphoreInfos = signal.data();
    check(vkQueueSubmit2(queue,1,&submit,nullptr),"Submit render graph");
    frame.finished = ++serial;
    auto present = vk<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &screen.ready;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &index;
    const auto status = vkQueuePresentKHR(queue,&present);
    if (status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR) dirty = true;
    else check(status,"Present frame");
    report = {extent.width,extent.height,count,static_cast<std::uint32_t>(items.size())-count,elapsed,serial};
    return true;
}

}
