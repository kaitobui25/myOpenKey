import os
from PIL import Image
import colorsys

def shift_hue(image, hue_shift):
    # Convert image to RGBA
    image = image.convert('RGBA')
    pixels = image.load()
    
    # Apply hue shift
    for y in range(image.size[1]):
        for x in range(image.size[0]):
            r, g, b, a = pixels[x, y]
            if a == 0:
                continue
            
            # Convert RGB to HSV
            h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
            
            # Shift Hue
            h = (h + hue_shift) % 1.0
            
            # Convert back to RGB
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            pixels[x, y] = (int(r * 255), int(g * 255), int(b * 255), a)
            
    return image

def process_ico(file_path, hue_shift):
    try:
        img = Image.open(file_path)
        
        # Save all frames if there are multiple
        frames = []
        sizes = []
        
        # ICO files can have multiple sizes. Pillow opens the largest by default or the first one.
        # We need to process all sizes.
        if hasattr(img, 'info') and 'sizes' in img.info:
            original_sizes = img.info['sizes']
        else:
            original_sizes = [img.size]
            
        print(f"Processing {file_path}, sizes: {original_sizes}")
        
        # In Pillow, to read all frames from an ICO:
        for size in original_sizes:
            img.size = size # This doesn't actually select the frame, Pillow ICO plugin has img.seek()
            
        # Actually, standard way for ICO in Pillow:
        # Just use img.seek()
        try:
            while True:
                frames.append(shift_hue(img.copy(), hue_shift))
                sizes.append(img.size)
                img.seek(img.tell() + 1)
        except EOFError:
            pass
            
        # Save back as ICO
        if len(frames) > 0:
            frames[0].save(file_path, format='ICO', sizes=sizes, append_images=frames[1:])
            print(f"Successfully processed {file_path}")
        
    except Exception as e:
        print(f"Error processing {file_path}: {e}")

if __name__ == "__main__":
    base_dir = r"c:\phong\AI\other\OpenKey\Sources\OpenKey\win32\OpenKey\OpenKey"
    
    # Red to Purple: Red hue is 0. Purple is ~270 degrees. Hue shift = 270/360 = 0.75
    process_ico(os.path.join(base_dir, "StatusViet.ico"), 0.8) # Shift to purple
    process_ico(os.path.join(base_dir, "icon.ico"), 0.8) # Shift main icon to purple as well? Actually just StatusViet and StatusEng
    
    # Blue to Green: Blue hue is ~240. Green is ~120. Hue shift = -120/360 = -0.33 -> 0.66
    process_ico(os.path.join(base_dir, "StatusEng.ico"), -0.33)
