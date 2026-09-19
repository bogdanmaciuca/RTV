import os
import shutil
import subprocess

# --- Configuration ---
SHADER_DIR = "shaders"
OUTPUT_HEADER = "src/engine/generated/screen_quad_shaders.h"
# ---------------------

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

def process_shader_pair(name, shader_dir=SHADER_DIR):
    """
    Given a shader base name (e.g. 'text'), compiles <name>.vert and <name>.frag
    to temporary SPIR-V files, converts them into C arrays (<name>_vs_bytecode,
    <name>_fs_bytecode), cleans up temp files, and returns the generated code string.
    """
    vert_src = os.path.join(shader_dir, f"{name}.vert")
    frag_src = os.path.join(shader_dir, f"{name}.frag")
    temp_vert_spv = f"temp_{name}_vert.spv"
    temp_frag_spv = f"temp_{name}_frag.spv"

    try:
        # Compile
        compile_to_spirv(vert_src, temp_vert_spv)
        compile_to_spirv(frag_src, temp_frag_spv)

        # Convert to C arrays
        vert_code = spirv_to_c_array(temp_vert_spv, f"{name}_vs_bytecode")
        frag_code = spirv_to_c_array(temp_frag_spv, f"{name}_fs_bytecode")

        return f"{vert_code}\n{frag_code}\n"
    finally:
        # Cleanup temporary files
        if os.path.exists(temp_vert_spv):
            os.remove(temp_vert_spv)
        if os.path.exists(temp_frag_spv):
            os.remove(temp_frag_spv)

def main():
    check_glslc()

    shaders_to_compile = ["screen_quad", "text"]
    header_content = ["#pragma once\n#include <stdint.h>\n\n"]

    print(f"Generating {OUTPUT_HEADER}...")
    for shader_name in shaders_to_compile:
        header_content.append(process_shader_pair(shader_name))

    os.makedirs(os.path.dirname(OUTPUT_HEADER), exist_ok=True)
    with open(OUTPUT_HEADER, "w") as f:
        f.write("\n".join(header_content))

    print("Done!")

if __name__ == "__main__":
    main()
