// The window scaling on the present path (vk_present.c present_image): how the picture, the crop rectangle of the
// game's image, reaches the swapchain when the plain bilinear vkCmdBlitImage is not enough. The setting window_filter
// (window_filter.h wf_resolve, wf_plan) picks, TOTK_SHARPNESS (0..100) sets the sharpen; both apply at the next frame.
//  - fsr1 (the default), with a window larger than an SDR picture: AMD FidelityFX Super Resolution 1, EASU to the
//    swapchain size, then RCAS when the sharpness is above 0, as before; at any other size, and for the game's own HDR
//    picture, nothing (the caller blits, as before). upscaler = bilinear with no filter chosen stays the plain blit.
//  - linear, catmull_rom, lanczos, when the player chooses one: runtime/shaders/win_scale.comp, one compute pass that
//    filters the picture to the window (each filter stretched over the picture's pixels one window pixel covers when the
//    picture is the larger) and then sharpens it with the slider, at any size, 1:1 included. Linear with the slider at
//    0 stays the plain blit: that is the same filter, with no pass.
//  - nothing needed (the window is the picture's size, the slider is 0): no pass, no images; the caller blits.
//  - native HDR (pq: the game's own HDR picture, Rec. 2020 PQ code values in A2B10G10R10), with one of those three:
//    the same plan through win_scale_pq (win_scale.comp with WIN_SCALE_PQ), which filters and sharpens the codes and
//    writes an RGB10A2 image. A GPU without storage-image support for A2B10G10R10 blits the picture unchanged.
// Every upscaler (the game's own, DLSS, FSR 3.1, FSR 4) gets the same window scaling and sharpening: vk_fsr3.c and
// vk_fsr4.c (vk_fsr4_record) leave the sharpening to this pass, at 1:1 with a filter other than FSR 1 chosen.
//
// Per frame: the pass reads the source through a view of its own format (the same colour conversion the blit does) and
// writes an RGBA16F image (RGB10A2 for the HDR picture) of the picture's size on the swapchain (all of it, or the part
// between black bars, gfx_window.c fit_rect); EASU+RCAS use a second image for RCAS; a 1:1 blit converts the result
// into its place in the swapchain image, which the caller left in TRANSFER_DST. The shaders
// (runtime/shaders/fsr_*.comp, win_scale.comp) compute AMD's constants from the push constants. Cost: one or two
// compute passes over the swapchain size and one copy per present; nothing on the game's draws. The images exist only
// while a frame needs them: the first frame that needs none gives them back.
//
// The source view of slot s is destroyed when slot s records again: present_image has waited for that slot's fence.
// The two intermediate images are shared by all slots; each frame's first barrier orders it after the previous
// frame's reads (one queue, so the barrier covers earlier submissions).
#include "vk.h"

#include <string.h>

#include "cfg.h"
#include "fsr_easu_spv.h"
#include "fsr_rcas_spv.h"
#include "host.h"
#include "win_scale_pq_spv.h"
#include "win_scale_spv.h"
#include "window_filter.h"

#define SLOTS 8

static struct {
    int tried, ok;
    PFN_vkCmdPushDescriptorSetKHR push;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkPipeline easu, rcas, win, win_pq;
    int pq_tried;                  // win_pq was tried (it is made at the first native HDR frame that needs it)
    VkFormat fmt;                  // the images' format: RGBA16F, or A2B10G10R10 for the HDR picture
    VkSampler sampler;
    VkImage img[2];                // EASU output, RCAS output
    VmaAllocation alloc[2];
    VkImageView view[2];
    uint32_t w, h;                 // their size
    VkImageView src_view[SLOTS];   // the source view each slot's last frame read
    int logged;                    // the last result logged (vk_fsr_record)
} F;

static VkPipeline make_pipeline(VkCtx *c, const uint32_t *code, size_t size) {
    VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code};
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(c->device, &mi, NULL, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = m, .pName = "main"},
                                      .layout = F.layout};
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &ci, NULL, &p) != VK_SUCCESS) p = VK_NULL_HANDLE;
    vkDestroyShaderModule(c->device, m, NULL);
    return p;
}

static int init(VkCtx *c) {
    F.tried = 1;
    F.push = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(c->device, "vkCmdPushDescriptorSetKHR");
    if (!F.push) {
        rt_log("vk: window scaling passes off: no VK_KHR_push_descriptor; the present scales bilinearly");
        return 0;
    }
    VkDescriptorSetLayoutBinding b[2] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                           .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                           .bindingCount = 2, .pBindings = b};
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                     .pSetLayouts = &F.dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pr};
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR,
                              .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0};
    if (vkCreateDescriptorSetLayout(c->device, &dci, NULL, &F.dsl) != VK_SUCCESS ||
        vkCreatePipelineLayout(c->device, &li, NULL, &F.layout) != VK_SUCCESS ||
        vkCreateSampler(c->device, &si, NULL, &F.sampler) != VK_SUCCESS ||
        !(F.easu = make_pipeline(c, fsr_easu_spv, sizeof fsr_easu_spv)) ||
        !(F.rcas = make_pipeline(c, fsr_rcas_spv, sizeof fsr_rcas_spv)) ||
        !(F.win = make_pipeline(c, win_scale_spv, sizeof win_scale_spv))) {
        rt_log("vk: window scaling passes off: their pipelines could not be created; the present scales bilinearly");
        return 0;
    }
    rt_log("vk: window scaling (FSR 1 EASU + RCAS, win_scale) ready for the present");
    return F.ok = 1;
}

// The pass for the game's own HDR picture, made at its first use: needs A2B10G10R10 as a storage image
static VkPipeline pq_pipeline(VkCtx *c) {
    if (!F.pq_tried) {
        F.pq_tried = 1;
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(c->phys, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                          VK_FORMAT_FEATURE_BLIT_SRC_BIT;
        if ((fp.optimalTilingFeatures & need) != need)
            rt_log("vk: window scaling of the game's own HDR picture off: no storage image of A2B10G10R10; it is blitted unchanged");
        else if (!(F.win_pq = make_pipeline(c, win_scale_pq_spv, sizeof win_scale_pq_spv)))
            rt_log("vk: window scaling of the game's own HDR picture off: its pipeline could not be created; it is blitted unchanged");
    }
    return F.win_pq;
}

static void free_images(VkCtx *c) {
    for (int i = 0; i < 2; i++) {
        if (F.view[i]) vkDestroyImageView(c->device, F.view[i], NULL);
        if (F.img[i]) vmaDestroyImage(c->vma, F.img[i], F.alloc[i]);
        F.view[i] = VK_NULL_HANDLE;
        F.img[i] = VK_NULL_HANDLE;
    }
    F.w = F.h = 0;
    F.fmt = VK_FORMAT_UNDEFINED;
}

// n images of w x h in format fmt exist afterwards (the pass needs one, EASU + RCAS two)
static int ensure_images(VkCtx *c, uint32_t w, uint32_t h, int n, VkFormat fmt) {
    if (F.img[n - 1] && F.w == w && F.h == h && F.fmt == fmt) return 1;
    // A swapchain size change: earlier presents may still read the old images. vkDeviceWaitIdle needs every queue
    // externally synchronized, so it holds the queue lock like a submission.
    vk_queue_lock();
    vkDeviceWaitIdle(c->device);
    vk_queue_unlock();
    free_images(c);
    for (int i = 0; i < n; i++) {
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                                .format = fmt, .extent = {w, h, 1}, .mipLevels = 1,
                                .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        if (vmaCreateImage(c->vma, &ci, &ai, &F.img[i], &F.alloc[i], NULL) != VK_SUCCESS) {
            F.img[i] = VK_NULL_HANDLE;
            free_images(c);
            return 0;
        }
        VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = F.img[i],
                                    .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        if (vkCreateImageView(c->device, &vi, NULL, &F.view[i]) != VK_SUCCESS) {
            F.view[i] = VK_NULL_HANDLE;
            free_images(c);
            return 0;
        }
    }
    F.w = w;
    F.h = h;
    F.fmt = fmt;
    return 1;
}

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 ss,
                    VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = ss, .srcAccessMask = sa,
                               .dstStageMask = ds, .dstAccessMask = da, .oldLayout = from, .newLayout = to,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
                               .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1,
                          .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &d);
}

static void bind_pass(VkCommandBuffer cb, VkPipeline p, VkImageView in, VkImageView out, const void *pc) {
    VkDescriptorImageInfo ii = {F.sampler, in, VK_IMAGE_LAYOUT_GENERAL}, oi = {VK_NULL_HANDLE, out, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &oi}};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    rec_push(F.push, cb, VK_PIPELINE_BIND_POINT_COMPUTE, F.layout, 0, 2, w);
    vkCmdPushConstants(cb, F.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 32, pc);
}

// The window pass's push constants (win_scale.comp P)
typedef struct { int crop[4]; int mode; float sharp; int pad[2]; } WinPush;

int vk_fsr_record(VkCommandBuffer cb, unsigned slot, VkImage src, VkFormat fmt, int pq, int sw, int sh,
                  const int crop[4], VkImage dst, const int out[4]) {
    int cw = crop[2], ch = crop[3];
    uint32_t dw = (uint32_t)out[2], dh = (uint32_t)out[3];
    // window_filter (the legacy upscaler = bilinear alias included) and the slider decide what runs, whichever
    // upscaler made the picture
    int filter = wf_resolve(cfg_value("TOTK_WINDOW_FILTER"), cfg_is_set("TOTK_WINDOW_FILTER"), cfg_value("TOTK_UPSCALER"));
    WfPlan plan = wf_plan(filter, cfg_int("TOTK_SHARPNESS"), pq, cw, ch, (int)dw, (int)dh);
    VkCtx *c = vk_ctx();
    if (!fmt || (!plan.fsr1 && !plan.pass)) {
        vk_fsr_idle();
        return 0;
    }
    if (!F.tried) init(c);
    if (!F.ok) return 0;
    VkPipeline win = pq ? pq_pipeline(c) : F.win;
    if (!win) {   // the HDR picture's pass is not available here: the caller blits it unchanged
        vk_fsr_idle();
        return 0;
    }
    if (!ensure_images(c, dw, dh, plan.fsr1 ? 2 : 1,
                       pq ? VK_FORMAT_A2B10G10R10_UNORM_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT))
        return 0;
    slot %= SLOTS;
    if (F.src_view[slot]) vkDestroyImageView(c->device, F.src_view[slot], NULL);
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = src,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                                .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_ONE},
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(c->device, &vi, NULL, &F.src_view[slot]) != VK_SUCCESS) {
        F.src_view[slot] = VK_NULL_HANDLE;
        return 0;
    }
    int sharp = plan.sharp;
    int result = plan.fsr1 ? 2 : 3 + plan.mode;   // vk_present.c g_scaler: 2 FSR 1, 3 linear, 4 Catmull-Rom, 5 Lanczos
    int logged = result | (pq ? 8 : 0);
    if (F.logged != logged) {
        F.logged = logged;
        static const char *const names[] = {"", "", "FSR 1", "linear", "Catmull-Rom", "Lanczos 2"};
        rt_log("vk: window scaling %s%s from %dx%d to %ux%u (sharpness %d)", names[result],
               pq ? " of the game's own HDR picture (PQ codes, RGB10A2)" : "", cw, ch, dw, dh, sharp);
    }
    const VkPipelineStageFlags2 CS = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, XF = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    // the game's writes to the source become visible to the compute read; the earlier frame's reads of the
    // intermediates finish before this frame writes them
    barrier(cb, src, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_WRITE_BIT, CS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    const int images = plan.fsr1 ? 2 : 1;   // the window pass writes one image, FSR 1's EASU and RCAS two
    for (int i = 0; i < images; i++)
        barrier(cb, F.img[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, CS | XF, 0, CS,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    int last = 0;
    if (plan.fsr1) {
        uint32_t gx = (dw + 15) / 16, gy = (dh + 15) / 16;
        float easu[8] = {(float)cw, (float)ch, (float)sw, (float)sh, (float)dw, (float)dh, (float)crop[0], (float)crop[1]};
        bind_pass(cb, F.easu, F.src_view[slot], F.view[0], easu);
        vkCmdDispatch(cb, gx, gy, 1);
        if (sharp > 0) {
            barrier(cb, F.img[0], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, CS, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    CS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            float rcas[8] = {(float)dw, (float)dh, (float)(100 - sharp) / 50.0f, 0, 0, 0, 0, 0};
            bind_pass(cb, F.rcas, F.view[0], F.view[1], rcas);
            vkCmdDispatch(cb, gx, gy, 1);
            last = 1;
        }
    } else {   // the window pass: the filter's scaling and the sharpening in one dispatch (win_scale.comp)
        WinPush wp = {{crop[0], crop[1], cw, ch}, plan.mode, (float)sharp / 100.0f, {0, 0}};
        bind_pass(cb, win, F.src_view[slot], F.view[0], &wp);
        vkCmdDispatch(cb, (dw + 7) / 8, (dh + 7) / 8, 1);
    }
    barrier(cb, F.img[last], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, CS, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            XF, VK_ACCESS_2_TRANSFER_READ_BIT);
    // later submissions that write the source wait for the compute read
    barrier(cb, src, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, CS, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    VkImageBlit region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .srcOffsets = {{0, 0, 0}, {(int)dw, (int)dh, 1}},
                          .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .dstOffsets = {{out[0], out[1], 0}, {out[0] + (int)dw, out[1] + (int)dh, 1}}};
    vkCmdBlitImage(cb, F.img[last], VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                   VK_FILTER_NEAREST);
    return result;
}

// The present needs no scaling pass this frame (nothing to do, or the pass is not available): the images go back, made
// again when a frame asks for them.
void vk_fsr_idle(void) {
    if (!F.img[0]) return;
    VkCtx *c = vk_ctx();
    vk_queue_lock();   // vkDeviceWaitIdle needs every queue left alone, and the replay submits from its own thread
    vkDeviceWaitIdle(c->device);
    vk_queue_unlock();
    free_images(c);
}

void vk_fsr_release(void) {
    VkCtx *c = vk_ctx();
    if (!c || !F.tried) return;
    free_images(c);
    for (int i = 0; i < SLOTS; i++)
        if (F.src_view[i]) vkDestroyImageView(c->device, F.src_view[i], NULL);
    if (F.easu) vkDestroyPipeline(c->device, F.easu, NULL);
    if (F.rcas) vkDestroyPipeline(c->device, F.rcas, NULL);
    if (F.win) vkDestroyPipeline(c->device, F.win, NULL);
    if (F.win_pq) vkDestroyPipeline(c->device, F.win_pq, NULL);
    if (F.sampler) vkDestroySampler(c->device, F.sampler, NULL);
    if (F.layout) vkDestroyPipelineLayout(c->device, F.layout, NULL);
    if (F.dsl) vkDestroyDescriptorSetLayout(c->device, F.dsl, NULL);
    memset(&F, 0, sizeof F);
}
