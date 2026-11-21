/**
 ******************************************************************************
 * @file    GlanceCache.hpp
 * @brief   Kernel-owned copies of glance data that outlive the glance app
 ******************************************************************************
 */

#ifndef APP_GLANCE_CACHE_HPP
#define APP_GLANCE_CACHE_HPP

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

// Relative to this file: the simulator's include paths do not reach Shared/.
#include "../Shared/AppTypes.hpp"

namespace App {

/**
 * @brief   Copies of the pixels a glance's image controls show.
 *
 * @details
 * A glance app's icons are arrays in its own memory, and the controls it
 * sends only point at them. Every stock glance exits when the Glances screen
 * closes, after which those pointers lead to whatever was loaded there next.
 * The GUI copies the pixels when it draws, so that was only ever a risk for a
 * list that outlived the app; keeping the last values for the next visit makes
 * every list do that. So the kernel copies the pixels when the app sends them,
 * on the app thread, the one moment the app is certain to be loaded (it is
 * waiting for the send to complete, and only that same thread unloads it).
 *
 * Immutable once built: lists, the GUI and the cache share one by shared_ptr,
 * and an update that changes any image builds a new one rather than touching
 * this one.
 */
struct GlancePixels {
    struct Image {
        uint32_t ctrlId = 0;
        uint16_t w = 0;
        uint16_t h = 0;
        std::unique_ptr<uint8_t[]> bytes;
    };

    std::vector<Image> images;

    const Image *find(uint32_t ctrlId) const
    {
        for (const Image &img : images) {
            if (img.ctrlId == ctrlId) {
                return &img;
            }
        }
        return nullptr;
    }
};

/// The largest image a glance can hold: its whole 240 x 60 area, one byte a
/// pixel (ABGR2222). Anything claiming more is not an image this screen can
/// show, and copying it would read past whatever the app pointed at.
constexpr uint32_t kGlanceImageMaxBytes = 240U * 60U;

/**
 * @brief   Take copies of every image @p data points at, and point it at them.
 * @details Reuses @p previous when every image is byte for byte what it holds,
 *          which for a glance whose icon does not change is every update after
 *          the first. An image with no pixels, or larger than the glance's
 *          area, or that cannot be allocated, is dropped from @p data rather
 *          than left pointing into the app.
 *          Reads the memory the image controls point at, so it must run while
 *          the app that sent them is loaded.
 * @param   data: a glance as the app sent it; on return, self-contained.
 * @param   previous: the pixels of this app's last update, or null.
 */
inline void CopyGlanceImages(GlanceData &data, const std::shared_ptr<const GlancePixels> &previous)
{
    // Drop what cannot be copied, keeping the order of the rest.
    uint8_t kept = 0;
    uint32_t images = 0;
    for (uint8_t i = 0; i < data.count; ++i) {
        const GlanceControl_t &c = data.ctrls[i];
        if (c.type == GLANCE_TYPE_IMAGE) {
            const uint32_t bytes = static_cast<uint32_t>(c.payload.image.size.w) *
                                   c.payload.image.size.h;
            if (c.payload.image.buff == nullptr || bytes == 0 || bytes > kGlanceImageMaxBytes) {
                continue;
            }
            ++images;
        }
        if (kept != i) {
            data.ctrls[kept] = c;
        }
        ++kept;
    }
    data.count = kept;
    data.pixels.reset();

    if (images == 0) {
        return;
    }

    // The same images as last time, byte for byte: share those copies.
    bool same = previous != nullptr && previous->images.size() == images;
    for (uint8_t i = 0; same && i < data.count; ++i) {
        const GlanceControl_t &c = data.ctrls[i];
        if (c.type != GLANCE_TYPE_IMAGE) {
            continue;
        }
        const GlancePixels::Image *img = previous->find(c.id);
        same = img != nullptr && img->w == c.payload.image.size.w &&
               img->h == c.payload.image.size.h &&
               std::memcmp(img->bytes.get(), c.payload.image.buff,
                           static_cast<size_t>(img->w) * img->h) == 0;
    }

    std::shared_ptr<const GlancePixels> pixels = previous;
    if (!same) {
        auto fresh = std::make_shared<GlancePixels>();
        fresh->images.reserve(images);
        for (uint8_t i = 0; i < data.count; ++i) {
            const GlanceControl_t &c = data.ctrls[i];
            if (c.type != GLANCE_TYPE_IMAGE) {
                continue;
            }
            const size_t bytes = static_cast<size_t>(c.payload.image.size.w) * c.payload.image.size.h;
            GlancePixels::Image img;
            img.ctrlId = c.id;
            img.w = c.payload.image.size.w;
            img.h = c.payload.image.size.h;
            img.bytes.reset(new (std::nothrow) uint8_t[bytes]);
            if (img.bytes) {
                std::memcpy(img.bytes.get(), c.payload.image.buff, bytes);
                fresh->images.push_back(std::move(img));
            }
        }
        pixels = std::move(fresh);
    }

    // Point every image at its copy; one whose copy could not be made goes.
    kept = 0;
    for (uint8_t i = 0; i < data.count; ++i) {
        GlanceControl_t c = data.ctrls[i];
        if (c.type == GLANCE_TYPE_IMAGE) {
            const GlancePixels::Image *img = pixels->find(c.id);
            if (img == nullptr) {
                continue;
            }
            c.payload.image.buff = img->bytes.get();
        }
        data.ctrls[kept++] = c;
    }
    data.count = kept;
    data.pixels = std::move(pixels);
}

/**
 * @brief   The last values a glance showed, kept for its next visit.
 * @details Only the controls in use, not the 32 a GlanceData has room for: a
 *          stock glance uses three, so this is about 160 bytes where a whole
 *          GlanceData is 1.7 KB, and there is one per installed glance. The
 *          name and PID are the app's own, so they are not kept twice.
 */
class CachedGlance {
public:
    bool valid() const { return mValid; }

    void store(const GlanceData &data)
    {
        mCtrls.assign(data.ctrls, data.ctrls + data.count);
        mPixels = data.pixels;
        mValid = true;
    }

    /// The cached glance as a list entry, under @p name and @p pid.
    GlanceData expand(const char *name, uint32_t pid) const
    {
        GlanceData d {};
        d.count = static_cast<uint8_t>(mCtrls.size());
        for (size_t i = 0; i < mCtrls.size(); ++i) {
            d.ctrls[i] = mCtrls[i];
        }
        std::snprintf(d.altname, sizeof(d.altname), "%s", name);
        d.pid = pid;
        d.pixels = mPixels;
        return d;
    }

private:
    bool mValid = false;
    std::vector<GlanceControl_t> mCtrls;
    std::shared_ptr<const GlancePixels> mPixels;
};

} // namespace App

#endif // APP_GLANCE_CACHE_HPP
