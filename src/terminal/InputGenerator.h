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
#pragma once

#include <terminal/DECTextLocator.h>
#include <terminal/Sequencer.h> // MouseProtocol
#include <terminal/primitives.h>

#include <crispy/escape.h>
#include <crispy/overloaded.h>

#include <unicode/convert.h>

#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace terminal
{

// {{{ Modifier
class Modifier
{
  public:
    enum Key : unsigned
    {
        None = 0,
        Shift = 1,
        Alt = 2,
        Control = 4,
        Meta = 8,
    };

    constexpr Modifier(Key _key): mask_ { static_cast<unsigned>(_key) } {}

    constexpr Modifier() = default;
    constexpr Modifier(Modifier&&) = default;
    constexpr Modifier(Modifier const&) = default;
    constexpr Modifier& operator=(Modifier&&) = default;
    constexpr Modifier& operator=(Modifier const&) = default;

    constexpr unsigned value() const noexcept { return mask_; }

    constexpr bool none() const noexcept { return value() == 0; }
    constexpr bool some() const noexcept { return value() != 0; }
    constexpr bool shift() const noexcept { return value() & Shift; }
    constexpr bool alt() const noexcept { return value() & Alt; }
    constexpr bool control() const noexcept { return value() & Control; }
    constexpr bool meta() const noexcept { return value() & Meta; }

    constexpr operator unsigned() const noexcept { return mask_; }

    constexpr bool any() const noexcept { return mask_ != 0; }

    constexpr Modifier& operator|=(Modifier const& _other) noexcept
    {
        mask_ |= _other.mask_;
        return *this;
    }

    constexpr Modifier with(Modifier const& _other) const noexcept
    {
        return Modifier(static_cast<Key>(mask_ | _other.mask_));
    }

    constexpr Modifier without(Modifier const& _other) const noexcept
    {
        return Modifier(static_cast<Key>(mask_ & ~_other.mask_));
    }

    bool contains(Modifier const& _other) const noexcept { return (mask_ & _other.mask_) == _other.mask_; }

    constexpr void enable(Key _key) noexcept { mask_ |= _key; }

    constexpr void disable(Key _key) noexcept { mask_ &= ~static_cast<unsigned>(_key); }

  private:
    unsigned mask_ = 0;
};

constexpr bool operator<(Modifier _lhs, Modifier _rhs) noexcept
{
    return _lhs.value() < _rhs.value();
}

constexpr bool operator==(Modifier _lhs, Modifier _rhs) noexcept
{
    return _lhs.value() == _rhs.value();
}

std::optional<Modifier::Key> parseModifierKey(std::string const& _key);

constexpr bool operator!(Modifier _modifier) noexcept
{
    return _modifier.none();
}

constexpr bool operator==(Modifier _lhs, Modifier::Key _rhs) noexcept
{
    return static_cast<Modifier::Key>(_lhs.value()) == _rhs;
}

constexpr Modifier operator+(Modifier::Key _lhs, Modifier::Key _rhs) noexcept
{
    return Modifier(static_cast<Modifier::Key>(static_cast<unsigned>(_lhs) | static_cast<unsigned>(_rhs)));
}

/// @returns CSI parameter for given function key modifier
constexpr size_t makeVirtualTerminalParam(Modifier _modifier) noexcept
{
    return 1 + _modifier.value();
}

std::string to_string(Modifier _modifier);

// }}}
// {{{ KeyInputEvent, Key
enum class Key
{
    // function keys
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,

    // cursor keys
    DownArrow,
    LeftArrow,
    RightArrow,
    UpArrow,

    // 6-key editing pad
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,

    // numpad keys
    Numpad_NumLock,
    Numpad_Divide,
    Numpad_Multiply,
    Numpad_Subtract,
    Numpad_CapsLock,
    Numpad_Add,
    Numpad_Decimal,
    Numpad_Enter,
    Numpad_Equal,
    Numpad_0,
    Numpad_1,
    Numpad_2,
    Numpad_3,
    Numpad_4,
    Numpad_5,
    Numpad_6,
    Numpad_7,
    Numpad_8,
    Numpad_9,
};

std::string to_string(Key _key);

enum class KeyMode
{
    Normal,
    Application
};
// }}}

class InputGenerator
{
  public:
    using Sequence = std::vector<char>;

    /// Changes the input mode for cursor keys.
    void setCursorKeysMode(KeyMode _mode);

    /// Changes the input mode for numpad keys.
    void setNumpadKeysMode(KeyMode _mode);

    void setApplicationKeypadMode(bool _enable);

    bool normalCursorKeys() const noexcept { return cursorKeysMode_ == KeyMode::Normal; }
    bool applicationCursorKeys() const noexcept { return !normalCursorKeys(); }

    bool numericKeypad() const noexcept { return numpadKeysMode_ == KeyMode::Normal; }
    bool applicationKeypad() const noexcept { return !numericKeypad(); }

    bool bracketedPaste() const noexcept { return bracketedPaste_; }
    void setBracketedPaste(bool _enable) { bracketedPaste_ = _enable; }

    void setMouseProtocol(MouseProtocol _mouseProtocol, bool _enabled);
    std::optional<MouseProtocol> mouseProtocol() const noexcept { return mouseProtocol_; }

    // Sets mouse event transport protocol (default, extended, xgr, urxvt)
    void setMouseTransport(MouseTransport _mouseTransport);
    MouseTransport mouseTransport() const noexcept { return mouseTransport_; }

    enum class MouseWheelMode
    {
        // mouse wheel generates mouse wheel events as determined by mouse protocol + transport.
        Default,
        // mouse wheel generates normal cursor key events
        NormalCursorKeys,
        // mouse wheel generates application cursor key events
        ApplicationCursorKeys
    };

    void setMouseWheelMode(MouseWheelMode _mode) noexcept;
    MouseWheelMode mouseWheelMode() const noexcept { return mouseWheelMode_; }

    void setGenerateFocusEvents(bool _enable) noexcept { generateFocusEvents_ = _enable; }
    bool generateFocusEvents() const noexcept { return generateFocusEvents_; }

    bool generate(char32_t _characterEvent, Modifier _modifier);
    bool generate(std::u32string const& _characterEvent, Modifier _modifier);
    bool generate(Key _key, Modifier _modifier);
    void generatePaste(std::string_view const& _text);
    bool generateMousePress(Modifier _modifier,
                            MouseButton _button,
                            CellLocation _pos,
                            MousePixelPosition _pixelPosition);
    bool generateMouseMove(Modifier _modifier, CellLocation _pos, MousePixelPosition _pixelPosition);
    bool generateMouseRelease(Modifier _modifier,
                              MouseButton _button,
                              CellLocation _pos,
                              MousePixelPosition _pixelPosition);

    bool generateFocusInEvent();
    bool generateFocusOutEvent();

    /// Generates raw input, usually used for sending reply VT sequences.
    bool generateRaw(std::string_view const& _raw);

    /// Peeks into the generated output, returning it as string view.
    ///
    /// @return a view into the generated buffer sequence.
    std::string_view peek() const noexcept
    {
        return std::string_view(pendingSequence_.data() + consumedBytes_,
                                pendingSequence_.size() - consumedBytes_);
    }

    void consume(int n)
    {
        consumedBytes_ += n;
        if (consumedBytes_ == pendingSequence_.size())
        {
            consumedBytes_ = 0;
            pendingSequence_.clear();
        }
    }

    enum class MouseEventType
    {
        Press,
        Drag,
        Release
    };

    /// Resets the input generator's state, as required by the RIS (hard reset) VT sequence.
    void reset();

    void lock() noexcept { mutex_.lock(); }
    void unlock() noexcept { mutex_.unlock(); }

    DECTextLocator* textLocator() noexcept { return &textLocator_; };

  private:
    bool generateMouse(MouseEventType _eventType,
                       Modifier _modifier,
                       MouseButton _button,
                       CellLocation _pos,
                       MousePixelPosition _pixelPosition);

    bool mouseTransport(MouseEventType _eventType,
                        uint8_t _button,
                        uint8_t _modifier,
                        CellLocation _pos,
                        MousePixelPosition _pixelPosition);
    bool mouseTransportX10(uint8_t _button, uint8_t _modifier, CellLocation _pos);

    bool mouseTransportSGR(MouseEventType _type, uint8_t _button, uint8_t _modifier, int x, int y);

    bool mouseTransportURXVT(MouseEventType _type, uint8_t _button, uint8_t _modifier, CellLocation _pos);

    inline bool append(std::string_view _sequence);
    inline bool append(char _asciiChar);
    inline bool append(uint8_t _byte);
    inline bool append(unsigned int _asciiChar);

    // private fields
    //
    KeyMode cursorKeysMode_ = KeyMode::Normal;
    KeyMode numpadKeysMode_ = KeyMode::Normal;
    bool bracketedPaste_ = false;
    bool generateFocusEvents_ = false;
    std::optional<MouseProtocol> mouseProtocol_ = std::nullopt;
    MouseTransport mouseTransport_ = MouseTransport::Default;
    MouseWheelMode mouseWheelMode_ = MouseWheelMode::Default;
    Sequence pendingSequence_ {};
    int consumedBytes_ {};
    std::mutex mutex_ {};

    std::set<MouseButton> currentlyPressedMouseButtons_ {};
    CellLocation currentMousePosition_ {}; // current mouse position

    DECTextLocator textLocator_;
};

inline std::string to_string(InputGenerator::MouseEventType _value)
{
    switch (_value)
    {
    case InputGenerator::MouseEventType::Press: return "Press";
    case InputGenerator::MouseEventType::Drag: return "Drag";
    case InputGenerator::MouseEventType::Release: return "Release";
    }
    return "???";
}

} // namespace terminal

namespace fmt // {{{
{
template <>
struct formatter<terminal::Modifier>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::Modifier _modifier, FormatContext& _ctx)
    {
        std::string s;
        auto const advance = [&](bool _cond, std::string_view _text) {
            if (!_cond)
                return;
            if (!s.empty())
                s += ',';
            s += _text;
        };
        advance(_modifier.alt(), "Alt");
        advance(_modifier.shift(), "Shift");
        advance(_modifier.control(), "Control");
        advance(_modifier.meta(), "Meta");
        if (s.empty())
            s = "None";
        return format_to(_ctx.out(), "{}", s);
    }
};

template <>
struct formatter<terminal::InputGenerator::MouseWheelMode>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::InputGenerator::MouseWheelMode _value, FormatContext& _ctx)
    {
        switch (_value)
        {
        case terminal::InputGenerator::MouseWheelMode::Default: return format_to(_ctx.out(), "Default");
        case terminal::InputGenerator::MouseWheelMode::NormalCursorKeys:
            return format_to(_ctx.out(), "NormalCursorKeys");
        case terminal::InputGenerator::MouseWheelMode::ApplicationCursorKeys:
            return format_to(_ctx.out(), "ApplicationCursorKeys");
        }
        return format_to(_ctx.out(), "<{}>", unsigned(_value));
    }
};

template <>
struct formatter<terminal::KeyMode>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::KeyMode _value, FormatContext& _ctx)
    {
        switch (_value)
        {
        case terminal::KeyMode::Application: return format_to(_ctx.out(), "Application");
        case terminal::KeyMode::Normal: return format_to(_ctx.out(), "Normal");
        }
        return format_to(_ctx.out(), "<{}>", unsigned(_value));
    }
};

template <>
struct formatter<terminal::MouseButton>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::MouseButton _value, FormatContext& _ctx)
    {
        switch (_value)
        {
        case terminal::MouseButton::Left: return format_to(_ctx.out(), "Left");
        case terminal::MouseButton::Right: return format_to(_ctx.out(), "Right");
        case terminal::MouseButton::Middle: return format_to(_ctx.out(), "Middle");
        case terminal::MouseButton::Release: return format_to(_ctx.out(), "Release");
        case terminal::MouseButton::WheelUp: return format_to(_ctx.out(), "WheelUp");
        case terminal::MouseButton::WheelDown: return format_to(_ctx.out(), "WheelDown");
        }
        return format_to(_ctx.out(), "<{}>", unsigned(_value));
    }
};

template <>
struct formatter<terminal::MouseTransport>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::MouseTransport _value, FormatContext& _ctx)
    {
        switch (_value)
        {
        case terminal::MouseTransport::Default: return format_to(_ctx.out(), "Default");
        case terminal::MouseTransport::Extended: return format_to(_ctx.out(), "Extended");
        case terminal::MouseTransport::SGR: return format_to(_ctx.out(), "SGR");
        case terminal::MouseTransport::URXVT: return format_to(_ctx.out(), "URXVT");
        case terminal::MouseTransport::SGRPixels: return format_to(_ctx.out(), "SGR-Pixels");
        }
        return format_to(_ctx.out(), "<{}>", unsigned(_value));
    }
};

} // namespace fmt
