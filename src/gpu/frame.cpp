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

bool Renderer::draw(std::span<const RenderItem> items, const View& view, std::span<const OverlayVertex> overlay, std::span<const SkinBatch> meshes, std::span<const MeshInstance> imported) {
    return state_->draw(items,view,overlay,meshes,imported);
}

void Renderer::capture_next(std::filesystem::path file) { state_->capture_file = std::move(file); }

void Renderer::State::wait(std::uint64_t value) const {
    if (!value) return;
    auto info = vk<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    info.semaphoreCount = 1;
    info.pSemaphores = &timeline;
    info.pValues = &value;
    check(vkWaitSemaphores(device,&info,5'000'000'000ULL),"Wait for frame completion timeline");
}

void Renderer::State::record(Frame& frame, Screen& screen, const View& view, const Mat4& camera, const Mat4& light, std::uint32_t count, std::uint32_t shadow_count,
                             std::uint32_t overlay_count,std::span<const SkinDraw> meshes) {
    const auto cmd = frame.commands;
    auto begin = vk<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd,&begin),"Begin recording frame");
    if (frame.timer) {
        vkCmdResetQueryPool(cmd,frame.timer,0,4);
        vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,frame.timer,0);
    }
    const VkDeviceSize mesh_offset = view.dense_mesh ? 36*sizeof(gpu::Vertex) : 0;
    const std::uint32_t vertices = view.dense_mesh ? 576U : 36U;
    const auto draw_skin = [&] {
        if (meshes.empty()) return;
        const std::array buffers{frame.skin.buffer,frame.skin_instances.buffer};
        constexpr std::array<VkDeviceSize,2> offsets{};
        vkCmdBindVertexBuffers(cmd,0,static_cast<std::uint32_t>(buffers.size()),buffers.data(),offsets.data());
        for (std::uint32_t i=0; i<meshes.size(); ++i) {
            vkCmdDraw(cmd,meshes[i].count,1,meshes[i].first,i);
        }
    };
    const VkViewport viewport{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),0,1};
    const VkRect2D rect{{0,0},extent};

    gpu::Resource hdr{frame.hdr.image,VK_IMAGE_ASPECT_COLOR_BIT};
    gpu::Resource depth{frame.depth.image,VK_IMAGE_ASPECT_DEPTH_BIT};
    gpu::Resource shadow{frame.shadow.image,VK_IMAGE_ASPECT_DEPTH_BIT};
    // Reusing a slot also needs a queue execution dependency across submissions.
    if (frame.attachments_written) {
        hdr = {frame.hdr.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
        depth = {frame.depth.image,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    }
    if (frame.shadow_written) {
        shadow = {frame.shadow.image,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    }
    gpu::Resource backbuffer{screen.image,VK_IMAGE_ASPECT_COLOR_BIT,
        screen.presented ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,0};
    const gpu::RenderGraph graph(cmd);
    graph.pass({{shadow,gpu::ImageUse::depth_output}},[&] {
        const VkRect2D area{{0,0},{shadow_size,shadow_size}};
        const VkViewport shadow_view{0,0,shadow_size,shadow_size,0,1};
        vkCmdSetViewport(cmd,0,1,&shadow_view);
        vkCmdSetScissor(cmd,0,1,&area);
        auto depth_attachment = vk<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        depth_attachment.imageView = frame.shadow.view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.clearValue.depthStencil.depth = 1;
        auto rendering = vk<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea = area;
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &depth_attachment;
        vkCmdBeginRendering(cmd,&rendering);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,shadow_pipeline);
        const std::array buffers{geometry.buffer,frame.instances.buffer};
        const std::array<VkDeviceSize,2> offsets{mesh_offset,instance_limit*sizeof(RenderItem)};
        vkCmdBindVertexBuffers(cmd,0,static_cast<std::uint32_t>(buffers.size()),buffers.data(),offsets.data());
        vkCmdPushConstants(cmd,shadow_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(light),&light);
        if (shadow_count) vkCmdDraw(cmd,vertices,shadow_count,0,0);
        draw_skin();
        vkCmdEndRendering(cmd);
    });
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,1);
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&rect);
    graph.pass({{hdr,gpu::ImageUse::hdr_output},{depth,gpu::ImageUse::depth_output},
                {shadow,gpu::ImageUse::shadow_sampled}},[&] {
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
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,scene_layout,0,1,&frame.light_set,0,nullptr);
        const std::array buffers{geometry.buffer,frame.instances.buffer};
        const std::array<VkDeviceSize,2> offsets{mesh_offset,0};
        vkCmdBindVertexBuffers(cmd,0,static_cast<std::uint32_t>(buffers.size()),buffers.data(),offsets.data());
        struct Camera {
            Mat4 projection;
            std::array<float,4> eye;
        } constants{camera,{view.eye.x,view.eye.y,view.eye.z,0}};
        static_assert(sizeof(Camera) == 80);
        vkCmdPushConstants(cmd,scene_layout,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,sizeof(constants),&constants);
        if (count) vkCmdDraw(cmd,vertices,count,0,0);
        draw_skin();
        vkCmdEndRendering(cmd);
    });
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,2);
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
        if (overlay_count) {
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,overlay_pipeline);
            constexpr VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd,0,1,&frame.overlay.buffer,&offset);
            const std::uint32_t encode = linear_target;
            vkCmdPushConstants(cmd,overlay_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(encode),&encode);
            vkCmdDraw(cmd,overlay_count,1,0,0);
        }
        vkCmdEndRendering(cmd);
    });
    if (!capture_file.empty()) {
        graph.pass({{backbuffer,gpu::ImageUse::transfer_read}},[&] {
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            region.imageExtent = {extent.width,extent.height,1};
            vkCmdCopyImageToBuffer(cmd,screen.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,capture.buffer,1,&region);
        });
    }
    graph.pass({{backbuffer,gpu::ImageUse::present}},[]{});
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,3);
    check(vkEndCommandBuffer(cmd),"Finish recording frame");
}

bool Renderer::State::draw(std::span<const RenderItem> items, const View& view, std::span<const OverlayVertex> overlay, std::span<const SkinBatch> meshes, std::span<const MeshInstance> imported) {
    if (meshes.size()>skin_mesh_limit) throw std::runtime_error("Frame skin mesh limit");
    if (items.size() > instance_limit) throw std::runtime_error("Frame instance limit exceeded");
    if (overlay.size() > overlay_limit) throw std::runtime_error("Frame overlay limit exceeded");
    if (dirty && !rebuild_swapchain()) return false;
    if (!capture_file.empty()) {
        if (!capture_supported) throw std::runtime_error("Swapchain readback unsupported");
        if (!capture.buffer) {
            make_buffer(capture,static_cast<VkDeviceSize>(extent.width)*extent.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        }
    }
    auto& frame = frames[serial % slots];
    wait(frame.finished);
    double elapsed = 0;
    const auto timed_serial = frame.finished;
    std::array<std::uint64_t,4> stamps{};
    if (timed_serial && frame.timer) {
        check(vkGetQueryPoolResults(device,frame.timer,0,4,sizeof(stamps),stamps.data(),sizeof(stamps[0]),
                                    VK_QUERY_RESULT_64_BIT),"Read completed GPU pass timestamps");
        const auto mask = timestamp_bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (1ULL<<timestamp_bits)-1;
        elapsed = static_cast<double>((stamps[3]-stamps[0]) & mask)*properties.limits.timestampPeriod*1e-6;
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
    const auto camera = perspective(view.fov,aspect,0.1f,view.far_plane)*look_at(view.eye,view.target,{0,1,0});
    const Vec3 direction = unit(Vec3{-0.45f,0.82f,0.24f});
    const Vec3 anchor{std::round(view.target.x*32)/32,0,std::round(view.target.z*32)/32};
    const auto light = orthographic(18,18,0.5f,85)*look_at(anchor+direction*32,anchor,{0,1,0});
    const auto mark = [&](std::size_t start,std::size_t end) {
        for (std::size_t i=start; i<end; ++i) {
            visibility[i] = static_cast<std::uint8_t>((gpu::visible(camera,items[i].model) ? 1U : 0U) |
                (gpu::visible(light,items[i].model) ? 2U : 0U));
        }
    };
    if (items.size() < 1024) mark(0,items.size());
    else {
        std::vector<Task> tasks;
        for (unsigned i=0; i<4; ++i) {
            tasks.push_back({{},{{0,false},{static_cast<ResourceId>(i+1),true}},[&,i](std::stop_token) {
                mark(items.size()*i/4,items.size()*(i+1)/4);
            }});
        }
        culling.run(Schedule(std::move(tasks)));
    }
    std::uint32_t count = 0, shadow_count = 0;
    Lighting lighting{light,{direction.x,direction.y,direction.z,0},{}};
    std::size_t lamps = 0;
    auto* upload = static_cast<std::byte*>(frame.instances.mapped);
    for (std::size_t i=0; i<items.size(); ++i) {
        const auto& item = items[i];
        if (visibility[i] & 1U) {
            std::memcpy(upload+count*sizeof(item),&item,sizeof(item));
            ++count;
        }
        if (visibility[i] & 2U) {
            std::memcpy(upload+(instance_limit+shadow_count)*sizeof(item),&item,sizeof(item));
            ++shadow_count;
        }
        if (item.emission > 0.6f && lamps < light_limit) {
            const auto p = transform_point(item.model,{});
            lighting.lights[lamps++] = {{p.x,p.y+0.2f,p.z,3.4f},
                {item.color.x,item.color.y,item.color.z,std::min(item.emission,4.0f)*1.3f}};
        }
    }
    lighting.direction_count[3] = static_cast<float>(lamps);
    static_assert(sizeof(Lighting) == 80+32*light_limit);
    std::memcpy(frame.lighting.mapped,&lighting,sizeof(lighting));
    if (!overlay.empty()) std::memcpy(frame.overlay.mapped,overlay.data(),overlay.size_bytes());
    std::array<SkinDraw,uploaded_mesh_limit> skin_draws{};
    std::size_t uploaded_draws = 0;
    std::uint32_t uploaded_vertices = 0;
    const auto upload_mesh = [&](std::span<const MeshVertex> vertices, const RenderItem& instance) {
        if (vertices.empty() || vertices.size()%3 || uploaded_draws>=skin_draws.size() ||
            vertices.size()>uploaded_vertex_limit-uploaded_vertices) {
            throw std::runtime_error("Frame mesh upload budget exceeded");
        }
        auto& draw = skin_draws[uploaded_draws];
        draw = {uploaded_vertices,static_cast<std::uint32_t>(vertices.size())};
        std::memcpy(static_cast<std::byte*>(frame.skin.mapped)+uploaded_vertices*sizeof(MeshVertex),
                    vertices.data(),vertices.size_bytes());
        std::memcpy(static_cast<std::byte*>(frame.skin_instances.mapped)+uploaded_draws*sizeof(RenderItem),
                    &instance,sizeof(instance));
        ++uploaded_draws;
        uploaded_vertices += draw.count;
    };
    for (const auto& mesh : meshes) {
        if (mesh.vertices.size()>skin_vertex_limit-uploaded_vertices) throw std::runtime_error("Frame skin vertex budget");
        upload_mesh(mesh.vertices,{mesh.model,mesh.color,0});
    }
    const auto animated_vertices = uploaded_vertices;
    for (const auto& mesh : imported) {
        if (!mesh.asset) throw std::runtime_error("Missing draw mesh");
        for (const auto& primitive : mesh.asset->primitives) {
            const auto& m = primitive.material;
            const Vec3 color{mesh.tint.x*m.base_color.x,mesh.tint.y*m.base_color.y,mesh.tint.z*m.base_color.z};
            upload_mesh(primitive.vertices,{mesh.model,color,0,{m.emissive.x,m.emissive.y,m.emissive.z,m.roughness},m.metallic});
        }
    }
    record(frame,screen,view,camera,light,count,shadow_count,static_cast<std::uint32_t>(overlay.size()),
           std::span(skin_draws).first(uploaded_draws));
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
    frame.attachments_written = frame.shadow_written = true;
    screen.presented = true;
    auto present = vk<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &screen.ready;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &index;
    const auto status = vkQueuePresentKHR(queue,&present);
    if (status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR) dirty = true;
    else check(status,"Present frame");
    if (!capture_file.empty()) {
        wait(frame.finished);
        save_capture();
    }
    const auto triangles = count*(view.dense_mesh ? 192U : 12U)+uploaded_vertices/3;
    const unsigned draws = 1 + static_cast<unsigned>(count>0) + static_cast<unsigned>(shadow_count>0) +
                           static_cast<unsigned>(!overlay.empty())+2*static_cast<unsigned>(uploaded_draws);
    report = {extent.width,extent.height,count,static_cast<std::uint32_t>(items.size())-count,elapsed,serial,
              shadow_count,static_cast<std::uint32_t>(lamps),triangles,draws,timed_serial,stamps,
              properties.limits.timestampPeriod,timestamp_bits,(uploaded_vertices-animated_vertices)/3};
    return true;
}

void Renderer::State::save_capture() {
    const auto count = static_cast<std::size_t>(extent.width)*extent.height;
    auto ppm = "P6\n" + std::to_string(extent.width) + " " + std::to_string(extent.height) + "\n255\n";
    const auto offset = ppm.size();
    ppm.resize(offset+count*3);
    const auto* data = static_cast<const char*>(capture.mapped);
    const bool bgra = screen_format == VK_FORMAT_B8G8R8A8_SRGB || screen_format == VK_FORMAT_B8G8R8A8_UNORM;
    for (std::size_t i=0; i<count; ++i) {
        ppm[offset+3*i] = data[4*i+(bgra ? 2U : 0U)];
        ppm[offset+3*i+1] = data[4*i+1];
        ppm[offset+3*i+2] = data[4*i+(bgra ? 0U : 2U)];
    }
    write_atomic(capture_file,ppm);
    capture_file.clear();
}

}
