import os
import shutil
import subprocess

# --- Configuration (Fill these in) ---
VERT_SRC = "shaders/screen_quad.vert"
FRAG_SRC = "shaders/screen_quad.frag"
OUTPUT_HEADER = "src/generated/screen_quad_shaders.h"
# -------------------------------------

def check_glslc():
    """Check if glslc is available in the system PATH."""
    if not shutil.which("glslc"):
        print("Error: 'glslc' not found in PATH. Make sure the Vulkan SDK is installed and its bin folder is in your PATH.")
        exit(1)

def compile_to_spirv(src_path, spv_path):
    """Compile GLSL source to SPIR-V binary."""
    print(f"Compiling {src_path}...")
    result = subprocess.run(["glslc", src_path, "-o", spv_path], capture_output=True, text=True)

    if result.returncode != 0:
        print(f"Compilation failed for {src_path}:\n{result.stderr}")
        exit(1)

def spirv_to_c_array(spv_path, array_name):
    """Read SPIR-V binary and format it as a C uint32_t array string."""
    with open(spv_path, "rb") as f:
        data = f.read()

    # Vulkan shader code must be aligned to 4 bytes (uint32_t)
    padding = (4 - (len(data) % 4)) % 4
    data += b'\x00' * padding

    # Convert bytes to 32-bit hex words (little-endian)
    words = []
    for i in range(0, len(data), 4):
        word = int.from_bytes(data[i:i+4], byteorder='little')
        words.append(f"0x{word:08x}")

    # Format nicely with 8 words per line
    lines = []
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(words[i:i+8]))

    formatted_array = ",\n".join(lines)
    return f"const uint32_t {array_name}[] = {{\n{formatted_array}\n}};\n"

def main():
    check_glslc()

    # Temporary SPIR-V output files
    vert_spv = "temp_vert.spv"
    frag_spv = "temp_frag.spv"

    # Compile
    compile_to_spirv(VERT_SRC, vert_spv)
    compile_to_spirv(FRAG_SRC, frag_spv)

    # Convert to C arrays
    print(f"Generating {OUTPUT_HEADER}...")
    vert_c_code = spirv_to_c_array(vert_spv, "screen_quad_vs_bytecode")
    frag_c_code = spirv_to_c_array(frag_spv, "screen_quad_fs_bytecode")

    # Write the header file
    with open(OUTPUT_HEADER, "w") as f:
        f.write("#pragma once\n")
        f.write("#include <stdint.h>\n\n")
        f.write(vert_c_code)
        f.write("\n")
        f.write(frag_c_code)

    # Cleanup temporary SPIR-V files
    os.remove(vert_spv)
    os.remove(frag_spv)
    print("Done!")

if __name__ == "__main__":
    main()
