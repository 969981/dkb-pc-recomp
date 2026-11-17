// CRT monitor and TV filters on the present (TOTK_CRT_FILTER = lottes_grille|lottes_tv|hyllian|zfast; off by default),
// ported from Hookshot (gpu/vk/vk_framegen.cpp init_crt and record_crt, gpu/vk/crt_policy.h raster and the four shaders
// gpu/vk/shaders/crt_*.comp), commit 709cbf8c "feat(vk): add selectable CRT monitor and TV filters". Hookshot is
// GPL-3.0; the shaders adapt Timothy Lottes' crt-lottes (public domain), Hyllian's crt-hyllian-fast (MIT) and Greg
// Hogan's zfast (GPL-2.0-or-later) from libretro/slang-shaders at 2345ee91602a7404d3a1a6a5df6108d7305fdad4, and keep
// their notices.
//
// The filter runs on the finished picture at the swapchain size, after FSR or the bilinear blit has scaled it and after
// SMAA, before the menu overlay is drawn, so the HUD of the game is filtered and the overlay is not. vk_present.c
// present_image calls it with the swapchain image (with lifted HDR, vk_hdr.c's 8-bit image, so the filter runs before
// the HDR pass) in TRANSFER_DST. The picture's rectangle is copied into an RGBA8 image and crt_downsample.comp reduces
// it, by exact area weights, to the virtual raster (240, 480 or 720 lines, see raster_size). The chosen filter shader
// then draws the raster at the picture's size as scanlines and a mask, and the result is blitted back 1:1. These are
// SDR shaders: present_image does not call vk_crt_record for the game's own HDR (PQ) picture, and calls vk_crt_idle
// instead, which logs the skip once. Resources are made at the first use of a filter and only for the one chosen;
// turning the filter off hands the images back. Any failure turns the filter off for the session and the picture goes
// out unfiltered. TOTK_CRT_LINES (240, 480 or 720) applies at the next frame.
#include "vk.h"

#include <string.h>

#include "cfg.h"
#include "crt_downsample_spv.h"
#include "crt_hyllian_spv.h"
#include "crt_lottes_spv.h"
#include "crt_zfast_spv.h"
#include "host.h"

#define CRT_FMT VK_FORMAT_R8G8B8A8_UNORM

// Hookshot's modes 0 to 4; the Lottes shader serves two of them (its style push constant), so three pipelines
enum { MODE_OFF, MODE_LOTTES_GRILLE, MODE_LOTTES_TV, MODE_HYLLIAN, MODE_ZFAST, MODES };
enum { SH_LOTTES, SH_HYLLIAN, SH_ZFAST, SHADERS };
enum { PICTURE, RASTER, OUTPUT, IMAGES };

typedef struct {
    VkImage img;
    VmaAllocation alloc;
    VkImageView view;
} Image;

// the push constants of the three filter shaders, 36 bytes as in Hookshot: source and output size as
// {width, height, 1/width, 1/height}, and the mode (Lottes: 1 flat aperture grille, 2 curved consumer TV)
typedef struct {
    float source[4], output[4];
    uint32_t style;
} CrtPush;
_Static_assert(sizeof(CrtPush) == 36, "the crt shaders' push constants are 36 bytes");

static struct {
    int tried, ok;
    PFN_vkCmdPushDescriptorSetKHR push;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkPipeline downsample, filter[SHADERS];
    int filter_tried[SHADERS];
    VkSampler nearest, linear;
    Image img[IMAGES];
    uint32_t pw, ph, rw, rh;               // the picture and the virtual raster the images are made for
    uint32_t failed_pw, failed_ph, failed_rw, failed_rh;   // the sizes the images could not be allocated at
    int log_mode, log_skip;
    uint32_t log_w, log_h, log_lines;
} S;

static VkPipeline make_pipeline(VkCtx *c, const uint32_t *code, size_t size) {
    VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size,
                                   .pCode = code};
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(c->device, &mi, NULL, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = m, .pName = "main"},
                                      .layout = S.layout};
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &ci, NULL, &p) != VK_SUCCESS) p = VK_NULL_HANDLE;
    vkDestroyShaderModule(c->device, m, NULL);
    return p;
}

static void free_image(VkCtx *c, Image *i) {
    if (i->view) vkDestroyImageView(c->device, i->view, NULL);
    if (i->img) vmaDestroyImage(c->vma, i->img, i->alloc);
    memset(i, 0, sizeof *i);
}

static int make_image(VkCtx *c, Image *i, uint32_t w, uint32_t h, VkImageUsageFlags usage) {
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = CRT_FMT,
                            .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(c->vma, &ci, &ai, &i->img, &i->alloc, NULL) != VK_SUCCESS) {
        i->img = VK_NULL_HANDLE;
        return 0;
    }
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = i->img,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = CRT_FMT,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(c->device, &vi, NULL, &i->view) != VK_SUCCESS) {
        i->view = VK_NULL_HANDLE;
        free_image(c, i);
        return 0;
    }
    return 1;
}

static void wait_idle(VkCtx *c) {
    vk_queue_lock();   // vkDeviceWaitIdle needs every queue left alone, and the replay submits from its own thread
    vkDeviceWaitIdle(c->device);
    vk_queue_unlock();
}

static void free_images(VkCtx *c) {
    for (int i = 0; i < IMAGES; i++) free_image(c, &S.img[i]);
    S.pw = S.ph = S.rw = S.rh = 0;
}

// Hookshot crt_policy::raster: the virtual raster of the output, lines high (240, 480 or 720) and as wide as the
// output's shape asks. At least two output pixels per virtual line, because more lines than that alias on a small
// output.
static void raster_size(uint32_t ow, uint32_t oh, int lines, uint32_t *rw, uint32_t *rh) {
    uint32_t cap = oh / 2 > 1 ? oh / 2 : 1;
    uint32_t h = (uint32_t)lines < cap ? (uint32_t)lines : cap;
    uint32_t w = (uint32_t)(((uint64_t)ow * h + oh / 2) / oh);
    *rw = w ? w : 1;
    *rh = h;
}

static int init(VkCtx *c) {
    S.tried = 1;
    S.push = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(c->device, "vkCmdPushDescriptorSetKHR");
    if (!S.push) {
        rt_log("vk: CRT filter off: no VK_KHR_push_descriptor");
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
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CrtPush)};
    VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                     .pSetLayouts = &S.dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pr};
    // clamp to an opaque black border, as Hookshot: the tube's edge is black, not a smear of the last row
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST,
                              .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                              .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                              .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                              .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, .maxLod = 0};
    VkSamplerCreateInfo li_s = si;
    li_s.magFilter = li_s.minFilter = VK_FILTER_LINEAR;   // zfast reads the raster bilinearly
    if (vkCreateDescriptorSetLayout(c->device, &dci, NULL, &S.dsl) != VK_SUCCESS ||
        vkCreatePipelineLayout(c->device, &li, NULL, &S.layout) != VK_SUCCESS ||
        vkCreateSampler(c->device, &si, NULL, &S.nearest) != VK_SUCCESS ||
        vkCreateSampler(c->device, &li_s, NULL, &S.linear) != VK_SUCCESS ||
        !(S.downsample = make_pipeline(c, crt_downsample_spv, sizeof crt_downsample_spv))) {
        rt_log("vk: CRT filter off: its pipeline or samplers could not be created");
        return 0;
    }
    return S.ok = 1;
}

// the pipeline of one filter shader, made the first time a mode needs it; 0 turns the filter off
static int ensure_filter(VkCtx *c, int shader) {
    if (!S.filter_tried[shader]) {
        S.filter_tried[shader] = 1;
        S.filter[shader] = shader == SH_LOTTES   ? make_pipeline(c, crt_lottes_spv, sizeof crt_lottes_spv)
                           : shader == SH_HYLLIAN ? make_pipeline(c, crt_hyllian_spv, sizeof crt_hyllian_spv)
                                                  : make_pipeline(c, crt_zfast_spv, sizeof crt_zfast_spv);
        if (!S.filter[shader]) {
            S.ok = 0;
            rt_log("vk: CRT filter off: shader %d could not be created; the picture goes out unfiltered", shader);
        }
    }
    return S.filter[shader] != VK_NULL_HANDLE;
}

static int ensure_images(VkCtx *c, uint32_t pw, uint32_t ph, uint32_t rw, uint32_t rh) {
    if (S.img[PICTURE].img && S.pw == pw && S.ph == ph && S.rw == rw && S.rh == rh) return 1;
    if (S.failed_pw == pw && S.failed_ph == ph && S.failed_rw == rw && S.failed_rh == rh) return 0;
    wait_idle(c);
    free_images(c);
    const VkImageUsageFlags work = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!make_image(c, &S.img[PICTURE], pw, ph, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !make_image(c, &S.img[RASTER], rw, rh, work) ||
        !make_image(c, &S.img[OUTPUT], pw, ph, work | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        free_images(c);
        S.failed_pw = pw, S.failed_ph = ph, S.failed_rw = rw, S.failed_rh = rh;
        S.ok = 0;   // as Hookshot: an allocation failure ends the filter for the session
        rt_log("vk: CRT filter off: no memory for its %ux%u images; the picture goes out unfiltered", pw, ph);
        return 0;
    }
    S.pw = pw, S.ph = ph, S.rw = rw, S.rh = rh;
    return 1;
}

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 ss,
                    VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = ss,
                               .srcAccessMask = sa,
                               .dstStageMask = ds, .dstAccessMask = da, .oldLayout = from, .newLayout = to,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
                               .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1,
                          .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &d);
}

// one compute pass over w x h invocations: in (binding 0, sampled with smp) to out (binding 1); push is the shader's
// push constants, or NULL for the downsample, which has none
static void run_pass(VkCommandBuffer cb, VkPipeline p, VkSampler smp, VkImageView in, VkImageView out, uint32_t w,
                     uint32_t h, const CrtPush *push) {
    VkDescriptorImageInfo ii = {smp, in, VK_IMAGE_LAYOUT_GENERAL}, oi = {VK_NULL_HANDLE, out, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &oi}};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    rec_push(S.push, cb, VK_PIPELINE_BIND_POINT_COMPUTE, S.layout, 0, 2, wr);
    if (push) vkCmdPushConstants(cb, S.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof *push, push);
    vkCmdDispatch(cb, (w + 7) / 8, (h + 7) / 8, 1);
}

static int parse_mode(const char *s) {
    return !strcmp(s, "lottes_grille") ? MODE_LOTTES_GRILLE : !strcmp(s, "lottes_tv") ? MODE_LOTTES_TV
           : !strcmp(s, "hyllian")     ? MODE_HYLLIAN       : !strcmp(s, "zfast")     ? MODE_ZFAST : MODE_OFF;
}

int vk_crt_record(VkCommandBuffer cb, VkImage dst, const int to[4]) {
    int mode = parse_mode(cfg_value("TOTK_CRT_FILTER"));
    VkCtx *c = vk_ctx();
    if (mode == MODE_OFF || (S.tried && !S.ok)) {   // turned off: hand the images back until it is turned on again
        if (S.img[PICTURE].img) {
            wait_idle(c);
            free_images(c);
        }
        return 0;
    }
    S.log_skip = 0;
    if (!S.tried && !init(c)) return 0;
    int shader = mode <= MODE_LOTTES_TV ? SH_LOTTES : mode == MODE_HYLLIAN ? SH_HYLLIAN : SH_ZFAST;
    int tx = to[0], ty = to[1], tw = to[2], th = to[3];
    if (tw <= 0 || th <= 0 || !ensure_filter(c, shader)) return 0;
    // 240 (Hookshot's default), 480 or 720 virtual lines; any other value counts as 240
    int req = cfg_int("TOTK_CRT_LINES");
    uint32_t rw, rh;
    raster_size((uint32_t)tw, (uint32_t)th, req == 480 || req == 720 ? req : 240, &rw, &rh);
    if (!ensure_images(c, (uint32_t)tw, (uint32_t)th, rw, rh)) return 0;
    if (S.log_mode != mode || S.log_w != (uint32_t)tw || S.log_h != (uint32_t)th || S.log_lines != rh) {
        static const char *const names[MODES] = {"", "crt-lottes-aperture", "crt-lottes-tv", "crt-hyllian",
                                                 "crt-zfast"};
        S.log_mode = mode, S.log_w = (uint32_t)tw, S.log_h = (uint32_t)th, S.log_lines = rh;
        rt_log("vk: CRT %s: %ux%u virtual raster, %dx%d output", names[mode], rw, rh, tw, th);
    }

    const VkPipelineStageFlags2 CS = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, XF = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                ALL = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    const VkAccessFlags2 WRITE = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, READ = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    const VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL, DST = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        SRC = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    // the swapchain image holds the scaled picture (the caller's last write is a transfer); copy its rectangle out
    barrier(cb, dst, DST, SRC, XF, VK_ACCESS_2_TRANSFER_WRITE_BIT, XF, VK_ACCESS_2_TRANSFER_READ_BIT);
    for (int i = 0; i < IMAGES; i++)   // the earlier frame's reads of the images finish before this frame writes them
        barrier(cb, S.img[i].img, VK_IMAGE_LAYOUT_UNDEFINED, G, ALL, 0, CS | XF,
                VK_ACCESS_2_TRANSFER_WRITE_BIT | WRITE | READ);
    VkImageBlit region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .srcOffsets = {{tx, ty, 0}, {tx + tw, ty + th, 1}},
                          .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .dstOffsets = {{0, 0, 0}, {tw, th, 1}}};
    vkCmdBlitImage(cb, dst, SRC, S.img[PICTURE].img, G, 1, &region, VK_FILTER_NEAREST);
    barrier(cb, S.img[PICTURE].img, G, G, XF, VK_ACCESS_2_TRANSFER_WRITE_BIT, CS, READ);

    // the finished picture, not the 3D before its upscaler, is reduced to the tube's raster
    run_pass(cb, S.downsample, S.nearest, S.img[PICTURE].view, S.img[RASTER].view, rw, rh, NULL);
    barrier(cb, S.img[RASTER].img, G, G, CS, WRITE, CS, READ);

    CrtPush pc = {{(float)rw, (float)rh, 1.0f / (float)rw, 1.0f / (float)rh},
                  {(float)tw, (float)th, 1.0f / (float)tw, 1.0f / (float)th}, (uint32_t)mode};
    run_pass(cb, S.filter[shader], mode == MODE_ZFAST ? S.linear : S.nearest, S.img[RASTER].view, S.img[OUTPUT].view,
             (uint32_t)tw, (uint32_t)th, &pc);

    // back into the swapchain image 1:1, which the caller finds in TRANSFER_DST as before
    barrier(cb, S.img[OUTPUT].img, G, G, CS, WRITE, XF, VK_ACCESS_2_TRANSFER_READ_BIT);
    barrier(cb, dst, SRC, DST, XF, 0, XF, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageBlit back = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .srcOffsets = {{0, 0, 0}, {tw, th, 1}},
                        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .dstOffsets = {{tx, ty, 0}, {tx + tw, ty + th, 1}}};
    vkCmdBlitImage(cb, S.img[OUTPUT].img, G, dst, DST, 1, &back, VK_FILTER_NEAREST);
    return 1;
}

// The game's own HDR picture bypasses the filter (vk_present.c present_image): the images go back as when it is turned
// off, made again when an SDR picture asks for the filter once more. With a filter chosen, the first skipped frame logs
// a line; vk_crt_record arms it again.
void vk_crt_idle(void) {
    if (parse_mode(cfg_value("TOTK_CRT_FILTER")) != MODE_OFF && !S.log_skip && !(S.tried && !S.ok)) {
        S.log_skip = 1;
        rt_log("vk: CRT filter skipped: the game's own HDR picture is not an SDR picture");
    }
    if (!S.img[PICTURE].img) return;
    VkCtx *c = vk_ctx();
    wait_idle(c);
    free_images(c);
}

void vk_crt_release(void) {
    VkCtx *c = vk_ctx();
    if (!c || !S.tried) return;
    free_images(c);
    if (S.downsample) vkDestroyPipeline(c->device, S.downsample, NULL);
    for (int i = 0; i < SHADERS; i++)
        if (S.filter[i]) vkDestroyPipeline(c->device, S.filter[i], NULL);
    if (S.nearest) vkDestroySampler(c->device, S.nearest, NULL);
    if (S.linear) vkDestroySampler(c->device, S.linear, NULL);
    if (S.layout) vkDestroyPipelineLayout(c->device, S.layout, NULL);
    if (S.dsl) vkDestroyDescriptorSetLayout(c->device, S.dsl, NULL);
    memset(&S, 0, sizeof S);
}
