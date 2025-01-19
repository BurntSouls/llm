#include "llama.h"
#include "llama-vision.h"
#include "llama-impl.h"
#include "llama-context.h"

#include <string.h> // memcpy
#include <limits>
#include <cmath>

#ifndef NDEBUG
// for debugging
#include <fstream>
#include <cstdint>
#include <iostream>

// export clip_image_u8 to bmp file for debugging
// https://codereview.stackexchange.com/questions/195121/writing-a-bitmap-image-from-c
struct clip_image_size;
static int bmp_export(const struct clip_image_u8 &img, const std::string &location);
#endif

struct clip_image_size {
    int width;
    int height;
};

// RGB uint8 image
// Memory layout: RGBRGBRGB...
struct clip_image_u8 {
    int nx;
    int ny;
    std::vector<uint8_t> buf;
    clip_image_u8() {}
    clip_image_u8(const llama_vision_bitmap & bmp) {
        nx = bmp.nx;
        ny = bmp.ny;
        buf.resize(nx*ny*3);
        memcpy(buf.data(), bmp.data, buf.size());
    }
};

struct clip_image_u8_batch {
    struct clip_image_u8 * data;
    size_t size;
};

static int clip_n_patches_x(const clip_context & ctx) {
    auto & hparams = ctx.model->hparams;
    return hparams.image_size / hparams.patch_size;
}

static int clip_n_patches_y(const clip_context & ctx) {
    return clip_n_patches_x(ctx);
}

static int clip_n_patches(const clip_context & ctx) {
    return clip_n_patches_x(ctx) * clip_n_patches_y(ctx);
}

uint32_t clip_n_mmproj_embd(const clip_vision_model & clip_model) {
    auto & proj_type = clip_model.hparams.proj_type;
    if (proj_type == CLIP_PROJECTOR_TYPE_MLP) {
        return clip_model.mm_2_b->ne[0];
    } else if (proj_type == CLIP_PROJECTOR_TYPE_LDPV2) {
        return clip_model.mm_model_peg_0_b->ne[0];
    } else if (proj_type == CLIP_PROJECTOR_TYPE_MINICPMV_2_5) {
        return 4096;
    } else if (proj_type == CLIP_PROJECTOR_TYPE_MINICPMV_2_6) {
        return 3584;
    } else {
        GGML_ASSERT(false && "invalid proj type");
    }
}

/**
 * Selects the best resolution from a list of possible resolutions based on the original size.
 *
 * @param original_size The original size of the image in the format (width, height).
 * @param possible_resolutions A list of possible resolutions in the format [(width1, height1), (width2, height2), ...].
 * @return The best fit resolution in the format (width, height).
 */
static clip_image_size select_best_resolution(const clip_image_size & original_size, const std::vector<clip_image_size>& possible_resolutions) {
    int original_width  = original_size.width;
    int original_height = original_size.height;

    clip_image_size best_fit;
    int max_effective_resolution = 0;
    int min_wasted_resolution = std::numeric_limits<int>::max();

    for (const auto& resolution : possible_resolutions) {
        int width   = resolution.width;
        int height  = resolution.height;
        float scale = std::min(static_cast<float>(width) / original_width, static_cast<float>(height) / original_height);
        int downscaled_width  = static_cast<int>(original_width * scale);
        int downscaled_height = static_cast<int>(original_height * scale);
        int effective_resolution = std::min(downscaled_width * downscaled_height, original_width * original_height);
        int wasted_resolution = (width * height) - effective_resolution;
        // LOG_DBG("resolution: %d %d, scale: %f, downscaled: %d %d, effective: %d, wasted: %d\n", width, height, scale, downscaled_width, downscaled_height, effective_resolution, wasted_resolution);
        if (effective_resolution > max_effective_resolution || (effective_resolution == max_effective_resolution && wasted_resolution < min_wasted_resolution)) {
            max_effective_resolution = effective_resolution;
            min_wasted_resolution = wasted_resolution;
            best_fit = resolution;
        }
    }

    return best_fit;
}

static bool bicubic_resize(const clip_image_u8 & img, clip_image_u8 & dst, int target_width, int target_height) {
    auto clip = [](int x, int lower, int upper) -> int {
        return std::max(lower, std::min(x, upper));
    };

    const int nx = img.nx;
    const int ny = img.ny;

    dst.nx = target_width;
    dst.ny = target_height;
    dst.buf.resize(3 * target_width * target_height);

    float Cc;
    float C[5];
    float d0, d2, d3, a0, a1, a2, a3;
    int i, j, k, jj;
    int x, y;
    float dx, dy;
    float tx, ty;

    tx = (float)nx / (float)target_width;
    ty = (float)ny / (float)target_height;

    // Bicubic interpolation; adapted from ViT.cpp, inspired from :
    //    -> https://github.com/yglukhov/bicubic-interpolation-image-processing/blob/master/libimage.c#L36
    //    -> https://en.wikipedia.org/wiki/Bicubic_interpolation

    for (i = 0; i < target_height; i++) {
        for (j = 0; j < target_width; j++) {
            x = (int)(tx * j);
            y = (int)(ty * i);

            dx = tx * j - x;
            dy = ty * i - y;

            for (k = 0; k < 3; k++) {
                for (jj = 0; jj <= 3; jj++) {
                    d0 = img.buf[(clip(y - 1 + jj, 0, ny - 1) * nx + clip(x - 1, 0, nx - 1)) * 3 + k] - img.buf[(clip(y - 1 + jj, 0, ny - 1) * nx + clip(x, 0, nx - 1)) * 3 + k];
                    d2 = img.buf[(clip(y - 1 + jj, 0, ny - 1) * nx + clip(x + 1, 0, nx - 1)) * 3 + k] - img.buf[(clip(y - 1 + jj, 0, ny - 1) * nx + clip(x, 0, nx - 1)) * 3 + k];
                    d3 = img.buf[(clip(y - 1 + jj, 0, ny - 1) * nx + clip(x + 2, 0, nx - 1)) * 3 + k] - img.buf[(clip(y - 1 + jj, 0, ny - 1) * nx + clip(x, 0, nx - 1)) * 3 + k];
                    a0 = img.buf[(clip(y - 1 + jj, 0, ny - 1) * nx + clip(x, 0, nx - 1)) * 3 + k];

                    a1 = -1.0 / 3 * d0 + d2 - 1.0 / 6 * d3;
                    a2 =  1.0 / 2 * d0 +      1.0 / 2 * d2;
                    a3 = -1.0 / 6 * d0 -      1.0 / 2 * d2 + 1.0 / 6 * d3;

                    C[jj] = a0 + a1 * dx + a2 * dx * dx + a3 * dx * dx * dx;

                    d0 = C[0] - C[1];
                    d2 = C[2] - C[1];
                    d3 = C[3] - C[1];
                    a0 = C[1];
                    a1 = -1.0 / 3 * d0 + d2 - 1.0 / 6 * d3;
                    a2 =  1.0 / 2 * d0 +      1.0 / 2 * d2;
                    a3 = -1.0 / 6 * d0 -      1.0 / 2 * d2 + 1.0 / 6 * d3;
                    Cc = a0 + a1 * dy + a2 * dy * dy + a3 * dy * dy * dy;

                    const uint8_t Cc2 = std::min(std::max(std::round(Cc), 0.0f), 255.0f);
                    dst.buf[(i * target_width + j) * 3 + k] = float(Cc2);
                }
            }
        }
    }

    return true;
}

static std::vector<clip_image_u8> divide_to_patches_u8(const clip_image_u8 & image, int patch_size) {
    std::vector<clip_image_u8> patches;
    int width = image.nx;
    int height = image.ny;
    for (int i = 0; i < height; i += patch_size) {
        for (int j = 0; j < width; j += patch_size) {
            clip_image_u8 patch;
            patch.nx = std::min(patch_size, width - j);
            patch.ny = std::min(patch_size, height - i);
            patch.buf.resize(3 * patch.nx * patch.ny);
            for (int y = 0; y < patch.ny; ++y) {
                for (int x = 0; x < patch.nx; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        patch.buf[3 * (y * patch.nx + x) + c] = image.buf[3 * ((i + y) * width + (j + x)) + c];
                    }
                }
            }
            patches.push_back(patch);
        }
    }
    return patches;
}

// llava-1.6 type of resize_and_pad (black)
static clip_image_u8 resize_and_pad_image(const clip_image_u8 & image, const clip_image_size & target_resolution) {
    int target_width  = target_resolution.width;
    int target_height = target_resolution.height;

    float scale_w = static_cast<float>(target_width) / image.nx;
    float scale_h = static_cast<float>(target_height) / image.ny;

    int new_width, new_height;

    if (scale_w < scale_h) {
        new_width = target_width;
        new_height = std::min(static_cast<int>(std::ceil(image.ny * scale_w)), target_height);
    } else {
        new_height = target_height;
        new_width = std::min(static_cast<int>(std::ceil(image.nx * scale_h)), target_width);
    }

    clip_image_u8 resized_image;
    // bilinear_resize(image, resized_image, new_width, new_height);
    bicubic_resize(image, resized_image, new_width, new_height);

    clip_image_u8 padded_image;
    padded_image.nx = target_width;
    padded_image.ny = target_height;
    padded_image.buf.resize(3 * target_width * target_height, 0); // Initialize with black

    // Calculate padding offsets
    int pad_x = (target_width - new_width) / 2;
    int pad_y = (target_height - new_height) / 2;

    // Copy the resized image into the center of the padded buffer
    for (int y = 0; y < new_height; ++y) {
        for (int x = 0; x < new_width; ++x) {
            for (int c = 0; c < 3; ++c) {
                padded_image.buf[3 * ((y + pad_y) * target_width + (x + pad_x)) + c] = resized_image.buf[3 * (y * new_width + x) + c];
            }
        }
    }
    return padded_image;
}

static void normalize_image_u8_to_f32(const clip_image_u8 & src, std::vector<float> & dst, const std::array<float, 3> & mean, const std::array<float, 3> & std) {
    dst.resize(src.buf.size());

    for (size_t i = 0; i < src.buf.size(); ++i) {
        int c = i % 3; // rgb
        dst[i] = (static_cast<float>(src.buf[i]) / 255.0f - mean[c]) / std[c];
    }
}

#define LLAMA_LOG_DEBUG LLAMA_LOG_INFO

// minicpmv preprocessor
struct minicpmv_preprocessor {
    int ensure_divide(int length, int patch_size) {
        return std::max(static_cast<int>(std::round(static_cast<float>(length) / patch_size) * patch_size), patch_size);
    }

    std::pair<int, int> uhd_find_best_resize(std::pair<int, int> original_size, int scale_resolution, int patch_size, bool allow_upscale = false) {
        int width = original_size.first;
        int height = original_size.second;
        if ((width * height > scale_resolution * scale_resolution) || allow_upscale) {
            float r = static_cast<float>(width) / height;
            height = static_cast<int>(scale_resolution / std::sqrt(r));
            width = static_cast<int>(height * r);
        }
        int best_width = ensure_divide(width, patch_size);
        int best_height = ensure_divide(height, patch_size);
        return std::make_pair(best_width, best_height);
    }

    std::pair<int, int> uhd_get_refine_size(std::pair<int, int> original_size, std::pair<int, int> grid, int scale_resolution, int patch_size, bool allow_upscale = false) {
        int width, height;
        std::tie(width, height) = original_size;
        int grid_x, grid_y;
        std::tie(grid_x, grid_y) = grid;

        int refine_width = ensure_divide(width, grid_x);
        int refine_height = ensure_divide(height, grid_y);

        int grid_width = refine_width / grid_x;
        int grid_height = refine_height / grid_y;

        // auto best_grid_size = find_best_resize(std::make_tuple(grid_width, grid_height), scale_resolution, patch_size, allow_upscale); (old line)
        auto best_grid_size = uhd_find_best_resize(std::make_pair(grid_width, grid_height), scale_resolution, patch_size, allow_upscale); // (new line) => fixes conversion for make_tuple to make_pair
        int best_grid_width, best_grid_height;
        std::tie(best_grid_width, best_grid_height) = best_grid_size;

        // std::pair<int, int> refine_size = std::make_tuple(best_grid_width * grid_x, best_grid_height * grid_y); (old line)
        std::pair<int, int> refine_size = std::make_pair(best_grid_width * grid_x, best_grid_height * grid_y); // (new line)
        return refine_size;
    }

    std::pair<int, int> uhd_best_grid(const int max_slice_nums, const int multiple, const float log_ratio) {
        std::vector<int> candidate_split_grids_nums;
        for (int i : {multiple - 1, multiple, multiple + 1}) {
            if (i == 1 || i > max_slice_nums) {
                continue;
            }
            candidate_split_grids_nums.push_back(i);
        }

        std::vector<std::pair<int, int>> candidate_grids;
        for (int split_grids_nums : candidate_split_grids_nums) {
            int m = 1;
            while (m <= split_grids_nums) {
                if (split_grids_nums % m == 0) {
                    candidate_grids.emplace_back(m, split_grids_nums / m);
                }
                ++m;
            }
        }

        std::pair<int, int> best_grid{1, 1};
        float min_error = std::numeric_limits<float>::infinity();
        for (const auto& grid : candidate_grids) {
            float error = std::abs(log_ratio - std::log(1.0 * grid.first / grid.second));
            if (error < min_error) {
                best_grid = grid;
                min_error = error;
            }
        }
        return best_grid;
    }

    std::vector<std::vector<clip_image_u8>> uhd_slice_image(
            const clip_image_u8 & img,
            const int max_slice_nums = 9,
            const int scale_resolution = 448,
            const int patch_size = 14) {
        const std::pair<int, int> original_size={img.nx,img.ny};
        const int original_width = img.nx;
        const int original_height = img.ny;
        const float log_ratio = log(1.0*original_width/original_height);
        const float ratio = 1.0 * original_width * original_height/ (scale_resolution * scale_resolution);
        const int multiple = fmin(ceil(ratio), max_slice_nums);

        std::vector<std::vector<clip_image_u8>> images;
        LLAMA_LOG_DEBUG("%s: multiple %d\n", __func__, multiple);
        images.push_back(std::vector<clip_image_u8>());

        if (multiple <= 1) {
            auto best_size = uhd_find_best_resize(original_size, scale_resolution, patch_size, true);
            clip_image_u8 source_image;
            bicubic_resize(img, source_image, best_size.first, best_size.second);
            // source_image = image.resize(best_size, Image.Resampling.BICUBIC)
            images[images.size()-1].push_back(source_image);
        }
        else if (multiple > 1) {
            auto best_size = uhd_find_best_resize(original_size, scale_resolution, patch_size);
            clip_image_u8 source_image;
            bicubic_resize(img, source_image, best_size.first, best_size.second);
            // source_image = image.copy().resize(best_resize, Image.Resampling.BICUBIC)
            LLAMA_LOG_DEBUG("%s: image_size: %d %d; source_image size: %d %d\n", __func__, img.nx, img.ny, best_size.first, best_size.second);
            images[images.size()-1].push_back(source_image);

            std::pair<int, int> best_grid = uhd_best_grid(max_slice_nums, multiple, log_ratio);
            LLAMA_LOG_DEBUG("%s: image_size: %d %d; best_grid: %d %d\n", __func__, img.nx, img.ny, best_grid.first, best_grid.second);

            auto refine_size = uhd_get_refine_size(original_size, best_grid, scale_resolution, patch_size, true);
            clip_image_u8 refine_image;
            bicubic_resize(img, refine_image, refine_size.first, refine_size.second);

            LLAMA_LOG_DEBUG("%s: refine_image_size: %d %d; refine_size: %d %d\n", __func__, refine_image.nx, refine_image.ny, refine_size.first, refine_size.second);

            // split_to_patches
            int width = refine_image.nx;
            int height = refine_image.ny;
            int grid_x = int(width / best_grid.first);
            int grid_y = int(height / best_grid.second);
            for (int patches_i = 0, ic = 0; patches_i < height && ic < best_grid.second; patches_i += grid_y, ic += 1){
                images.push_back(std::vector<clip_image_u8>());
                for(int patches_j = 0, jc = 0; patches_j < width && jc < best_grid.first; patches_j += grid_x, jc += 1){
                    clip_image_u8 patch;
                    patch.nx = grid_x;
                    patch.ny = grid_y;
                    patch.buf.resize(3 * patch.nx * patch.ny);
                    for (int y = patches_i; y < patches_i + grid_y; ++y) {
                        for (int x = patches_j; x < patches_j + grid_x; ++x) {
                            const int i = 3 * (y * refine_image.nx + x);
                            const int j = 3 * ((y-patches_i) * patch.nx + (x-patches_j));
                            patch.buf[j]   = refine_image.buf[i];
                            patch.buf[j+1] = refine_image.buf[i+1];
                            patch.buf[j+2] = refine_image.buf[i+2];
                        }
                    }
                    images[images.size()-1].push_back(patch);
                }
            }
        }
        return images;
    }
};

static llama_vision_patches clip_image_preprocess_minicpmv(const clip_context & ctx, const clip_image_u8 & img) {
    auto & params = ctx.model->hparams;
    GGML_ASSERT(params.arch == VISION_ARCH_MINICPMV);

    static const int max_slice_nums = 9;
    minicpmv_preprocessor preprocessor;
    std::vector<std::vector<clip_image_u8>> imgs = preprocessor.uhd_slice_image(img, max_slice_nums);

    llama_vision_patches output_patches;
    output_patches.n_px = clip_n_patches_x(ctx);
    output_patches.n_py = clip_n_patches_y(ctx);
    output_patches.px = params.patch_size;
    output_patches.py = params.patch_size;

    for (size_t i = 0; i < imgs.size(); ++i) {
        for (size_t j = 0; j < imgs[i].size(); ++j) {
            std::vector<float> res;
            normalize_image_u8_to_f32(imgs[i][j], res, params.image_mean, params.image_std);
            output_patches.buf.push_back(res);
        }
    }
}

// returns the normalized float tensor for llava-1.5, for spatial_unpad with anyres processing for llava-1.6 it returns the normalized image patch tensors as a vector
// res_imgs memory is being allocated here, previous allocations will be freed if found
static llama_vision_patches clip_image_preprocess(const clip_context & ctx, const clip_image_u8 & img) {
    bool pad_to_square = true;
    auto & params = ctx.model->hparams;
    // The model config actually contains all we need to decide on how to preprocess, here we automatically switch to the new llava-1.6 preprocessing
    if (params.mm_patch_merge_type == MM_PATCH_MERGE_SPATIAL_UNPAD) {
        pad_to_square = false;
    }

    llama_vision_patches output_patches;
    output_patches.n_px = clip_n_patches_x(ctx);
    output_patches.n_py = clip_n_patches_y(ctx);
    output_patches.px = params.patch_size;
    output_patches.py = params.patch_size;

    // the logic below is to pad the shorter side to the longer side with a background color: rgb(122, 116, 104)
    // see https://github.com/haotian-liu/LLaVA/blob/e854a2bf85118c504f6f16bf5c3c7c92f8fa8c6b/llava/conversation.py#L113-L156

    clip_image_u8 temp;
    if (pad_to_square && img.nx != img.ny) {
        // if the image is not square, pad it to a square
        int longer_side = std::max(img.nx, img.ny);
        temp.nx = longer_side;
        temp.ny = longer_side;
        temp.buf.resize(3 * longer_side * longer_side);
        const uint8_t bc[3] = {122, 116, 104}; // background color in RGB from LLaVA (this is the mean rgb color * 255)

        // fill with background color
        for (size_t i = 0; i < temp.buf.size(); i++) {
            temp.buf[i] = bc[i % 3];
        }

        // copy from the input image
        for (int y = 0; y < img.ny; y++) {
            for (int x = 0; x < img.nx; x++) {
                const int i = 3 * (y * img.nx + x);
                const int j = 3 * (y * temp.nx + x);
                temp.buf[j]   = img.buf[i];
                temp.buf[j+1] = img.buf[i+1];
                temp.buf[j+2] = img.buf[i+2];
            }
        }
    } else if (params.image_grid_pinpoints[0] != 0) {
        // "spatial_unpad" with "anyres" processing for llava-1.6
        std::vector<clip_image_size> possible_resolutions;
        for (int i = 0; i < 32 && params.image_grid_pinpoints[i] != 0; i += 2) {
            clip_image_size s;
            s.width  = params.image_grid_pinpoints[i];
            s.height = params.image_grid_pinpoints[i+1];
            possible_resolutions.push_back(s);
        }
        clip_image_size best_resolution = select_best_resolution({img.nx, img.ny}, possible_resolutions);
        // clip_image_save_to_bmp(*img, "input.bmp");
        temp = resize_and_pad_image(img, best_resolution);  // we do not pad with mean-bg color anymore in llava-1.6
        // clip_image_save_to_bmp(*temp, "resized.bmp");

        std::vector<clip_image_u8> patches = divide_to_patches_u8(temp, params.image_size); // prepare spatial sorted main patches of image_size each (336 in llava-1.6)

        clip_image_u8 image_original_resize;
        // bilinear_resize(*img, *image_original_resize, params.image_size, params.image_size); // in python this is "shortest_edge", but all CLIP are square
        bicubic_resize(img, image_original_resize, params.image_size, params.image_size); // in python this is "shortest_edge", but all CLIP are square
        patches.insert(patches.begin(), image_original_resize);
        // clip_image_f32_batch_init(patches.size());
        output_patches.buf.resize(patches.size());
        int num = 0;
        for (auto & patch : patches) {
            normalize_image_u8_to_f32(patch, output_patches.buf[num], params.image_mean, params.image_std);
            num++;
        }
        return output_patches;
    } else {
        temp.nx = img.nx;
        temp.ny = img.ny;
        temp.buf.resize(img.buf.size());
        memcpy(temp.buf.data(), img.buf.data(), temp.buf.size());
    }

    const int nx = temp.nx;
    const int ny = temp.ny;
    // bmp_export(temp, "resized_vanilla.bmp");

    const int nx2 = params.image_size;
    const int ny2 = params.image_size;
    std::vector<float> res;
    res.resize(3 * nx2 * ny2);

    const float scale = std::max(nx, ny) / (float)params.image_size;

    const int nx3 = int(nx / scale + 0.5f);
    const int ny3 = int(ny / scale + 0.5f);

    const auto & m3 = params.image_mean; // {0.48145466f, 0.4578275f, 0.40821073f};
    const auto & s3 = params.image_std;  // {0.26862954f, 0.26130258f, 0.27577711f};

    for (int y = 0; y < ny3; y++) {
        for (int x = 0; x < nx3; x++) {
            for (int c = 0; c < 3; c++) {
                // linear interpolation
                const float sx = (x + 0.5f) * scale - 0.5f;
                const float sy = (y + 0.5f) * scale - 0.5f;

                const int x0 = std::max(0, (int)std::floor(sx));
                const int y0 = std::max(0, (int)std::floor(sy));

                const int x1 = std::min(x0 + 1, nx - 1);
                const int y1 = std::min(y0 + 1, ny - 1);

                const float dx = sx - x0;
                const float dy = sy - y0;

                const int j00 = 3 * (y0 * nx + x0) + c;
                const int j01 = 3 * (y0 * nx + x1) + c;
                const int j10 = 3 * (y1 * nx + x0) + c;
                const int j11 = 3 * (y1 * nx + x1) + c;

                const float v00 = temp.buf[j00];
                const float v01 = temp.buf[j01];
                const float v10 = temp.buf[j10];
                const float v11 = temp.buf[j11];

                const float v0 = v00 * (1.0f - dx) + v01 * dx;
                const float v1 = v10 * (1.0f - dx) + v11 * dx;

                const float v = v0 * (1.0f - dy) + v1 * dy;

                const uint8_t v2 = std::min(std::max(std::round(v), 0.0f), 255.0f);

                const int i = 3 * (y * nx3 + x) + c;

                res[i] = ((float(v2) / 255.0f) - m3[c]) / s3[c];
            }
        }
    }

    output_patches.buf.resize(1);
    output_patches.buf[0] = std::move(res);

    return output_patches;
}

static ggml_cgraph * clip_image_build_graph(clip_context & ctx, int batch_size, clip_image_size & image_size) {
    auto & model = *ctx.model;
    auto & hparams = ctx.model->hparams;

    const int hidden_size   = hparams.hidden_size;
    const int n_head        = hparams.n_head;
    const int d_head        = hidden_size / n_head;
    const int patch_size    = hparams.patch_size;
    const float eps         = hparams.eps;
    const int num_patches   = ((image_size.width / patch_size) * (image_size.height / patch_size));
    const int num_positions = num_patches + (model.class_embedding ? 1 : 0);

    LLAMA_LOG_DEBUG("%s: num_patches = %d\n", __func__, num_patches);

    struct ggml_init_params params = {
        /*.mem_size   =*/ ctx.buf_compute_meta.size(),
        /*.mem_buffer =*/ ctx.buf_compute_meta.data(),
        /*.no_alloc   =*/ true,
    };

    struct ggml_context * ctx0 = ggml_init(params);
    struct ggml_cgraph * gf = ggml_new_graph(ctx0);

    // input
    struct ggml_tensor * embeddings;
    {
        struct ggml_tensor * inp_raw = ggml_new_tensor_4d(ctx0, GGML_TYPE_F32, image_size.width, image_size.height, 3, batch_size);
        ggml_set_name(inp_raw, "inp_raw");
        ggml_set_input(inp_raw);

        struct ggml_tensor * inp = ggml_conv_2d(ctx0, model.patch_embeddings, inp_raw, patch_size, patch_size, 0, 0, 1, 1);

        inp = ggml_reshape_3d(ctx0, inp, num_patches, hidden_size, batch_size);
        inp = ggml_cont(ctx0, ggml_permute(ctx0, inp, 1, 0, 2, 3));

        if (model.patch_bias) {
            inp = ggml_add(ctx0, inp, model.patch_bias);
        }
        // auto * ne = inp->ne; printf("%d %d %d %d\n", ne[0], ne[1], ne[2], ne[3]);

        embeddings = inp;
        if (model.class_embedding) {
            embeddings = ggml_new_tensor_3d(ctx0, GGML_TYPE_F32, hidden_size, num_positions, batch_size);
            ggml_set_name(embeddings, "embeddings");
            ggml_set_input(embeddings);
            embeddings = ggml_acc(ctx0, embeddings, model.class_embedding,
                    embeddings->nb[1], embeddings->nb[2], embeddings->nb[3], 0);
            embeddings = ggml_acc(ctx0, embeddings, inp,
                    embeddings->nb[1], embeddings->nb[2], embeddings->nb[3], model.class_embedding->nb[1]);
        }

        struct ggml_tensor * positions = ggml_new_tensor_1d(ctx0, GGML_TYPE_I32, num_positions);
        ggml_set_name(positions, "positions");
        ggml_set_input(positions);

        embeddings = ggml_add(ctx0,
            embeddings,
            ggml_get_rows(ctx0, model.position_embeddings, positions));
    }

    // pre-layernorm
    if (model.pre_norm_w) {
        embeddings = ggml_norm(ctx0, embeddings, eps);
        ggml_set_name(embeddings, "pre_ln");

        embeddings = ggml_add(ctx0, ggml_mul(ctx0, embeddings, model.pre_norm_w), model.pre_norm_b);
    }

    // loop over layers
    for (int il = 0; il < (int)hparams.n_layer + hparams.select_layer; il++) {
        struct ggml_tensor * cur = embeddings;

        // layernorm1
        {
            cur = ggml_norm(ctx0, cur, eps);
            cur = ggml_add(ctx0,
                ggml_mul(ctx0, cur, model.layers[il].norm_in_w),
                model.layers[il].norm_in_b);
        }

        // self-attention
        {

            struct ggml_tensor * Q = ggml_add(ctx0,
                ggml_mul_mat(ctx0, model.layers[il].q_w, cur),
                model.layers[il].q_b);

            Q = ggml_scale_inplace(ctx0, Q, 1.0f / sqrt((float)d_head));
            Q = ggml_reshape_4d(ctx0, Q, d_head, n_head, num_positions, batch_size);
            Q = ggml_cont(ctx0, ggml_permute(ctx0, Q, 0, 2, 1, 3));
            Q = ggml_reshape_3d(ctx0, Q, d_head, num_positions, n_head * batch_size);

            struct ggml_tensor * K = ggml_add(ctx0,
                ggml_mul_mat(ctx0, model.layers[il].k_w, cur),
                model.layers[il].k_b);

            K = ggml_reshape_4d(ctx0, K, d_head, n_head, num_positions, batch_size);
            K = ggml_cont(ctx0, ggml_permute(ctx0, K, 0, 2, 1, 3));
            K = ggml_reshape_3d(ctx0, K, d_head, num_positions, n_head * batch_size);

            struct ggml_tensor * V = ggml_add(ctx0,
                ggml_mul_mat(ctx0, model.layers[il].v_w, cur),
                model.layers[il].v_b);

            V = ggml_reshape_4d(ctx0, V, d_head, n_head, num_positions, batch_size);
            V = ggml_cont(ctx0, ggml_permute(ctx0, V, 1, 2, 0, 3));
            V = ggml_reshape_3d(ctx0, V, num_positions, d_head, n_head * batch_size);

            struct ggml_tensor * KQ = ggml_mul_mat(ctx0, K, Q);
            KQ = ggml_soft_max_inplace(ctx0, KQ);
            struct ggml_tensor * KQV = ggml_mul_mat(ctx0, V, KQ);
            KQV = ggml_reshape_4d(ctx0, KQV, d_head, num_positions, n_head, batch_size);
            KQV = ggml_permute(ctx0, KQV, 0, 2, 1, 3);

            cur = ggml_cont_3d(ctx0, KQV, hidden_size, num_positions, batch_size);
        }

        // attention output
        cur = ggml_add(ctx0, ggml_mul_mat(ctx0, model.layers[il].output_w, cur), model.layers[il].output_b);

        // re-add the layer input, e.g., residual
        cur = ggml_add(ctx0, cur, embeddings);

        embeddings = cur; // embeddings = residual, cur = hidden_states

        // layernorm2
        {
            cur = ggml_norm(ctx0, cur, eps);
            cur = ggml_add(ctx0,
                ggml_mul(ctx0, cur, model.layers[il].norm_out_w),
                model.layers[il].norm_out_b);
        }

        cur = ggml_mul_mat(ctx0, model.layers[il].ffn_up_w, cur);
        cur = ggml_add(ctx0, cur, model.layers[il].ffn_up_b);

        if (hparams.use_gelu) {
            cur = ggml_gelu_inplace(ctx0, cur);
        } else {
            cur = ggml_gelu_quick_inplace(ctx0, cur);
        }

        cur = ggml_mul_mat(ctx0, model.layers[il].ffn_down_w, cur);
        cur = ggml_add(ctx0, cur, model.layers[il].ffn_down_b);

        // residual 2
        cur = ggml_add(ctx0, embeddings, cur);

        embeddings = cur;
    }

    // post-layernorm
    if (model.post_norm_w) {
        embeddings = ggml_norm(ctx0, embeddings, eps);
        ggml_set_name(embeddings, "post_ln");

        embeddings = ggml_add(ctx0, ggml_mul(ctx0, embeddings, model.post_norm_w), model.post_norm_b);
    }

    // llava projector
    {
        embeddings = ggml_reshape_2d(ctx0, embeddings, embeddings->ne[0], embeddings->ne[1]);

        struct ggml_tensor * patches = ggml_new_tensor_1d(ctx0, GGML_TYPE_I32, num_patches);
        ggml_set_name(patches, "patches");
        ggml_set_input(patches);

        // shape [1, 576, 1024]
        // ne is whcn, ne = [1024, 576, 1, 1]
        embeddings = ggml_get_rows(ctx0, embeddings, patches);

        if (hparams.proj_type == CLIP_PROJECTOR_TYPE_MLP) {
            embeddings = ggml_mul_mat(ctx0, model.mm_1_w, embeddings);
            embeddings = ggml_add(ctx0, embeddings, model.mm_1_b);

            embeddings = ggml_gelu(ctx0, embeddings);
            embeddings = ggml_mul_mat(ctx0, model.mm_2_w, embeddings);
            embeddings = ggml_add(ctx0, embeddings, model.mm_2_b);

        } else if (hparams.proj_type == CLIP_PROJECTOR_TYPE_LDPV2) {
            int n_patch = 24;
            struct ggml_tensor * mlp_0 = ggml_mul_mat(ctx0, model.mm_model_mlp_0_w, embeddings);
            mlp_0 = ggml_add(ctx0, mlp_0, model.mm_model_mlp_0_b);
            mlp_0 = ggml_gelu(ctx0, mlp_0);
            struct ggml_tensor * mlp_2 = ggml_mul_mat(ctx0, model.mm_model_mlp_2_w, mlp_0);
            mlp_2 = ggml_add(ctx0, mlp_2, model.mm_model_mlp_2_b);
            // mlp_2 ne = [2048, 576, 1, 1]
            // // AVG Pool Layer 2*2, strides = 2
            mlp_2 = ggml_cont(ctx0, ggml_permute(ctx0, mlp_2, 1, 0, 2, 3));
            // mlp_2 ne = [576, 2048, 1, 1]
            mlp_2 = ggml_reshape_4d(ctx0, mlp_2, n_patch, n_patch, mlp_2->ne[1], mlp_2->ne[2]);
            // mlp_2 ne [24, 24, 2048, 1]
            mlp_2 = ggml_pool_2d(ctx0, mlp_2, GGML_OP_POOL_AVG, 2, 2, 2, 2, 0, 0);
            // weight ne = [3, 3, 2048, 1]
            struct ggml_tensor * peg_0 = ggml_conv_2d_dw(ctx0, model.mm_model_peg_0_w, mlp_2, 1, 1, 1, 1, 1, 1);
            peg_0 = ggml_cont(ctx0, ggml_permute(ctx0, peg_0, 1, 2, 0, 3));
            peg_0 = ggml_add(ctx0, peg_0, model.mm_model_peg_0_b);
            mlp_2 = ggml_cont(ctx0, ggml_permute(ctx0, mlp_2, 1, 2, 0, 3));
            peg_0 = ggml_add(ctx0, peg_0, mlp_2);
            peg_0 = ggml_reshape_3d(ctx0, peg_0, peg_0->ne[0], peg_0->ne[1] * peg_0->ne[2], peg_0->ne[3]);
            embeddings = peg_0;

        } else {
            GGML_ASSERT(false && "unsupported proj type");
        }
    }

    embeddings = ggml_cont(ctx0, embeddings);

    // build the graph
    ggml_build_forward_expand(gf, embeddings);
    ggml_free(ctx0);
    return gf;
}

static int32_t clip_image_encode(clip_context & ctx, const llama_vision_patches & patches) {
    int batch_size = patches.buf.size();
    auto & model = *ctx.model;
    auto & hparams = ctx.model->hparams;

    if (hparams.arch == VISION_ARCH_LLAVA) {
        GGML_ASSERT(batch_size == 1); // TODO: support multiple images
    }

    clip_image_size image_size{(int)hparams.image_size, (int)hparams.image_size};
    const int patch_size    = hparams.patch_size;
    const int num_patches   = ((image_size.width / patch_size) * (image_size.height / patch_size));
    const int num_positions = num_patches + (model.class_embedding ? 1 : 0);

    LLAMA_LOG_DEBUG("%s: image_size = %d\n", __func__, hparams.image_size);
    LLAMA_LOG_DEBUG("%s: num_positions = %d\n", __func__, num_positions);

    // build the inference graph
    ggml_cgraph * gf = clip_image_build_graph(ctx, batch_size, image_size);

    // alloc memory for graph
    bool ok = ggml_backend_sched_alloc_graph(ctx.sched, gf);
    if (!ok) {
        LLAMA_LOG_ERROR("failed to alloc memory for graph\n");
        return -1;
    }

    // set raw input
    {
        struct ggml_tensor * inp_raw = ggml_graph_get_tensor(gf, "inp_raw");
        float * data = (float *)malloc(ggml_nbytes(inp_raw));

        for (int i = 0; i < batch_size; i++) {
            const int nx = patches.px * patches.n_px;
            const int ny = patches.py * patches.n_py;
            const int n = nx * ny;

            for (int b = 0; b < batch_size; b++) {
                for (int k = 0; k < 3; k++) {
                    for (int y = 0; y < ny; y++) {
                        for (int x = 0; x < nx; x++) {
                            data[(b * 3 * n) + k * n + y * nx + x] = patches.buf[b][3 * (y * nx + x) + k];
                        }
                    }
                }
            }
        }
        ggml_backend_tensor_set(inp_raw, data, 0, ggml_nbytes(inp_raw));
        free(data);
    }

    if (model.class_embedding) {
        struct ggml_tensor * embeddings = ggml_graph_get_tensor(gf, "embeddings");

        void* zero_mem = malloc(ggml_nbytes(embeddings));
        memset(zero_mem, 0, ggml_nbytes(embeddings));
        ggml_backend_tensor_set(embeddings, zero_mem, 0, ggml_nbytes(embeddings));
        free(zero_mem);
    }

    {
        struct ggml_tensor * positions = ggml_graph_get_tensor(gf, "positions");

        int* positions_data = (int*)malloc(ggml_nbytes(positions));
        for (int i = 0; i < num_positions; i++) {
            positions_data[i] = i;
        }
        ggml_backend_tensor_set(positions, positions_data, 0, ggml_nbytes(positions));
        free(positions_data);
    }

    {
        struct ggml_tensor * patches = ggml_graph_get_tensor(gf, "patches");
        int* patches_data = (int*)malloc(ggml_nbytes(patches));
        for (int i = 0; i < num_patches; i++) {
            patches_data[i] = i + 1;
        }
        ggml_backend_tensor_set(patches, patches_data, 0, ggml_nbytes(patches));
        free(patches_data);
    }

    // compute
    ggml_backend_sched_graph_compute(ctx.sched, gf);

    // the last node is the embedding tensor
    struct ggml_tensor * output_node = ggml_graph_node(gf, -1);
    //LLAMA_LOG_INFO("%s: output tensor shape = %lld %lld %lld %lld\n", __func__, output->ne[0], output->ne[1], output->ne[2], output->ne[3]);

    // copy output node to context
    if (ctx.ctx_ggml) {
        ggml_free(ctx.ctx_ggml);
    }
    ggml_init_params params = {
        /*.mem_size   =*/ ggml_tensor_overhead(),
        /*.mem_buffer =*/ NULL,
        /*.no_alloc   =*/ true,
    };
    ctx.ctx_ggml = ggml_init(params);
    ctx.output = ggml_dup_tensor(ctx.ctx_ggml, output_node);
    ggml_backend_alloc_ctx_tensors_from_buft(ctx.ctx_ggml, ctx.model->buft);
    ggml_backend_tensor_copy(output_node, ctx.output);

    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////
// public API

struct llama_vision_bitmap * llama_vision_bitmap_init(uint32_t nx, uint32_t ny) {
    llama_vision_bitmap * bmp = new llama_vision_bitmap;
    bmp->nx = nx;
    bmp->ny = ny;
    bmp->data = (unsigned char *)malloc(3 * nx * ny);
    return bmp;
}

void llama_vision_bitmap_free(llama_vision_bitmap * bmp) {
    free(bmp->data);
    delete bmp;
}

struct llama_vision_patches * llama_vision_patches_init(
        struct llama_context * ctx,
        llama_vision_bitmap * bmp) {
    clip_context & vctx = ctx->vctx;
    if (vctx.model->hparams.arch == VISION_ARCH_MINICPMV) {
        return new llama_vision_patches(clip_image_preprocess_minicpmv(vctx, *bmp));
    }
    return new llama_vision_patches(clip_image_preprocess(vctx, *bmp));
}

void llama_vision_patches_free(llama_vision_patches * p) {
    delete p;
}

int32_t llama_vision_encode(struct llama_context * ctx, llama_vision_patches * p) {
    if (p->buf.empty()) {
        LLAMA_LOG_ERROR("%s: nothing to encode\n", __func__);
        return -1;
    }

    clip_context & vctx = ctx->vctx;
    auto & hparams = vctx.model->hparams;
    switch (hparams.mm_patch_merge_type) {
        case MM_PATCH_MERGE_FLAT:
            {
                // flat / default llava-1.5 type embedding
                // n_output = clip_n_patches(ctx);
                int32_t encoded = clip_image_encode(vctx, *p);
                if (encoded != 0) {
                    LLAMA_LOG_ERROR("Unable to encode image\n");
                    return encoded;
                }
            } break;
        case MM_PATCH_MERGE_SPATIAL_UNPAD:
            {
                // TODO: support llava-1.6
                (void)0;
            } break;
        default:
            GGML_ASSERT(false && "unsupported mm_patch_merge_type");
    }

    return 0;
}

struct ggml_tensor * llama_vision_get_output_tensor(llama_context * ctx) {
    return ctx->vctx.output;
}

////////////////////////////////////////////////////////////////////////////////////////
// for debugging
#ifndef NDEBUG

static int bmp_export(const struct clip_image_u8 &img, const std::string &location) {
    const uint32_t width = img.nx;
    const uint32_t height = img.ny;
    // swap red and blue channel
    std::vector<uint8_t> buffer(width*height*3);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            size_t base = x*3 + y*3*width;
            buffer[base+2] = img.buf[base];
            buffer[base+1] = img.buf[base+1];
            buffer[base]   = img.buf[base+2];
        }
    }
    const bool hasAlphaChannel = false;

    std::ofstream fout(location, std::ios::out | std::ios::binary);

    if (fout.fail()) {
        return 0;
    }

    //Padding
    const uint8_t padding = hasAlphaChannel ? 0 : (4 - (width * 3) % 4) % 4;

    //Bitmap file header.
    const char signature[2] = { 'B', 'M' };
    const uint32_t fileSize = buffer.size() * sizeof(uint8_t) + padding * (height - 1) + 14 + 124;
    const uint32_t offset = 14 + 124;

    //Bitmap information header file
    const uint32_t DIBSize = 124;
    const int32_t bitmapWidth = width;
    const int32_t bitmapHeight = height;
    const uint16_t numPlanes = 1;
    const uint16_t bitsPerPixel = (hasAlphaChannel) ? 32 : 24;
    const uint32_t compressionMethod = (hasAlphaChannel) ? 3 : 0; //BI_RGB = 0, BI_BITFIELDS = 3
    const uint32_t bitmapSize = buffer.size() * sizeof(uint8_t);
    const int32_t horizontalResolution = 2834;
    const int32_t verticalResolution = 2834;
    const uint32_t numColors = 0;
    const uint32_t impColorCount = 0;
    const uint32_t redBitmask = (hasAlphaChannel) ? 0x0000FF00 : 0; //ARGB32 pixel format
    const uint32_t greenBitmask = (hasAlphaChannel) ? 0x00FF0000 : 0;
    const uint32_t blueBitmask = (hasAlphaChannel) ? 0xFF000000 : 0;
    const uint32_t alphaBitmask = (hasAlphaChannel) ? 0x000000FF : 0;

    //Writing the file header and information header to the file
    std::vector<uint8_t> header(offset, 0);
    header[0] = signature[0];
    header[1] = signature[1];

#define BMP_HEADERS(i, variableName)    header[i] = variableName; header[i+1] = variableName >> 8; header[i+2] = variableName >> 16; header[i+3] = variableName >> 24;

    BMP_HEADERS(2, fileSize);
    BMP_HEADERS(6, 0);
    BMP_HEADERS(10, offset);
    BMP_HEADERS(14, DIBSize);
    BMP_HEADERS(18, bitmapWidth);
    BMP_HEADERS(22, bitmapHeight);

    header[26] = (uint8_t)numPlanes;
    header[27] = (uint8_t)(numPlanes >> 8);
    header[28] = (uint8_t)bitsPerPixel;
    header[29] = (uint8_t)(bitsPerPixel >> 8);

    BMP_HEADERS(30, compressionMethod);
    BMP_HEADERS(34, (unsigned char)bitmapSize);
    BMP_HEADERS(38, (unsigned char)horizontalResolution);
    BMP_HEADERS(42, (unsigned char)verticalResolution);
    BMP_HEADERS(46, (unsigned char)numColors);
    BMP_HEADERS(50, (unsigned char)impColorCount);
    BMP_HEADERS(54, (unsigned char)redBitmask);
    BMP_HEADERS(58, (unsigned char)greenBitmask);
    BMP_HEADERS(62, (unsigned char)blueBitmask);
    BMP_HEADERS(66, alphaBitmask);

#undef BMP_HEADERS

    fout.write((char *)header.data(), sizeof(uint8_t) * header.size());

    //Writing the pixel array
    const uint32_t bWidth = bitsPerPixel / 8 * width;

    for (int i = height - 1; i >= 0; i--) {
        std::vector<uint8_t> row(buffer.begin() + i * bWidth, buffer.begin() + i * bWidth + bWidth);
        fout.write((char *)row.data(), row.size() * sizeof(uint8_t));
        fout.seekp(padding * sizeof(uint8_t), std::ios::cur);
    }

    fout.close();
    return 1;
}

#endif

