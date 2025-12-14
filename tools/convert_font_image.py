#!/usr/bin/env python3
"""
Convert PICO-8 font image (128x128, 16x16 grid of 8x8 characters) to C array data.
This tool converts the font image to format suitable for embedding in TIC-80 sprites.

Usage: python convert_font_image.py font.png > ../src/pico8_font_data.inl

The input image should be:
- 128x128 pixels (16x16 grid of 8x8 pixel characters)
- Grayscale or RGB (will be converted to monochrome)
- Black/dark pixels represent "on" pixels (character foreground)
- White/light pixels represent "off" pixels (character background)
- Characters ordered 0-255, left-to-right, top-to-bottom

Output format:
- C array compatible with TIC-80 sprite data
- Each character is 8 bytes (one byte per row, bits represent pixels)
- Can be embedded directly in TIC-80 cartridge as sprites
"""
import sys

try:
    from PIL import Image
    from PIL import UnidentifiedImageError
except ImportError:
    print("ERROR: PIL/Pillow not installed.", file=sys.stderr)
    print("Install with: pip install Pillow", file=sys.stderr)
    sys.exit(1)

# Constants
PIXEL_THRESHOLD = 128  # Threshold for determining if a pixel is "on" (dark) or "off" (light)

def image_to_font_data(image_path):
    """Convert a PICO-8 font image to C array data suitable for TIC-80 sprites."""
    try:
        img = Image.open(image_path).convert('L')  # Convert to grayscale
    except FileNotFoundError:
        print(f"ERROR: Image file '{image_path}' not found.", file=sys.stderr)
        sys.exit(1)
    except UnidentifiedImageError:
        print(f"ERROR: '{image_path}' is not a valid image file.", file=sys.stderr)
        print("Supported formats: PNG, GIF, JPEG, BMP", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: Could not open image '{image_path}': {e}", file=sys.stderr)
        sys.exit(1)
    
    width, height = img.size
    if width != 128 or height != 128:
        print(f"ERROR: Image must be 128x128 pixels, got {width}x{height}", file=sys.stderr)
        print("The image should contain a 16x16 grid of 8x8 pixel characters", file=sys.stderr)
        sys.exit(1)
    
    # Get pixel data
    pixels = img.load()
    
    # Output header
    print("/* PICO-8 Font Data - Generated from", image_path, "*/")
    print("/* 256 characters, 8 bytes each (8 rows of 8 pixels) */")
    print("/* Format: Each byte represents one row, bits represent pixels (LSB=left, MSB=right) */")
    print()
    
    # Extract font data for all 256 characters (16x16 grid of 8x8 characters)
    for char_row in range(16):
        for char_col in range(16):
            char_index = char_row * 16 + char_col
            char_bytes = []
            
            # Extract 8x8 pixels for this character
            for pixel_row in range(8):
                byte_val = 0
                for pixel_col in range(8):
                    # Calculate pixel position in the full image
                    px = char_col * 8 + pixel_col
                    py = char_row * 8 + pixel_row
                    
                    # Check if pixel is "on" (dark pixel means foreground)
                    if pixels[px, py] < PIXEL_THRESHOLD:
                        byte_val |= (1 << pixel_col)
                
                char_bytes.append(byte_val)
            
            # Output as C array element
            hex_bytes = ", ".join(f"0x{b:02x}" for b in char_bytes)
            
            # Add comment with character info
            comment = f"/* {char_index:3d} 0x{char_index:02x}"
            if 32 <= char_index < 127:
                comment += f" '{chr(char_index)}'"
            comment += " */"
            
            print(f"    {hex_bytes}, {comment}")

def main():
    if len(sys.argv) != 2:
        print("PICO-8 Font Image to C Array Converter", file=sys.stderr)
        print("", file=sys.stderr)
        print("Usage: python convert_font_image.py <font_image.png>", file=sys.stderr)
        print("", file=sys.stderr)
        print("Input: 128x128 pixel image with 16x16 grid of 8x8 characters", file=sys.stderr)
        print("Output: C array data suitable for embedding in TIC-80 sprites", file=sys.stderr)
        sys.exit(1)
    
    image_to_font_data(sys.argv[1])

if __name__ == "__main__":
    main()
