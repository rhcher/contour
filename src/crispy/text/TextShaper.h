/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019 Christian Parpart <christian@parpart.family>
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

#include <crispy/text/Font.h>
#include <crispy/reference.h>

#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>

#include <string>
#include <string_view>

namespace crispy::text {

/**
 * Performs the actual text shaping.
 */
class TextShaper {
  public:
    TextShaper();
    ~TextShaper();

    /// Renders codepoints into glyph positions with the first font fully matching all codepoints.
    ///
    /// @param _font        the font list in priority order to be used for text shaping
    /// @param _size        number of codepoints to be shaped
    /// @param _codepoints  array of codepoints to be shaped
    /// @param _clusters    cluster assignments for each codepoint (must be of equal size of @p _code_codepoints)
    ///
    /// @returns pointer to the shape result
    GlyphPositionList const& shape(FontList const& _font,
                                   size_t _size,
                                   char32_t const* _codepoints,
                                   unsigned const* _clusters);

    /// Replaces all missing glyphs with the missing-glyph glyph.
    void replaceMissingGlyphs(Font& _font, GlyphPositionList& _result);

    // Clears the internal font cache.
    void clearCache();

  private:
    /// Performs text shaping for given text using the given font.
    bool shape(size_t _size,
               char32_t const* _codepoints,
               unsigned const* _clusters,
               Font& _font,
               reference<GlyphPositionList> _result);

    /// Inserts a new cache value @p _glyphPosition by given key _cacheKey into the cache.
    ///
    /// @returns const pointer to the stored glyph positions object.
    GlyphPositionList const& cache(std::u32string_view const& _cacheKey, GlyphPositionList&& _glyphPosition);

  private:
    hb_buffer_t* hb_buf_;
    std::unordered_map<Font const*, hb_font_t*> hb_fonts_ = {};
    std::unordered_map<std::u32string_view, std::u32string> cacheKeys_;
    std::unordered_map<std::u32string_view, GlyphPositionList> cache_;
};

} // end namespace