#include "state.hpp"

#include <bit>
#include <cstring>
#include <limits>

namespace ember {
using gpu::check;
using gpu::vk;

namespace {

float radical_inverse(std::uint32_t i,std::uint32_t base) {
    float sample = 0;
    float weight = 1;
    while (i) {
        weight /= static_cast<float>(base);
        sample += static_cast<float>(i%base)*weight;
        i /= base;
    }
    return sample;
}

gpu::Resource sampled_image(VkImage image) {
    return {image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
}

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

bool Renderer::draw(std::span<const RenderItem> items,const View& view,std::span<const OverlayVertex> overlay,
                    std::span<const SkinBatch> meshes,std::span<const MeshInstance> imported,std::span<const Effect> effects) {
    return state_->draw(items,view,overlay,meshes,imported,effects);
}

void Renderer::reset_history() { state_->history_active = false; }

void Renderer::capture_next(std::filesystem::path file) { state_->capture_file = std::move(file); }

void Renderer::State::wait(std::uint64_t value) const {
    if (!value) return;
    auto info = vk<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    info.semaphoreCount = 1;
    info.pSemaphores = &timeline;
    info.pValues = &value;
    check(vkWaitSemaphores(device,&info,5'000'000'000ULL),"Wait for frame completion timeline");
}

void Renderer::State::record(Frame& frame, const Frame& previous, Screen& screen, const View& view,
                             const Mat4& camera, bool temporal, bool history, std::uint32_t count,
                             std::uint32_t dirty_layers,std::span<const ShadowDraw> shadows,
                             std::uint32_t overlay_count,std::span<const SkinDraw> meshes,std::span<const Effect> effects) {
    const auto cmd = frame.commands;
    auto begin = vk<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd,&begin),"Begin recording frame");
    if (frame.timer) {
        vkCmdResetQueryPool(cmd,frame.timer,0,6);
        vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,frame.timer,0);
    }
    const VkDeviceSize mesh_offset = view.dense_mesh ? 36*sizeof(gpu::Vertex) : 0;
    const std::uint32_t vertices = view.dense_mesh ? 576U : 36U;
    const auto draw_skin = [&](bool shadow_only) {
        if (meshes.empty()) return;
        const std::array buffers{frame.skin.buffer,frame.skin_instances.buffer,frame.mesh_motions.buffer};
        constexpr std::array<VkDeviceSize,3> offsets{};
        vkCmdBindVertexBuffers(cmd,0,shadow_only ? 2U : 3U,buffers.data(),offsets.data());
        for (std::uint32_t i=0; i<meshes.size(); ++i) {
            vkCmdDraw(cmd,meshes[i].count,1,meshes[i].first,i);
        }
    };
    const VkViewport viewport{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),0,1};
    const VkRect2D rect{{0,0},extent};

    gpu::Resource hdr{frame.hdr.image,VK_IMAGE_ASPECT_COLOR_BIT};
    gpu::Resource depth{frame.depth.image,VK_IMAGE_ASPECT_DEPTH_BIT};
    gpu::Resource shadow{frame.shadow.image,VK_IMAGE_ASPECT_DEPTH_BIT};
    gpu::Resource motion{frame.motion.image,VK_IMAGE_ASPECT_COLOR_BIT};
    gpu::Resource resolved{frame.history.image,VK_IMAGE_ASPECT_COLOR_BIT};
    gpu::Resource effect_color{frame.effects.image,VK_IMAGE_ASPECT_COLOR_BIT};
    if (frame.history_written) resolved = sampled_image(frame.history.image);
    if (frame.motion_sampled) motion = sampled_image(frame.motion.image);
    else if (frame.attachments_written) {
        motion = {frame.motion.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    }
    // Reusing a slot also needs a queue execution dependency across submissions.
    if (frame.attachments_written) {
        hdr = sampled_image(frame.hdr.image);
        effect_color = sampled_image(frame.effects.image);
        depth = {frame.depth.image,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT};
    }
    if (frame.shadow_written) {
        shadow = {frame.shadow.image,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    }
    gpu::Resource backbuffer{screen.image,VK_IMAGE_ASPECT_COLOR_BIT,
        screen.presented ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,0};
    hdr.label = "SCENE HDR";
    depth.label = "SCENE DEPTH";
    shadow.label = "SHADOW ATLAS";
    motion.label = "MOTION AND DEPTH";
    resolved.label = "CURRENT HISTORY";
    effect_color.label = "EFFECT HDR";
    backbuffer.label = "SWAPCHAIN";
    const gpu::RenderGraph graph(cmd,graph_trace);
    gpu::Resource textures{frame.textures.image,VK_IMAGE_ASPECT_COLOR_BIT};
    if (frame.textures_initialized) textures = sampled_image(frame.textures.image);
    textures.levels = MeshTexture::mips;
    textures.layers = MeshTexture::limit;
    textures.label = "MATERIAL ARRAY";
    if (dirty_layers || !frame.textures_initialized) {
        graph.pass("MATERIAL UPLOAD",{{textures,gpu::ImageUse::transfer_write}},[&] {
            std::array<VkBufferImageCopy,MeshTexture::limit*MeshTexture::mips> copies{};
            std::uint32_t copy_count = 0;
            for (std::uint32_t layer=0; layer<MeshTexture::limit; ++layer) {
                if (!(dirty_layers & (1U<<layer))) continue;
                VkDeviceSize offset = layer*MeshTexture::bytes;
                for (std::uint32_t mip=0; mip<MeshTexture::mips; ++mip) {
                    const auto side = MeshTexture::side>>mip;
                    auto& copy = copies[copy_count++];
                    copy.bufferOffset = offset;
                    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,mip,layer,1};
                    copy.imageExtent = {side,side,1};
                    offset += side*side*4;
                }
            }
            if (copy_count) {
                vkCmdCopyBufferToImage(cmd,frame.texture_staging.buffer,frame.textures.image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,copy_count,copies.data());
            }
        });
    }
    graph.pass("SHADOW ATLAS",{{shadow,gpu::ImageUse::depth_output}},[&] {
        constexpr auto tile = ShadowViews::tile_pixels;
        constexpr auto columns = ShadowViews::atlas_columns;
        const VkRect2D atlas{{0,0},{ShadowViews::atlas_pixels,ShadowViews::atlas_pixels}};
        auto depth_attachment = vk<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        depth_attachment.imageView = frame.shadow.view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.clearValue.depthStencil.depth = 1;
        auto rendering = vk<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea = atlas;
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &depth_attachment;
        vkCmdBeginRendering(cmd,&rendering);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,shadow_pipeline);
        for (std::uint32_t i=0; i<shadows.size(); ++i) {
            const auto& draw = shadows[i];
            const auto x = (i%columns)*tile, y = (i/columns)*tile;
            const VkRect2D area{{static_cast<int>(x),static_cast<int>(y)},{tile,tile}};
            const VkViewport shadow_view{static_cast<float>(x),static_cast<float>(y),tile,tile,0,1};
            vkCmdSetViewport(cmd,0,1,&shadow_view);
            vkCmdSetScissor(cmd,0,1,&area);
            const std::array buffers{geometry.buffer,frame.instances.buffer};
            const std::array<VkDeviceSize,2> offsets{mesh_offset,draw.first*sizeof(RenderItem)};
            vkCmdBindVertexBuffers(cmd,0,static_cast<std::uint32_t>(buffers.size()),buffers.data(),offsets.data());
            vkCmdPushConstants(cmd,shadow_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(draw.projection),&draw.projection);
            if (draw.count) vkCmdDraw(cmd,vertices,draw.count,0,0);
            draw_skin(true);
        }
        vkCmdEndRendering(cmd);
    });
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,1);
    vkCmdSetViewport(cmd,0,1,&viewport);
    vkCmdSetScissor(cmd,0,1,&rect);
    graph.pass("OPAQUE HDR",{{hdr,gpu::ImageUse::hdr_output},{motion,gpu::ImageUse::hdr_output},
                {depth,gpu::ImageUse::depth_output},{shadow,gpu::ImageUse::shadow_sampled},
                {textures,gpu::ImageUse::sampled}},[&] {
        const std::array colors{color_attachment(frame.hdr.view,{0.028f,0.044f,0.072f,1}),
                                color_attachment(frame.motion.view,{0,0,0,0})};
        auto depth_attachment = vk<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        depth_attachment.imageView = frame.depth.view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.clearValue.depthStencil.depth = 1;
        auto rendering = vk<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea = rect;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
        rendering.pColorAttachments = colors.data();
        rendering.pDepthAttachment = &depth_attachment;
        vkCmdBeginRendering(cmd,&rendering);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,scene_pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,scene_layout,0,1,&frame.light_set,0,nullptr);
        const std::array buffers{geometry.buffer,frame.instances.buffer,frame.motions.buffer};
        const std::array<VkDeviceSize,3> offsets{mesh_offset,0,0};
        vkCmdBindVertexBuffers(cmd,0,static_cast<std::uint32_t>(buffers.size()),buffers.data(),offsets.data());
        struct Camera {
            Mat4 projection;
            std::array<float,4> eye;
        } constants{camera,{view.eye.x,view.eye.y,view.eye.z,0}};
        static_assert(sizeof(Camera) == 80);
        vkCmdPushConstants(cmd,scene_layout,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,sizeof(constants),&constants);
        if (count) vkCmdDraw(cmd,vertices,count,0,0);
        draw_skin(false);
        vkCmdEndRendering(cmd);
    });
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,2);
    if (temporal) {
        const auto resolve = [&] {
            auto color = color_attachment(frame.history.view,{0,0,0,0});
            color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            auto rendering = vk<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
            rendering.renderArea = rect;
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            vkCmdBeginRendering(cmd,&rendering);
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,temporal_pipeline);
            const auto set = history ? frame.resolve_set : frame.resolve_first_set;
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,temporal_layout,0,1,&set,0,nullptr);
            vkCmdDraw(cmd,3,1,0,0);
            vkCmdEndRendering(cmd);
        };
        if (history) {
            auto prior = sampled_image(previous.history.image);
            prior.label = "PREVIOUS HISTORY";
            graph.pass("TEMPORAL RESOLVE",{{hdr,gpu::ImageUse::sampled},{motion,gpu::ImageUse::sampled},
                        {prior,gpu::ImageUse::sampled},{resolved,gpu::ImageUse::hdr_output}},resolve);
        } else {
            graph.pass("TEMPORAL RESOLVE",{{hdr,gpu::ImageUse::sampled},{motion,gpu::ImageUse::sampled},
                        {resolved,gpu::ImageUse::hdr_output}},resolve);
        }
    }
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,3);
    graph.pass("ADDITIVE EFFECTS",{{effect_color,gpu::ImageUse::hdr_output},{depth,gpu::ImageUse::depth_input}},[&] {
        const auto color = color_attachment(frame.effects.view,{0,0,0,0});
        auto opaque_depth = vk<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        opaque_depth.imageView = frame.depth.view;
        opaque_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        opaque_depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        opaque_depth.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
        auto rendering = vk<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea = rect;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &opaque_depth;
        vkCmdBeginRendering(cmd,&rendering);
        if (!effects.empty()) {
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,effects_pipeline);
            const float cotangent = 1/std::tan(view.fov*0.5f);
            const float aspect = static_cast<float>(extent.width)/static_cast<float>(extent.height);
            struct Emitter {
                Mat4 camera;
                std::array<float,4> position_age, color_radius;
                std::array<float,2> projection;
                std::uint32_t seed, kind;
            };
            static_assert(sizeof(Emitter)==112);
            for (const auto& effect : effects) {
                const Emitter emitter{camera,{effect.position.x,effect.position.y,effect.position.z,effect.age},
                    {effect.radiance.x,effect.radiance.y,effect.radiance.z,effect.radius},
                    {cotangent/aspect,-cotangent},effect.seed,static_cast<std::uint32_t>(effect.kind)};
                vkCmdPushConstants(cmd,effects_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(Emitter),&emitter);
                vkCmdDraw(cmd,6,effect.kind==EffectKind::burst ? 24U : 1U,0,0);
            }
        }
        vkCmdEndRendering(cmd);
    });
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,4);
    auto& display_image = temporal ? resolved : hdr;
    const auto display_set = temporal ? frame.history_sample : frame.sample;
    graph.pass("DISPLAY AND OVERLAY",{{display_image,gpu::ImageUse::sampled},{effect_color,gpu::ImageUse::sampled},
                                      {backbuffer,gpu::ImageUse::hdr_output}},[&] {
        auto color = color_attachment(screen.view,{0,0,0,1});
        auto rendering = vk<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea = rect;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmd,&rendering);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,tonemap_pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,tonemap_layout,0,1,&display_set,0,nullptr);
        const bool linear_target = screen_format == VK_FORMAT_B8G8R8A8_UNORM || screen_format == VK_FORMAT_R8G8B8A8_UNORM;
        struct Display { float exposure; std::int32_t antialias, srgb; };
        const Display display{std::clamp(view.exposure,0.05f,8.0f),view.antialias && !temporal,linear_target};
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
        graph.pass("FRAME READBACK",{{backbuffer,gpu::ImageUse::transfer_read}},[&] {
            auto barrier = vk<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = capture.buffer;
            barrier.size = VK_WHOLE_SIZE;
            auto dependency = vk<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd,&dependency);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            region.imageExtent = {extent.width,extent.height,1};
            vkCmdCopyImageToBuffer(cmd,screen.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,capture.buffer,1,&region);
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            vkCmdPipelineBarrier2(cmd,&dependency);
        });
    }
    graph.pass("PRESENT",{{backbuffer,gpu::ImageUse::present}},[]{});
    if (frame.timer) vkCmdWriteTimestamp2(cmd,VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,frame.timer,5);
    check(vkEndCommandBuffer(cmd),"Finish recording frame");
}

bool Renderer::State::draw(std::span<const RenderItem> items,const View& view,std::span<const OverlayVertex> overlay,
                           std::span<const SkinBatch> meshes,std::span<const MeshInstance> imported,std::span<const Effect> effects) {
    if (effects.size()>Effects::limit) throw std::runtime_error("Effect emitter limit");
    std::uint32_t particles = 0, decals = 0;
    for (const auto& effect : effects) {
        if (!finite(effect.position) || !finite(effect.radiance) ||
            !(effect.age>=0 && effect.age<=Effects::lifetime && effect.radius>=0.01f && effect.radius<=8.0f) ||
            std::max({effect.radiance.x,effect.radiance.y,effect.radiance.z})>16 ||
            std::min({effect.radiance.x,effect.radiance.y,effect.radiance.z})<0) {
            throw std::runtime_error("Effect emitter outside supported bounds");
        }
        if (effect.kind==EffectKind::burst) particles += 24;
        else if (effect.kind==EffectKind::ground_ring) ++decals;
        else throw std::runtime_error("Invalid effect kind");
    }
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
    const auto& previous = frames[(serial+slots-1)%slots];
    wait(frame.finished);
    double elapsed = 0;
    std::array<double,5> pass_gpu_ms{};
    const auto timed_serial = frame.finished;
    std::array<std::uint64_t,6> stamps{};
    if (timed_serial && frame.timer) {
        check(vkGetQueryPoolResults(device,frame.timer,0,6,sizeof(stamps),stamps.data(),sizeof(stamps[0]),
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),"Read completed GPU pass timestamps");
        const auto mask = timestamp_bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (1ULL<<timestamp_bits)-1;
        for (std::size_t i=0; i<pass_gpu_ms.size(); ++i) {
            pass_gpu_ms[i] = static_cast<double>((stamps[i+1]-stamps[i]) & mask)*properties.limits.timestampPeriod*1e-6;
            elapsed += pass_gpu_ms[i];
        }
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
    const Vec3 up{0,1,0};
    const auto forward = unit(view.target-view.eye);
    auto projection = perspective(view.fov,aspect,0.1f,view.far_plane);
    const auto camera_transform = look_at(view.eye,view.target,up);
    const auto cull_camera = projection*camera_transform;
    const bool temporal = view.temporal && view.antialias;
    const bool history = temporal && history_active && previous.history_written &&
        length(view.eye-previous_view.eye)<3.0f && dot(forward,previous_forward)>0.90f &&
        std::abs(view.fov-previous_view.fov)<0.015f && view.far_plane==previous_view.far_plane;
    if (!history) jitter_index = 0;
    if (temporal) {
        const auto sequence = jitter_index%8+1;
        projection(0,2) -= 2*(radical_inverse(sequence,2)-0.5f)/static_cast<float>(extent.width);
        projection(1,2) -= 2*(radical_inverse(sequence,3)-0.5f)/static_cast<float>(extent.height);
    }
    const auto camera = projection*camera_transform;
    const Vec3 direction = unit(Vec3{-0.45f,0.82f,0.24f});

    Lighting lighting{};
    lighting.previous_camera = history ? previous_camera : camera;
    lighting.viewport = {1.0f/static_cast<float>(extent.width),1.0f/static_cast<float>(extent.height),0,0};
    next_models.clear();
    std::uint32_t motion_items = 0;
    const auto motion_for = [&](std::uint64_t id,const Mat4& model) -> Motion {
        if (!temporal || !id) return {};
        const auto [slot,inserted] = next_models.emplace(id,model);
        if (!inserted && slot->second.data != model.data) throw std::logic_error("Conflicting render history id");
        const auto prior = previous_models.find(id);
        if (!history || prior == previous_models.end()) return {};
        ++motion_items;
        return prior->second;
    };
    std::array<LightSphere,light_limit> spheres{};
    std::size_t lamps = 0, local = light_limit, emitter = items.size();
    float nearest = std::numeric_limits<float>::infinity();
    for (std::size_t i=0; i<items.size() && lamps<light_limit; ++i) {
        const auto& item = items[i];
        if (item.emission<=0.6f) continue;
        const auto p = transform_point(item.model,{})+Vec3{0,0.2f,0};
        spheres[lamps] = {p,3.4f};
        const auto offset = p-view.target;
        const auto squared = dot(offset,offset);
        if (view.local_shadows && squared<nearest) {
            nearest = squared;
            local = lamps;
            emitter = i;
        }
        lighting.lights[lamps++] = {{p.x,p.y,p.z,3.4f},
            {item.color.x,item.color.y,item.color.z,std::min(item.emission,4.0f)*1.3f}};
    }
    const auto shadow = shadow_views({view.eye,forward,up,view.fov,aspect,0.1f,view.far_plane},direction,
                                    local<lamps ? std::optional{spheres[local]} : std::nullopt);
    const auto mark = [&](std::size_t start,std::size_t end) {
        for (std::size_t i=start; i<end; ++i) {
            const auto& model = items[i].model;
            std::uint16_t mask = (gpu::visible(camera,model) || (temporal && gpu::visible(cull_camera,model))) ? 1 : 0;
            for (std::size_t map=0; map<shadow.count; ++map) {
                if (map>=ShadowViews::cascade_count && i==emitter) continue;
                if (gpu::visible(shadow.projections[map],model)) {
                    mask = static_cast<std::uint16_t>(mask | (1U<<(map+1)));
                }
            }
            visibility[i] = mask;
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
    auto* upload = static_cast<std::byte*>(frame.instances.mapped);
    const auto upload_marked = [&](unsigned bit,std::uint32_t first,std::uint32_t budget) {
        std::uint32_t count = 0;
        for (std::size_t i=0; i<items.size(); ++i) {
            if (!(visibility[i] & (1U<<bit))) continue;
            if (count==budget) throw std::runtime_error("Frame shadow instance budget exceeded");
            std::memcpy(upload+(first+count)*sizeof(RenderItem),&items[i],sizeof(RenderItem));
            if (!bit) {
                if (items[i].texture_layers != std::array<std::uint32_t,3>{}) {
                    throw std::runtime_error("Texture layers must come from an imported mesh");
                }
                const auto motion = motion_for(items[i].history_id,items[i].model);
                std::memcpy(static_cast<std::byte*>(frame.motions.mapped)+count*sizeof(Motion),&motion,sizeof(motion));
            }
            ++count;
        }
        return count;
    };
    const auto count = upload_marked(0,0,instance_limit);
    std::array<ShadowDraw,ShadowViews::limit> shadow_draws{};
    std::uint32_t shadow_count = 0, nonempty_shadows = 0;
    for (std::uint32_t i=0; i<shadow.count; ++i) {
        const auto first = instance_limit+shadow_count;
        const auto amount = upload_marked(i+1,first,shadow_instance_limit-shadow_count);
        shadow_draws[i] = {shadow.projections[i],first,amount};
        shadow_count += amount;
        nonempty_shadows += amount>0 ? 1U : 0U;
    }
    lighting.shadow = shadow.projections;
    lighting.camera_forward = {forward.x,forward.y,forward.z,0.1f};
    lighting.cascade_ends = {shadow.cascade_ends[0],shadow.cascade_ends[1],shadow.cascade_ends[2],0};
    lighting.direction_count = {direction.x,direction.y,direction.z,static_cast<float>(lamps)};
    lighting.shadow_options = {ShadowViews::atlas_columns,ShadowViews::tile_pixels,static_cast<std::uint32_t>(local),0};
    tile_lights.build(camera,extent.width,extent.height,std::span(spheres).first(lamps));
    lighting.tile_grid = {tile_lights.columns(),tile_lights.rows(),TileLights::tile_size,0};
    const auto masks = tile_lights.masks();
    std::memcpy(frame.light_tiles.mapped,masks.data(),masks.size_bytes());
    static_assert(sizeof(Lighting) == 64*(ShadowViews::limit+1)+96+32*light_limit);
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
        const auto motion = motion_for(instance.history_id,instance.model);
        std::memcpy(static_cast<std::byte*>(frame.mesh_motions.mapped)+uploaded_draws*sizeof(Motion),&motion,sizeof(motion));
        ++uploaded_draws;
        uploaded_vertices += draw.count;
    };
    for (const auto& mesh : meshes) {
        if (mesh.vertices.size()>skin_vertex_limit-uploaded_vertices) throw std::runtime_error("Frame skin vertex budget");
        upload_mesh(mesh.vertices,{mesh.model,mesh.color,0});
    }
    const auto animated_vertices = uploaded_vertices;
    std::map<const MeshAsset*,std::uint32_t> atlas_bases;
    std::uint32_t texture_layers = 0, dirty_layers = 0;
    for (const auto& mesh : imported) {
        if (!mesh.asset) throw std::runtime_error("Missing draw mesh");
        const auto [base,added_asset] = atlas_bases.emplace(mesh.asset,texture_layers);
        if (added_asset) {
            if (mesh.asset->textures.size()>MeshTexture::limit-texture_layers) {
                throw std::runtime_error("Frame material texture limit exceeded");
            }
            for (const auto& layer : mesh.asset->textures) {
                if (layer.rgba.size()!=MeshTexture::bytes) throw std::runtime_error("Invalid material mip chain");
                auto* target = static_cast<char*>(frame.texture_staging.mapped)+texture_layers*MeshTexture::bytes;
                const auto bit = 1U<<texture_layers++;
                if (!(frame.resident_layers & bit) || std::memcmp(target,layer.rgba.data(),MeshTexture::bytes)!=0) {
                    // Staging no longer proves residency until this submission succeeds.
                    frame.resident_layers &= ~bit;
                    std::memcpy(target,layer.rgba.data(),MeshTexture::bytes);
                    dirty_layers |= bit;
                }
            }
        }
        for (const auto& primitive : mesh.asset->primitives) {
            const auto& material = primitive.material;
            const Vec3 color{mesh.tint.x*material.base_color.x,mesh.tint.y*material.base_color.y,mesh.tint.z*material.base_color.z};
            RenderItem draw_item{mesh.model,color,0,{material.emissive.x,material.emissive.y,material.emissive.z,material.roughness},
                                material.metallic,mesh.history_id};
            for (std::size_t i=0; i<material.textures.size(); ++i) {
                if (material.textures[i]>mesh.asset->textures.size()) throw std::runtime_error("Invalid material texture index");
                if (material.textures[i]) draw_item.texture_layers[i] = base->second+material.textures[i];
            }
            draw_item.normal_scale = material.normal_scale;
            upload_mesh(primitive.vertices,draw_item);
        }
    }
    record(frame,previous,screen,view,camera,temporal,history,count,dirty_layers,std::span(shadow_draws).first(shadow.count),static_cast<std::uint32_t>(overlay.size()),
           std::span(skin_draws).first(uploaded_draws),effects);
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
    frame.history_written = frame.history_written || temporal;
    frame.motion_sampled = temporal;
    frame.textures_initialized = true;
    frame.resident_layers |= dirty_layers;
    history_active = temporal;
    previous_camera = camera;
    previous_view = view;
    previous_forward = forward;
    ++jitter_index;
    previous_models.swap(next_models);
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
    const unsigned draws = 1 + static_cast<unsigned>(temporal) + static_cast<unsigned>(count>0) + nonempty_shadows + static_cast<unsigned>(!overlay.empty()) +
                           static_cast<unsigned>(shadow.count+1)*static_cast<unsigned>(uploaded_draws)+static_cast<unsigned>(effects.size());
    auto owned = geometry.allocated+capture.allocated;
    for (const auto& slot : frames) {
        owned += slot.instances.allocated+slot.overlay.allocated+slot.lighting.allocated+slot.skin.allocated+
                 slot.skin_instances.allocated+slot.light_tiles.allocated+slot.motions.allocated+slot.mesh_motions.allocated+
                 slot.texture_staging.allocated+slot.hdr.allocated+slot.depth.allocated+slot.shadow.allocated+
                 slot.motion.allocated+slot.history.allocated+slot.textures.allocated+slot.effects.allocated;
    }
    report = {extent.width,extent.height,count,static_cast<std::uint32_t>(items.size())-count,elapsed,serial,
              shadow_count,static_cast<std::uint32_t>(lamps),triangles,draws,timed_serial,stamps,
              properties.limits.timestampPeriod,timestamp_bits,(uploaded_vertices-animated_vertices)/3,
              static_cast<std::uint32_t>(masks.size()),tile_lights.entries(),static_cast<std::uint32_t>(shadow.count),nonempty_shadows,motion_items,animated_vertices/3,history,texture_layers,static_cast<std::uint32_t>(std::popcount(dirty_layers)),pass_gpu_ms,owned,particles,decals};
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
