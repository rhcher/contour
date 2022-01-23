/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2021 Christian Parpart <christian@parpart.family>
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
#pragma once

#include <terminal/Color.h>
#include <terminal/primitives.h> // ImageSize

#include <crispy/StrongHash.h>
#include <crispy/StrongLRUHashtable.h>
#include <crispy/assert.h>
#include <crispy/boxed.h>

#include <fmt/format.h>

#include <variant> // monostate
#include <vector>

namespace terminal::renderer::atlas
{

using Buffer = std::vector<uint8_t>;

enum class Format
{
    Red = 1,
    RGB = 3,
    RGBA = 4
};

constexpr uint32_t element_count(atlas::Format format) noexcept
{
    return static_cast<uint32_t>(format);
}

// -----------------------------------------------------------------------
// informational data structures

/**
 * Unique identifier of a tile in a fixed-size grid texture atlas.
 *
 * The 32-bit integer can be decomposed into two 16-bit X and Y offsets,
 * whereas Y-offset is in the most-significant 16 bits,
 * and X-offset in the least-significant 16 bits.
 *
 * With this property, the texture size of the atlas need not to be known
 * for computing the tile offset into the texture atlas.
 */
struct AtlasTileID
{
    uint32_t value;
};

/**
 * Describes the location of a tile in an atlas.
 *
 * NB: The tile-size is fixed as the atlas-grid is fixed-size.
 */
struct TileLocation
{
    // clang-format off
    struct X { uint16_t value; };
    struct Y { uint16_t value; };
    // struct RelativeX { float value; };
    // struct RelativeY { float value; };
    // struct RelativeWidth { float value; };
    // struct RelativeHeight { float value; };
    // clang-format on

    // X-offset of the tile into the texture atlas.
    X x {};

    // Y-offset of the tile into the texture atlas.
    Y y {};

    constexpr TileLocation(X ax, Y ay) noexcept: x { ax }, y { ay } {}

    constexpr TileLocation() noexcept = default;
    constexpr TileLocation(TileLocation const&) noexcept = default;
    constexpr TileLocation(TileLocation&&) noexcept = default;
    constexpr TileLocation& operator=(TileLocation const&) noexcept = default;
    constexpr TileLocation& operator=(TileLocation&&) noexcept = default;
};

struct NormalizedTileLocation
{
    float x {};
    float y {};
    float width {};
    float height {};
};

// An texture atlas is holding fixed sized tiles in a grid.
//
// The tiles are identified using a 32-bit Integer (AtlasTileID) that can
// be decomposed into X and Y coordinate pointing into the atlas texture's
// coordinate system.
struct AtlasProperties
{
    // Texture pixel format, such as monochrome, RGB, or RGBA.
    Format format {};

    // Size in pixels of a tile.
    ImageSize tileSize {};

    // Number of tiles the texture atlas must be able to store at least.
    uint32_t tileCount {};

    // Number of direct-mapped tile slots.
    //
    // This can be for example [A-Za-z0-9], characters that are most often
    // used and least likely part of a ligature.
    uint32_t directMappingCount {};
};

// This has absolutely no productivity value but makes inspecting the texture
// atlas more nice when filled.
double inline constexpr TileWidthMultiplier = 1.0;  // 1.5;
double inline constexpr TileHeightMultiplier = 1.0; // 1.5;

// -----------------------------------------------------------------------
// command data structures

// Command structure to (re-)construct a texture atlas.
struct ConfigureAtlas
{
    // Texture atlas size in pixels.
    crispy::ImageSize size {};

    AtlasProperties properties {};
};

// Command structure for uploading a tile into the texture atlas.
struct UploadTile
{
    TileLocation location;
    Buffer bitmap; // texture data to be uploaded
    ImageSize bitmapSize;
    Format bitmapFormat;
};

// Command structure for rendering a tile from a texture atlas.
struct RenderTile
{
    // clang-format off
    struct X { int value; };
    struct Y { int value; };
    // clang-format on

    X x {};                       // target X coordinate to start rendering to
    Y y {};                       // target Y coordinate to start rendering to
    ImageSize bitmapSize {};      // bitmap size inside the tile (must not exceed the grid's tile size)
    std::array<float, 4> color;   // optional; a color being associated with this texture
    TileLocation tileLocation {}; // what tile to render from which texture atlas

    NormalizedTileLocation normalizedLocation {};

    uint32_t fragmentShaderSelector {};
};

constexpr std::array<float, 4> normalize(terminal::RGBColor color, float alpha) noexcept
{
    return std::array<float, 4> { static_cast<float>(color.red) / 255.f,
                                  static_cast<float>(color.green) / 255.f,
                                  static_cast<float>(color.blue) / 255.f,
                                  alpha };
}

constexpr std::array<float, 4> normalize(terminal::RGBAColor color) noexcept
{
    return std::array<float, 4> { static_cast<float>(color.red()) / 255.f,
                                  static_cast<float>(color.green()) / 255.f,
                                  static_cast<float>(color.blue()) / 255.f,
                                  static_cast<float>(color.alpha()) / 255.f };
}

// -----------------------------------------------------------------------
// interface

/// Generic listener API to events from an TextureAtlas.
/// AtlasBackend interface, performs the actual atlas operations, such as
/// texture creation, upload, render, and destruction.
///
/// @see OpenGLRenderer
class AtlasBackend
{
  public:
    virtual ~AtlasBackend() = default;

    virtual ImageSize atlasSize() const noexcept = 0;

    /// Creates a new texture atlas, effectively destroying any prior existing one
    /// as there  can be only one atlas.
    virtual void configureAtlas(ConfigureAtlas atlas) = 0;

    /// Uploads given texture to the atlas.
    virtual void uploadTile(UploadTile tile) = 0;

    /// Renders given texture from the atlas with the given target position parameters.
    virtual void renderTile(RenderTile tile) = 0;
};

// Defines location of the tile in the atlas and its associated metadata
template <typename Metadata>
struct TileAttributes
{
    TileLocation location;
    ImageSize bitmapSize; // size of the bitmap inside the tile
    Metadata metadata;
};

/**
 * Manages the tiles of a single texture atlas.
 *
 * Atlas items are LRU-cached and the possibly passed metadata is
 * going to be destroyed at the time of cache eviction.
 *
 * The total number of of cachable tiles should be at least as large
 * as the terminal's cell count per page.
 * More tiles will most likely improve render performance.
 *
 * The metadata can be for example the render offset relative to the
 * target render base position and the actual tile size
 * (which must be smaller or equal to the tile size).
 */
template <typename Metadata = std::monostate>
class TextureAtlas
{
  public:
    /// Initializes this texture atlas given the passed AtlasProperties.
    ///
    /// This will create at least one atlas in the backend.
    TextureAtlas(AtlasBackend& backend, AtlasProperties atlasProperties);

    ~TextureAtlas();

    void reset(AtlasProperties atlasProperties);

    AtlasBackend& backend() noexcept { return _backend; }

    ImageSize atlasSize() const noexcept { return _atlasSize; }
    ImageSize tileSize() const noexcept { return _atlasProperties.tileSize; }

    // Tests in LRU-cache if the tile
    constexpr bool contains(crispy::StrongHash const& _id) const noexcept;

    // Return type for in-place tile-construction callback.
    struct TileCreateData
    {
        Buffer bitmap; // RGBA bitmap data
        Format bitmapFormat;
        ImageSize bitmapSize;
        Metadata metadata;
    };

    /// Always returns either the existing item by the given key, if found,
    /// or a newly created one by invoking constructValue().
    template <typename CreateTileDataFn>
    [[nodiscard]] TileAttributes<Metadata>& get_or_emplace(crispy::StrongHash const& key,
                                                           CreateTileDataFn constructValue);

    [[nodiscard]] TileAttributes<Metadata> const* try_get(crispy::StrongHash const& key);

    template <typename CreateTileDataFn>
    [[nodiscard]] TileAttributes<Metadata> const* get_or_try_emplace(crispy::StrongHash const& key,
                                                                     CreateTileDataFn constructValue);

    /// Explicitly create or overwrites a tile for the given hash key.
    template <typename CreateTileDataFn>
    void emplace(crispy::StrongHash const& key, CreateTileDataFn constructValue);

    void remove(crispy::StrongHash key);

    // Uploads tile data to a direct-mapped slot in the texture atlas
    // bypassing the LRU cache.
    //
    // The index must be between 0 and number of direct-mapped tiles minus 1.
    void setDirectMapping(uint32_t index, TileCreateData tileCreateData);

    // Receives a reference to the metadata of a direct-mapped tile slot.
    //
    // The index must be between 0 and number of direct-mapped tiles minus 1.
    TileAttributes<Metadata> const& directMapped(uint32_t index) const;

    bool isDirectMappingEnabled() const noexcept { return !_directMapping.empty(); }

    TileLocation tileLocation(uint32_t index) const { return _tileLocations[index]; }

    // Retrieves the number of total tiles that can be stored.
    size_t capacity() const noexcept { return _tileLocations.size(); }

    void inspect(std::ostream& output) const;

    uint32_t tilesInX() const noexcept { return _tilesInX; }
    uint32_t tilesInY() const noexcept { return _tilesInY; }

  private:
    using TileCache = crispy::StrongLRUHashtable<TileAttributes<Metadata>>;
    using TileCachePtr = typename TileCache::Ptr;

    template <typename CreateTileDataFn>
    std::optional<TileAttributes<Metadata>> constructTile(CreateTileDataFn fn, uint32_t entryIndex);

    AtlasBackend& _backend;
    AtlasProperties _atlasProperties;
    ImageSize _atlasSize;
    uint32_t _tilesInX;
    uint32_t _tilesInY;

    // The number of entries of this cache must at most match the number
    // of tiles that can be stored into the atlas.
    TileCachePtr _tileCache;

    // A vector of precomputed mappings from entry index to TileLocation.
    std::vector<TileLocation> _tileLocations;

    // A vector holding the tile meta data for the direct mapped textures.
    std::vector<TileAttributes<Metadata>> _directTileMapping;

    // human readable name for debugging/introspection purpose only.
    std::string _name;

    std::vector<TileAttributes<Metadata>> _directMapping;
};

template <typename Metadata = std::monostate>
struct DirectMapping
{
    using TileCreateData = typename TextureAtlas<Metadata>::TileCreateData;

    uint32_t baseIndex = 0;
    uint32_t count = 0;

    constexpr operator bool() const noexcept { return count != 0; }
    constexpr bool operator!() const noexcept { return count == 0; }

    uint32_t toTileIndex(uint32_t directMappingIndex) const noexcept
    {
        Require(directMappingIndex < count);
        return baseIndex + directMappingIndex;
    }
};

template <typename Metadata = std::monostate>
struct DirectMappingAllocator
{
    using DirectMapping = DirectMapping<Metadata>;

    uint32_t currentlyAllocatedCount = 0;
    bool enabled = true;

    /// Allocates a new DirectMapping container.
    ///
    /// Returns either mapping for the fully requested count or an empty mapping.
    DirectMapping allocate(uint32_t count)
    {
        if (!enabled)
            return DirectMapping {};

        uint32_t const baseIndex = currentlyAllocatedCount;
        currentlyAllocatedCount += count;
        // fmt::print("DirectMappingAllocator.allocate: {} .. {} (#{})\n",
        //            baseIndex, baseIndex + count - 1, count);
        return DirectMapping { baseIndex, count };
    }
};

struct TileSliceIndex
{
    uint32_t sliceIndex;
    uint32_t beginX;
    uint32_t endX;
};

/// Constructs a container to conveniently iterate over sliced tiles of the given
/// input `bitmapSize`.
constexpr auto sliced(Width tileWidth, uint32_t offsetX, ImageSize bitmapSize)
{
    struct Container
    {
        Width tileWidth;
        uint32_t offsetX;
        ImageSize bitmapSize;

        struct iterator
        {
            Width tileWidth;
            TileSliceIndex value;
            constexpr TileSliceIndex const& operator*() const noexcept { return value; }
            constexpr bool operator==(iterator const& rhs) const noexcept
            {
                return value.beginX == rhs.value.beginX;
            }
            constexpr bool operator!=(iterator const& rhs) const noexcept
            {
                return value.beginX != rhs.value.beginX;
            }
            constexpr iterator& operator++() noexcept
            {
                value.sliceIndex++;
                value.beginX = value.endX;
                value.endX += unbox<uint32_t>(tileWidth);
                return *this;
            }
        };

        constexpr uint32_t offsetForEndX() const noexcept
        {
            auto const c = unbox<uint32_t>(bitmapSize.width) % unbox<uint32_t>(tileWidth);
            return unbox<uint32_t>(bitmapSize.width) + c;
        }

        constexpr iterator begin() noexcept
        {
            return iterator { tileWidth,
                              TileSliceIndex {
                                  0,                         // index
                                  offsetX,                   // begin
                                  unbox<uint32_t>(tileWidth) // end
                              } };
        }
        constexpr iterator end() noexcept
        {
            return iterator { tileWidth,
                              TileSliceIndex {
                                  0,               // index (irrelevant, undefind)
                                  offsetForEndX(), // begin
                                  offsetForEndX()  // end
                              } };
        }
    };
    return Container { tileWidth, offsetX, bitmapSize };
}

// {{{ implementation

inline ImageSize computeAtlasSize(AtlasProperties const& atlasProperties) noexcept
{
    using std::ceil;
    using std::sqrt;

    // clang-format off
    auto const totalTileCount = crispy::detail::nextPowerOfTwo(atlasProperties.tileCount + atlasProperties.directMappingCount);
    auto const squareEdgeCount = ceil(sqrt(totalTileCount));
    auto const width = Width::cast_from(crispy::detail::nextPowerOfTwo(static_cast<uint32_t>(ceil(
        TileWidthMultiplier * squareEdgeCount * unbox<double>(atlasProperties.tileSize.width)))));
    auto const height = Height::cast_from(crispy::detail::nextPowerOfTwo(static_cast<uint32_t>(ceil(
        TileHeightMultiplier * squareEdgeCount * unbox<double>(atlasProperties.tileSize.height)))));
    // clang-format on

    // fmt::print("computeAtlasSize: tiles {}+{}={} -> texture size {}x{} (tile size {})\n",
    //            atlasProperties.tileCount,
    //            atlasProperties.directMappingCount,
    //            totalTileCount,
    //            width,
    //            height,
    //            atlasProperties.tileSize);

    return ImageSize { width, height };
}

template <typename Metadata>
TextureAtlas<Metadata>::TextureAtlas(AtlasBackend& backend, AtlasProperties atlasProperties):
    _backend { backend },
    _atlasProperties { atlasProperties },
    _atlasSize { computeAtlasSize(_atlasProperties) },
    _tilesInX { static_cast<uint32_t>(
        floor(unbox<double>(_atlasSize.width)
              / (unbox<double>(_atlasProperties.tileSize.width) * TileWidthMultiplier))) },
    _tilesInY { static_cast<uint32_t>(floor(
        unbox<uint32_t>(_atlasSize.height / _atlasProperties.tileSize.height) * TileHeightMultiplier)) },
    _tileCache { TileCache::create(crispy::StrongHashtableSize { _tilesInX * _tilesInY },
                                   crispy::LRUCapacity { _tilesInX * _tilesInY },
                                   "LRU cache for texture atlas") },
    _tileLocations { static_cast<size_t>(_tilesInX * _tilesInY) }
{
    // fmt::print("TextureAtlas: tiles {}x{} (locations: {}) texture {}; props {}\n",
    //            _tilesInX,
    //            _tilesInY,
    //            _tileLocations.size(),
    //            _atlasSize,
    //            _atlasProperties);

    Require(_tileLocations.size() >= _atlasProperties.directMappingCount + _atlasProperties.tileCount);

    auto data = ConfigureAtlas {};
    data.size = _atlasSize;
    data.properties = _atlasProperties;
    _backend.configureAtlas(data);

    // The StrongLRUHashtable's passed entryIndex can be used
    // to construct the texture atlas' tile coordinates.
    for (size_t entryIndex = 0; entryIndex < _tileLocations.size(); ++entryIndex)
    {
        // clang-format off
        auto const xBase =
            static_cast<uint16_t>(
                (entryIndex % _tilesInX) *
                _atlasProperties.tileSize.width.value *
                TileWidthMultiplier
            );

        auto const yBase =
            static_cast<uint16_t>(
                floor(
                    static_cast<double>(entryIndex / _tilesInX) *
                    unbox<double>(_atlasProperties.tileSize.height) *
                    TileHeightMultiplier
                )
            );
        // clang-format on

        _tileLocations[entryIndex] = TileLocation({ xBase }, { yBase });
    }

    _directMapping.resize(_atlasProperties.directMappingCount);
}

template <typename Metadata>
TextureAtlas<Metadata>::~TextureAtlas()
{
}

template <typename Metadata>
constexpr bool TextureAtlas<Metadata>::contains(crispy::StrongHash const& _id) const noexcept
{
    return _tileCache->contains(_id);
}

template <typename Metadata>
template <typename CreateTileDataFn>
auto TextureAtlas<Metadata>::constructTile(CreateTileDataFn createTileData, uint32_t entryIndex)
    -> std::optional<TileAttributes<Metadata>>
{
    auto const tileIndex = entryIndex + _atlasProperties.directMappingCount;
    auto const tileLocation = _tileLocations[tileIndex];

    std::optional<TileCreateData> tileCreateDataOpt = createTileData(tileLocation, entryIndex);
    if (!tileCreateDataOpt)
        return std::nullopt;

    TileCreateData& tileCreateData = *tileCreateDataOpt;

    auto tileUpload = UploadTile {};
    tileUpload.location = tileLocation;
    tileUpload.bitmapSize = tileCreateData.bitmapSize;
    tileUpload.bitmapFormat = tileCreateData.bitmapFormat;
    tileUpload.bitmap = std::move(tileCreateData.bitmap);
    _backend.uploadTile(std::move(tileUpload));

    auto instance = TileAttributes<Metadata> {};
    instance.location = tileLocation;
    instance.bitmapSize = tileCreateData.bitmapSize;
    instance.metadata = std::move(tileCreateData.metadata);

    return instance;
}

template <typename Metadata>
template <typename CreateTileDataFn>
TileAttributes<Metadata>& TextureAtlas<Metadata>::get_or_emplace(crispy::StrongHash const& key,
                                                                 CreateTileDataFn createTileData)
{
    return _tileCache->get_or_emplace(key,
                                      [&](uint32_t entryIndex) -> std::optional<TileAttributes<Metadata>> {
                                          return constructTile(std::move(createTileData), entryIndex);
                                      });
}

template <typename Metadata>
TileAttributes<Metadata> const* TextureAtlas<Metadata>::try_get(crispy::StrongHash const& key)
{
    return _tileCache->try_get(key);
}

template <typename Metadata>
template <typename CreateTileDataFn>
[[nodiscard]] TileAttributes<Metadata> const* TextureAtlas<Metadata>::get_or_try_emplace(
    crispy::StrongHash const& key, CreateTileDataFn createTileData)
{
    return _tileCache->get_or_try_emplace(
        key, [&](uint32_t entryIndex) -> std::optional<TileAttributes<Metadata>> {
            return constructTile(std::move(createTileData), entryIndex);
        });
}

template <typename Metadata>
template <typename CreateTileDataFn>
void TextureAtlas<Metadata>::emplace(crispy::StrongHash const& key, CreateTileDataFn createTileData)
{
    // clang-format off
    _tileCache->emplace(
        key,
        [&](uint32_t entryIndex) -> TileAttributes<Metadata>
        {
            return constructTile(
                [&](TileLocation location, uint32_t /*entryIndex*/)
                -> std::optional<TileCreateData>
                {
                    return { createTileData(location) };
                },
                entryIndex
            ).value();
        }
    );
    // clang-format on
}

template <typename Metadata>
void TextureAtlas<Metadata>::remove(crispy::StrongHash key)
{
    _tileCache->remove(key);
}

template <typename Metadata>
void TextureAtlas<Metadata>::reset(AtlasProperties atlasProperties)
{
    _atlasProperties = atlasProperties;
    _tileCache->clear();
}

template <typename Metadata>
TileAttributes<Metadata> const& TextureAtlas<Metadata>::directMapped(uint32_t index) const
{
    Require(index < _directMapping.size());
    return _directMapping[index];
}

template <typename Metadata>
void TextureAtlas<Metadata>::setDirectMapping(uint32_t index, TileCreateData tileCreateData)
{
    Require(index < _directMapping.size());

    auto const tileLocation = _tileLocations[index];

    auto tileUpload = UploadTile {};
    tileUpload.location = tileLocation;
    tileUpload.bitmapSize = tileCreateData.bitmapSize;
    tileUpload.bitmapFormat = tileCreateData.bitmapFormat;
    tileUpload.bitmap = std::move(tileCreateData.bitmap);
    _backend.uploadTile(std::move(tileUpload));

    auto instance = TileAttributes<Metadata> {};
    instance.location = tileLocation;
    instance.bitmapSize = tileCreateData.bitmapSize;
    instance.metadata = std::move(tileCreateData.metadata);

    _directMapping[index] = std::move(instance);
}

template <typename Metadata>
void TextureAtlas<Metadata>::inspect(std::ostream& output) const
{
    output << fmt::format("TextureAtlas\n");
    output << fmt::format("------------------------\n");
    output << fmt::format("atlas size     : {}\n", _atlasSize);
    output << fmt::format("tile size      : {}\n", _atlasProperties.tileSize);
    output << fmt::format("direct mapped  : {}\n", _atlasProperties.directMappingCount);
    output << '\n';
    _tileCache->inspect(output);
}

// }}}

} // namespace terminal::renderer::atlas

// {{{ fmt
namespace fmt
{
template <>
struct formatter<terminal::renderer::atlas::Format>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::renderer::atlas::Format value, FormatContext& ctx)
    {
        switch (value)
        {
        case terminal::renderer::atlas::Format::Red: return format_to(ctx.out(), "R");
        case terminal::renderer::atlas::Format::RGB: return format_to(ctx.out(), "RGB");
        case terminal::renderer::atlas::Format::RGBA: return format_to(ctx.out(), "RGBA");
        }
        return format_to(ctx.out(), "{}", static_cast<unsigned>(value));
    }
};

template <>
struct formatter<terminal::renderer::atlas::TileLocation>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::renderer::atlas::TileLocation value, FormatContext& ctx)
    {
        return format_to(ctx.out(), "Tile {}x+{}y", value.x.value, value.y.value);
    }
};

template <>
struct formatter<terminal::renderer::atlas::RenderTile>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::renderer::atlas::RenderTile const& value, FormatContext& ctx)
    {
        return format_to(
            ctx.out(), "RenderTile({}x + {}y, {})", value.x.value, value.y.value, value.tileLocation);
    }
};

template <>
struct formatter<terminal::renderer::atlas::AtlasProperties>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::renderer::atlas::AtlasProperties const& value, FormatContext& ctx)
    {
        return format_to(ctx.out(),
                         "tile size {}, format {}, direct-mapped {}",
                         value.tileSize,
                         value.format,
                         value.directMappingCount);
    }
};
} // namespace fmt
// }}}
