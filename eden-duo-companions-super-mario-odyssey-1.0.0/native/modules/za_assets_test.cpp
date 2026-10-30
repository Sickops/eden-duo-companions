// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Development harness for za_assets.cpp: points the decoder at an already-extracted romfs
// directory (the same layout the host's read_romfs resolves) and dumps what it produces, so
// the pack/BNTX/BCn path can be checked without booting the game.
//
//   za_assets_test <romfs dir> <out dir>
//
// Not part of the module: the module reads through the host's read_romfs instead.

#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include "za_assets.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

std::string g_root;
bool g_available = true;
std::string g_out;
bool g_failed = false;

std::size_t ReadRomfs(void*, const char* path, std::uint64_t offset, void* output,
                      std::size_t size) {
    if (!g_available) return 0;
    const std::string full = g_root + "/" + path;
    std::FILE* const file = std::fopen(full.c_str(), "rb");
    if (file == nullptr) {
        return 0;
    }
    // 64-bit seeks: data.trpfs is 3.3 GB, so every pack offset is past what a 32-bit long can
    // hold (the host's own reader is 64-bit throughout).
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return 0;
    }
    const auto length = _ftelli64(file);
    if (length < 0) {
        std::fclose(file);
        return 0;
    }
    const auto total = static_cast<std::uint64_t>(length);
    if (output == nullptr) {
        std::fclose(file);
        return static_cast<std::size_t>(total);
    }
    if (offset >= total) {
        std::fclose(file);
        return 0;
    }
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(size, total - offset));
    if (_fseeki64(file, static_cast<long long>(offset), SEEK_SET) != 0) {
        std::fclose(file);
        return 0;
    }
    const std::size_t got = std::fread(output, 1, count, file);
    std::fclose(file);
    return got;
}

void Dump(const za::Picture& picture, const std::string& name) {
    const std::string path = g_out + "/" + name;
    if (!picture.Valid()) {
        std::printf("  %-28s INVALID\n", name.c_str());
        return;
    }
    if (!stbi_write_png(path.c_str(), static_cast<int>(picture.width), static_cast<int>(picture.height),
                   4, picture.rgba.data(), static_cast<int>(picture.width) * 4)) g_failed = true;
    std::printf("  %-28s %ux%u -> %s\n", name.c_str(), picture.width, picture.height, path.c_str());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <romfs dir> <out dir>\n", argv[0]);
        return 2;
    }
    g_root = argv[1];
    g_out = argv[2];
    std::filesystem::create_directories(g_out);
    za::Assets assets;
    assets.SetReader(nullptr, &ReadRomfs);
    if (argc > 3 && std::strcmp(argv[3], "--retry") == 0) {
        g_available = false;
        if (assets.EnsureNames()) return 1;
        g_available = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        if (!assets.EnsureNames()) return 1;
        std::puts("delayed romfs: retry recovered");
    }

    if (!assets.EnsureNames()) g_failed = true;
    std::printf("names ready: %d\n", assets.NamesReady() ? 1 : 0);
    if (std::strcmp(assets.SpeciesName(25), "Pikachu") != 0) g_failed = true;
    if (assets.ItemName(4) != "Pok\xC3\xA9 Ball") g_failed = true;
    static const int kSpecies[] = {1, 4, 25, 35, 133, 150, 152, 650, 664, 666, 682, 688, 493, 906, 1000};
    for (int species : kSpecies) {
        std::printf("  #%04d %s\n", species, assets.SpeciesName(species));
    }

    za::Picture picture;
    std::printf("icons:\n");
    for (int species : kSpecies) {
        if (assets.Icon(species, 0, picture)) {
            char name[64];
            std::snprintf(name, sizeof(name), "icon_%04d.png", species);
            Dump(picture, name);
        } else {
            std::printf("  icon %04d absent or failed\n", species);
            if (species == 25 || species == 650 || species == 688) g_failed = true;
        }
    }
    std::printf("map:\n");
    if (assets.Map(640, picture)) {
        Dump(picture, "map_640.png");
    } else {
        std::printf("  map FAILED\n");
        g_failed = true;
    }
    return g_failed ? 1 : 0;
}
