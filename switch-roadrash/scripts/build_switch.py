import os
import sys
import shutil
import subprocess
import concurrent.futures
import time
import hashlib
import glob

DEVKITPRO = r"C:/devkitPro"
A64_CXX = os.path.join(DEVKITPRO, "devkitA64", "bin", "aarch64-none-elf-g++.exe")
ELF2NRO = os.path.join(DEVKITPRO, "tools", "bin", "elf2nro.exe")
NACPTOOL = os.path.join(DEVKITPRO, "tools", "bin", "nacptool.exe")

ROOT_DIR = r"C:/Users/schim/Downloads/road rash"
SRC_DIR = os.path.join(ROOT_DIR, "road-rash-arkits", "src")
SWITCH_DIR = os.path.join(ROOT_DIR, "switch-roadrash")
BUILD_DIR = os.path.join(SWITCH_DIR, "build")
ROMFS_DIR = os.path.join(SWITCH_DIR, "romfs")
OUTPUT_ELF = os.path.join(SWITCH_DIR, "roadrash_switch.elf")
OUTPUT_NACP = os.path.join(SWITCH_DIR, "roadrash_switch.nacp")
OUTPUT_NRO = os.path.join(SWITCH_DIR, "roadrash_switch.nro")
OUTPUT_BASE_NRO = os.path.join(SWITCH_DIR, "roadrash_base.nro")
ICON_JPG = os.path.join(SWITCH_DIR, "icon.jpg")

os.makedirs(BUILD_DIR, exist_ok=True)

# List of all engine sources
ENGINE_SOURCES = [
    # Core resource & game objects
    "resource.cpp", "course.cpp", "road.cpp", "assets.cpp", "framebuffer.cpp",
    "bike.cpp", "race.cpp", "input.cpp", "bob.cpp", "session.cpp",
    "iso9660.cpp", "sha256.cpp", "importer.cpp", "import_job.cpp", "wav.cpp",
    "mixer.cpp", "ima_adpcm.cpp", "avi.cpp", "cinepak.cpp", "video_player.cpp",
    "raster.cpp", "terrain.cpp", "scene.cpp", "polygon.cpp", "texture.cpp",
    "cel.cpp", "world_renderer.cpp", "render_threads.cpp", "road_panels.cpp",
    "road_materials.cpp", "exe_tables.cpp", "pe_strings.cpp", "career.cpp",
    "text.cpp", "settings.cpp", "rri.cpp", "frontend.cpp", "frontend_net.cpp", "sound.cpp",
    "explode.cpp", "rider_sprites.cpp", "mmpx.cpp", "upscale_cache.cpp",
    "atomic_file.cpp", "display.cpp", "native_strings.cpp", "native_screens.cpp",
    "view_state.cpp", "texture_perspective.cpp", "framebuffer_rgb.cpp",
    "raster_rgb.cpp", "fog.cpp", "post_chain.cpp", "upscale_import.cpp",
    "race_actions.cpp", "race_overlay.cpp", "race_helpers.cpp", "game_types.cpp",
    "race_screen.cpp", "family.cpp", "terrain_panels.cpp", "building_panels.cpp",
    "terrain_ground.cpp", "course_scene.cpp", "hud.cpp", "animation.cpp",
    "actor_animation.cpp", "actor_descriptors.cpp", "actor_render.cpp",
    "actor_nodes.cpp", "crash.cpp", "police.cpp", "scenery.cpp", "traffic.cpp",
    "pedestrians.cpp", "race_pedestrians.cpp", "race_schedule.cpp",
    "race_music.cpp", "race_setup.cpp", "track_ring.cpp", "race_state.cpp",
    "det_math.cpp", "utf8_path.cpp", "system_fonts.cpp", "platform_files.cpp",
    "platform_linux.cpp", "file_browser.cpp", "road_draw.cpp", "import_ui.cpp", "gpu_race.cpp",
    "platform_game.cpp", "game_main.cpp"
]

SPECIAL_SOURCES = [
    (os.path.join(SRC_DIR, "net", "rollback.cpp"), "rollback.o"),
    (os.path.join(SRC_DIR, "net", "protocol.cpp"), "net_protocol.o"),
    (os.path.join(SRC_DIR, "net", "loopback_channel.cpp"), "net_loopback.o"),
    (os.path.join(SRC_DIR, "net", "client.cpp"), "net_client.o"),
    (os.path.join(SRC_DIR, "net", "compat.cpp"), "net_compat.o"),
    (os.path.join(SRC_DIR, "net", "mano.cpp"), "net_mano.o"),
    (os.path.join(SWITCH_DIR, "source", "wine_wingdings.cpp"), "wine_wingdings.o"),
    (os.path.join(SWITCH_DIR, "source", "switch_platform.cpp"), "switch_platform.o"),
    (os.path.join(SWITCH_DIR, "source", "ws_channel_switch.cpp"), "ws_channel_switch.o"),
]

INCLUDES = [
    os.path.join(SWITCH_DIR, "include"),
    SRC_DIR,
    os.path.join(ROOT_DIR, "road-rash-arkits", "third_party"),
    os.path.join(DEVKITPRO, "libnx", "include"),
    os.path.join(DEVKITPRO, "portlibs", "switch", "include"),
    os.path.join(DEVKITPRO, "portlibs", "switch", "include", "freetype2"),
]

LIBDIRS = [
    os.path.join(DEVKITPRO, "libnx", "lib"),
    os.path.join(DEVKITPRO, "portlibs", "switch", "lib"),
]

LIBS = [
    "-lSDL2", "-lfreetype", "-lharfbuzz", "-lturbojpeg", "-lpng", "-lz", "-lbz2",
    "-lGLESv2", "-lEGL", "-lglapi", "-ldrm_nouveau", "-ldeko3d", "-lnx", "-lpthread", "-lm"
]

ARCH_FLAGS = [
    "-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIE"
]

def _compute_sim_source_hash():
    files = []
    for ext in ("*.cpp", "*.hpp"):
        files.extend(glob.glob(os.path.join(SRC_DIR, "**", ext), recursive=True))
    files.append(os.path.join(SWITCH_DIR, "source", "ws_channel_switch.cpp"))
    files.sort(key=lambda p: os.path.relpath(p, ROOT_DIR).replace("\\", "/"))
    hashes = []
    for path in files:
        with open(path, "rb") as f:
            text = f.read().replace(b"\r\n", b"\n")
        hashes.append(hashlib.sha256(text).hexdigest())
    return hashlib.sha256("".join(hashes).encode("ascii")).hexdigest()

SIM_SOURCE_HASH = _compute_sim_source_hash()

CXX_FLAGS = ARCH_FLAGS + [
    "-O2", "-g", "-ffunction-sections", "-fdata-sections",
    "-std=gnu++20", "-D__SWITCH__", "-D_GNU_SOURCE",
    "-DRR_NET_TRANSPORT=1",
    f'-DRR_SIM_SOURCE_HASH="{SIM_SOURCE_HASH}"'
] + [f"-I{inc}" for inc in INCLUDES]

LINK_FLAGS = [
    f"-specs={os.path.join(DEVKITPRO, 'libnx', 'switch.specs')}",
    "-g",
    "-Wl,--wrap=__cxa_throw",
] + ARCH_FLAGS + [f"-L{d}" for d in LIBDIRS] + LIBS

def _newest_header_mtime():
    newest = 0.0
    for root, _, files in os.walk(os.path.join(SWITCH_DIR, "include")):
        for f in files:
            newest = max(newest, os.path.getmtime(os.path.join(root, f)))
    return newest

HEADER_MTIME = _newest_header_mtime()

def compile_source(src_path, obj_path):
    if os.path.exists(obj_path) and os.path.getmtime(obj_path) > max(os.path.getmtime(src_path), HEADER_MTIME):
        return True, obj_path, f"Up to date: {os.path.basename(src_path)}"
    cmd = [A64_CXX] + CXX_FLAGS + ["-c", src_path, "-o", obj_path]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        return False, obj_path, f"FAILED: {os.path.basename(src_path)}\n{res.stderr}\n{res.stdout}"
    return True, obj_path, f"Compiled: {os.path.basename(src_path)}"

def main():
    print("=== Building Nintendo Switch Homebrew: Road Rash Native ===")
    tasks = []
    for s in ENGINE_SOURCES:
        src = os.path.join(SRC_DIR, s)
        base = os.path.splitext(s)[0]
        obj = os.path.join(BUILD_DIR, f"{base}.o")
        tasks.append((src, obj))
    for src, obj_name in SPECIAL_SOURCES:
        obj = os.path.join(BUILD_DIR, obj_name)
        tasks.append((src, obj))

    start = time.time()
    failed = False
    obj_files = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 8) as executor:
        futures = {executor.submit(compile_source, src, obj): (src, obj) for src, obj in tasks}
        for future in concurrent.futures.as_completed(futures):
            ok, obj_path, msg = future.result()
            if not ok:
                print(msg)
                failed = True
            else:
                obj_files.append(obj_path)

    if failed:
        print("Compilation failed.")
        sys.exit(1)

    print(f"Compiled {len(tasks)} object files in {time.time() - start:.2f}s")
    print(f"Linking {OUTPUT_ELF}...")
    link_cmd = [A64_CXX] + [os.path.join(BUILD_DIR, f"{os.path.splitext(os.path.basename(s))[0]}.o") for s in ENGINE_SOURCES]
    for _, obj_name in SPECIAL_SOURCES:
        link_cmd.append(os.path.join(BUILD_DIR, obj_name))
    link_cmd += LINK_FLAGS + ["-o", OUTPUT_ELF]

    res = subprocess.run(link_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print("Link failed:\n", res.stderr, res.stdout)
        sys.exit(1)
    print("ELF linked successfully.")

    # Patch out ARMv9.4-A Guarded Control Stack (GCS) instructions in libgcc unwinder
    # Switch Cortex-A57 is ARMv8.0 and crashes on mrs x1, gcspr_el0 (0xd53b2521)
    with open(OUTPUT_ELF, "rb") as f:
        elf_bytes = bytearray(f.read())
    target = bytes.fromhex("21 25 3b d5")
    idx = 0
    patched_count = 0
    while True:
        idx = elf_bytes.find(target, idx)
        if idx == -1:
            break
        # Check instruction right before
        prev4 = elf_bytes[idx-4:idx]
        if prev4 == bytes.fromhex("a0 fb ff b4"): # cbz x0, -35
            elf_bytes[idx-4:idx] = bytes.fromhex("dd ff ff 17") # b -35
        elif prev4 == bytes.fromhex("80 fb ff b4"): # cbz x0, -36
            elf_bytes[idx-4:idx] = bytes.fromhex("dc ff ff 17") # b -36
        elf_bytes[idx:idx+4] = bytes.fromhex("1f 20 03 d5") # nop
        patched_count += 1
        idx += 4
    if patched_count > 0:
        with open(OUTPUT_ELF, "wb") as f:
            f.write(elf_bytes)
        print(f"Patched {patched_count} ARMv9 GCS unwinder instructions in ELF for ARMv8 compatibility.")

    # Generate NACP
    print("Generating NACP...")
    nacp_cmd = [NACPTOOL, "--create", "Road Rash", "Homebrew Port", "1.0.0", OUTPUT_NACP]
    subprocess.run(nacp_cmd, check=True)

    # Generate NRO
    print("Packaging NRO with RomFS...")
    nro_cmd = [
        ELF2NRO, OUTPUT_ELF, OUTPUT_NRO,
        f"--icon={ICON_JPG}",
        f"--nacp={OUTPUT_NACP}",
        f"--romfsdir={ROMFS_DIR}"
    ]
    subprocess.run(nro_cmd, check=True)
    print(f"BUILD SUCCESS: {OUTPUT_NRO} created (Size: {os.path.getsize(OUTPUT_NRO)} bytes)")

    # Generate asset-free base NRO for the shippable ISO->NRO builder
    base_nro_cmd = [
        ELF2NRO, OUTPUT_ELF, OUTPUT_BASE_NRO,
        f"--icon={ICON_JPG}",
        f"--nacp={OUTPUT_NACP}",
    ]
    subprocess.run(base_nro_cmd, check=True)
    builder_dist_dir = os.path.join(ROOT_DIR, "RoadRash-Switch-Builder")
    if os.path.isdir(builder_dist_dir):
        shutil.copy2(OUTPUT_BASE_NRO, os.path.join(builder_dist_dir, "roadrash_base.nro"))
        print(f"Updated {os.path.join(builder_dist_dir, 'roadrash_base.nro')}")
    web_dist_dir = os.path.join(ROOT_DIR, "gh-pages-builder")
    if os.path.isdir(web_dist_dir):
        shutil.copy2(OUTPUT_BASE_NRO, os.path.join(web_dist_dir, "roadrash_base.nro"))
        print(f"Updated {os.path.join(web_dist_dir, 'roadrash_base.nro')}")

    # Also update sdcard_release copy
    sd_nro = os.path.join(ROOT_DIR, "sdcard_release", "switch", "roadrash", "roadrash_switch.nro")
    os.makedirs(os.path.dirname(sd_nro), exist_ok=True)
    shutil.copy2(OUTPUT_NRO, sd_nro)
    print(f"Copied to {sd_nro}")

if __name__ == "__main__":
    main()
