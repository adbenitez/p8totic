# PICO-8 Font Support in TIC-80

## Issue

PICO-8 has a custom font with 256 characters (0-255), including special symbols and glyphs that are commonly used in PICO-8 games:

- **Special symbols**: ❎ (X button), 🅾️ (O button), arrows (⬆️⬇️⬅️➡️)
- **Block graphics**: █, ▒, ░, ▮, ■, □, etc.
- **Japanese characters**: Hiragana and Katakana (あいうえお, アイウエオ, etc.)
- **Other symbols**: ♥, ★, ●, ◆, etc.

When converting PICO-8 cartridges to TIC-80, these characters are converted to UTF-8 equivalents. However, TIC-80's default font doesn't render many of these UTF-8 characters properly, especially:
- Emoji characters (🐱, 😐, 웃)
- Some special symbols
- Combining characters with variation selectors

## Current Solution

**ASCII Fallback Mappings** (v1.0)

The converter now maps problematic emoji characters to ASCII-compatible alternatives that render correctly in TIC-80:

| PICO-8 Char | Unicode | ASCII Fallback | Note |
|-------------|---------|----------------|------|
| 130 (0x82)  | 🐱 (cat) | `^.^` | Cat face |
| 132 (0x84)  | ️░ | `░` | Light shade (emoji variant removed) |
| 137 (0x89)  | 웃 | `:)` | Smiling face |
| 139 (0x8B)  | ⬅️ | `←` | Left arrow (Unicode, not emoji) |
| 140 (0x8C)  | 😐 | `:I` | Neutral face |
| 142 (0x8E)  | 🅾️ | `(O)` | O button |
| 145 (0x91)  | ➡️ | `→` | Right arrow (Unicode, not emoji) |
| 148 (0x94)  | ⬆️ | `↑` | Up arrow (Unicode, not emoji) |
| 131 (0x83)  | ⬇️ | `↓` | Down arrow (Unicode, not emoji) |
| 151 (0x97)  | ❎ | `(X)` | X button |

Other Unicode characters (block graphics, Japanese characters, basic symbols) are kept as UTF-8 since TIC-80 can render most of them.

## Future Enhancement: Full Font Sprite Support

For games that require the exact PICO-8 font appearance, a future enhancement could embed the complete PICO-8 font as sprites in the TIC-80 cartridge.

### Implementation Plan

1. **Font Image**: Convert the PICO-8 font image (128x128 pixels, 16x16 grid of 8x8 characters) to sprite data using:
   ```bash
   python3 tools/convert_font_image.py pico8_font.png > src/pico8_font_data.inl
   ```

2. **Sprite Storage**: Store font sprites in TIC-80 cartridge:
   - Option A: Use sprites 0-127 for font (requires game to use sprites 128-255)
   - Option B: Use sprites 128-255 for font (requires game to use sprites 0-127)
   - Option C: Use TIC-80 Pro's additional sprite banks

3. **Custom Print Function**: Add to the Lua wrapper:
   ```lua
   function __p8_print_with_font(str, x, y, col)
       for i = 1, #str do
           local c = string.byte(str, i)
           spr(c, x + (i-1)*8, y, 0)  -- Draw character as sprite
       end
   end
   ```

4. **Integration**: Modify the converter to optionally enable font sprite mode.

### Trade-offs

**ASCII Fallback (Current)**:
- ✅ No sprite usage - games can use all 256 sprites
- ✅ Works in TIC-80 free version
- ✅ Simple implementation
- ❌ Not pixel-perfect to PICO-8 font
- ❌ Some characters look different

**Font Sprites (Future)**:
- ✅ Pixel-perfect PICO-8 font rendering
- ✅ All 256 characters supported exactly
- ❌ Uses 128-256 sprite slots
- ❌ Slightly slower rendering
- ❌ Requires game sprite reorgan ization

## For Game Developers

If your PICO-8 game heavily uses special characters (especially buttons ❎🅾️ or faces), you may want to:

1. **Use the ASCII fallbacks**: These render correctly in TIC-80 and are readable, though not identical to PICO-8.

2. **Customize the text**: If specific symbols are critical, you can modify them in your game's Lua code after conversion.

3. **Wait for font sprite support**: A future version may add optional full PICO-8 font rendering via sprites.

## Testing

To test character rendering:
1. Convert a PICO-8 cart that uses special characters
2. Open in TIC-80
3. Check if characters render correctly
4. Report any rendering issues

## Contributing

If you have the PICO-8 font image or want to contribute to font sprite support:
1. Provide the 128x128 PICO-8 font image
2. Test the font conversion tool
3. Submit improvements or bug reports
