/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <terminal_renderer/ImageRenderer.h>

#include <crispy/algorithm.h>
#include <crispy/times.h>

#include <array>

using crispy::times;

using std::array;
using std::nullopt;
using std::optional;

namespace terminal::renderer
{

ImageRenderer::ImageRenderer(GridMetrics const& gridMetrics, ImageSize cellSize):
    Renderable { gridMetrics }, cellSize_ { cellSize }
{
}

void ImageRenderer::setRenderTarget(RenderTarget& renderTarget,
                                    DirectMappingAllocator& directMappingAllocator)
{
    Renderable::setRenderTarget(renderTarget, directMappingAllocator);
    clearCache();
}

void ImageRenderer::setCellSize(ImageSize _cellSize)
{
    cellSize_ = _cellSize;
    // TODO: recompute rasterized images slices here?
}

void ImageRenderer::renderImage(crispy::Point _pos, ImageFragment const& fragment)
{
    // std::cout << fmt::format("ImageRenderer.renderImage: {}\n", fragment);

    AtlasTileAttributes const* tileAttributes = getOrCreateCachedTileAttributes(fragment);
    if (!tileAttributes)
        return;

    // clang-format off
    renderTile(atlas::RenderTile::X { _pos.x },
               atlas::RenderTile::Y { _pos.y },
               RGBAColor::White,
               *tileAttributes);
    // clang-format on
}

Renderable::AtlasTileAttributes const* ImageRenderer::getOrCreateCachedTileAttributes(
    ImageFragment const& fragment)
{
    auto const key = ImageFragmentKey { fragment.rasterizedImage().image().id(),
                                        fragment.offset(),
                                        fragment.rasterizedImage().cellSize() };
    auto const hash = crispy::StrongHash::compute(key);

    return textureAtlas().get_or_try_emplace(
        hash,
        [&](atlas::TileLocation /*targetLocation*/, uint32_t /*entryIndex*/)
            -> optional<TextureAtlas::TileCreateData> { return createTileData(fragment); });
}

auto ImageRenderer::createTileData(ImageFragment const& fragment) -> optional<TextureAtlas::TileCreateData>
{
    auto constexpr colored = true;

    auto tileData = TextureAtlas::TileCreateData {};
    tileData.bitmap = fragment.data();
    tileData.bitmapSize = cellSize_;
    tileData.bitmapFormat = atlas::Format::RGBA; // fragment.rasterizedImage().image().format();
    tileData.metadata.x = {};
    tileData.metadata.y = {};

    return { tileData };
}

void ImageRenderer::discardImage(ImageId _imageId)
{
    // We currently don't really discard.
    // Because the GPU texture atlas is resource-guarded by an LRU hashtable.
}

void ImageRenderer::clearCache()
{
    // We currently don't really clean up anything.
    // Because the GPU texture atlas is resource-guarded by an LRU hashtable.
}

void ImageRenderer::inspect(std::ostream& /*output*/) const
{
}

} // namespace terminal::renderer
