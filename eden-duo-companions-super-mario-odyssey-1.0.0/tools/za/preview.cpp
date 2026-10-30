// SPDX-License-Identifier: GPL-3.0-or-later
// Render the actual package with the actual exported Windows module, without booting a game.
// Usage: dsmod_preview <module.dll> <save-copy.bin> <romfs> <dualscreen> <output-dir>
#define main za_fixture_main
#include "../../native/modules/za_module_host_test.cpp"
#undef main
#undef DrawText
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <nlohmann/json.hpp>
#include "common/stb.h"
#include "core/mods/mod_runtime.h"
#include "core/mods/mod_ui.h"
#include "common/logging.h"
#include "common/fs/path_util.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"

using namespace Core::Mods;
namespace fs = std::filesystem;
static FileSys::VirtualDir merged_romfs;

std::vector<uint8_t> Bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    // Coverage, tint, clipping and the unchanged 1:1 path of the host's glyph reduction.
    {
        Image atlas; atlas.w=4; atlas.h=10; atlas.pixels.resize(40);
        for (size_t i=0;i<40;++i) atlas.pixels[i]=(i%2)==0 ? 0xFFFFFFFFu : 0;
        FontMetrics font; font.line_height=10; font.first_codepoint='A';
        font.glyphs.push_back(FontGlyph{0,0,4,10,0,10,4});
        Canvas canvas; canvas.Resize(8,12);canvas.SetFont(&atlas,&font);canvas.Clear(0);
        canvas.DrawText(0,0,"A",1,0xFFFF0000u);
        if (canvas.Pixels()[0] != 0x80FF0000u) return 20;
        canvas.Clear(0);canvas.SetClip(1,0,1,5);canvas.DrawText(0,0,"A",1,0xFFFFFFFFu);
        if (canvas.Pixels()[0] != 0 || canvas.Pixels()[1] != 0x80FFFFFFu) return 21;
        canvas.ResetClip();canvas.Clear(0);canvas.DrawText(0,0,"A",2,0xFFFFFFFFu);
        if (canvas.Pixels()[0] != 0xFFFFFFFFu || canvas.Pixels()[1] != 0) return 22;
        std::puts("glyph filtering: coverage, tint, clipping and 1:1 OK");
    }
    if (argc != 6 && argc != 8) return 2;
    std::shared_ptr<FileSys::NCA> base_nca, update_nca;
    std::shared_ptr<FileSys::RealVfsFilesystem> probe_vfs;
    if (argc == 8) {
        const auto log_dir=fs::absolute(fs::path(argv[5]))/"logs";
        fs::create_directories(log_dir);
        Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir,log_dir);
        Common::Log::Initialize(); Common::Log::Start();
        if (const char* keys = std::getenv("EDEN_ZA_KEYS_DIR"))
            Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, fs::path(keys));
        probe_vfs = std::make_shared<FileSys::RealVfsFilesystem>();
        auto& vfs = probe_vfs;
        base_nca = std::make_shared<FileSys::NCA>(vfs->OpenFile(argv[6], FileSys::OpenMode::Read));
        update_nca = std::make_shared<FileSys::NCA>(vfs->OpenFile(argv[7], FileSys::OpenMode::Read), base_nca.get());
        if (!update_nca->GetRomFS()) { std::printf("NCA status %d\n", int(update_nca->GetStatus())); return 30; }
        std::printf("NCA base=%d update=%d merged=%zu\n",int(base_nca->GetStatus()),int(update_nca->GetStatus()),update_nca->GetRomFS()->GetSize());
        merged_romfs = FileSys::ExtractRomFS(update_nca->GetRomFS());
        if (!merged_romfs) return 31;
        for (const auto& file : merged_romfs->GetFiles())
            std::printf("merged romfs: %s %zu bytes\n", file->GetName().c_str(),file->GetSize());
        for (const auto& dir:merged_romfs->GetSubdirectories()) {
            std::printf("merged directory: %s\n",dir->GetName().c_str());
            for(const auto& file:dir->GetFiles())
                std::printf("merged romfs: %s/%s %zu bytes\n",dir->GetName().c_str(),file->GetName().c_str(),file->GetSize());
        }
    }
    HMODULE library = LoadLibraryA(argv[1]);
    if (!library) return 3;
    auto get = reinterpret_cast<EdenDsmodGetModuleFn>(GetProcAddress(library, "eden_dsmod_get_module"));
    auto save_get = reinterpret_cast<EdenDsmodGetSaveExtensionsFn>(GetProcAddress(library, "eden_dsmod_get_save_extensions"));
    auto ext_get = reinterpret_cast<EdenDsmodGetExtensionsFn>(GetProcAddress(library, "eden_dsmod_get_extensions"));
    if (!get || !save_get || !ext_get) return 4;
    const auto* api = get(EDEN_DSMOD_MODULE_ABI_VERSION, EDEN_DSMOD_MODULE_ABI_HASH);
    const auto* save = save_get(EDEN_DSMOD_SAVE_EXT_VERSION, EDEN_DSMOD_SAVE_EXT_HASH);
    const auto* ext = ext_get(EDEN_DSMOD_EXT_VERSION, EDEN_DSMOD_EXT_HASH);
    if (!api || !save || !ext) return 5;
    FakeHost fixture;
    fixture.save_path = argv[2]; fixture.romfs = argv[3]; fixture.tick = 600;
    auto host = MakeHost(fixture);
    if (merged_romfs) host.read_romfs = +[](void*, const char* path, uint64_t offset, void* out, size_t size) -> size_t {
        std::string name=path;
        if (name.starts_with("romfs:")) name.erase(0,6);
        const auto file=merged_romfs->GetFileRelative(name);
        if (!file) return 0;
        if (!out) return file->GetSize();
        return file->Read(static_cast<uint8_t*>(out), size, offset);
    };
    const auto save_host = MakeSaveApi(fixture);
    auto* instance = api->create(&host, "{}");
    if (!instance) return 6;
    save->configure(instance, &save_host);
    api->tick(instance, &host);
    api->sample(instance, &host);
    std::unordered_map<std::string, std::shared_ptr<const Image>> cache;
    ImageProvider images = [&](const std::string& key) -> std::shared_ptr<const Image> {
        if (cache.contains(key)) return cache.at(key);
        std::shared_ptr<Image> image;
        if (key.starts_with("module:")) {
            ext->load_image(instance, &host, key.c_str(), &image,
                +[](void* out, uint32_t w, uint32_t h, const uint8_t* data, size_t size) {
                    if (size != static_cast<size_t>(w)*h*4) return;
                    auto result = std::make_shared<Image>(); result->w=w; result->h=h;
                    result->pixels.resize(static_cast<size_t>(w)*h);
                    for (size_t i=0; i<result->pixels.size(); ++i)
                        result->pixels[i] = (uint32_t(data[i*4+3])<<24) | (uint32_t(data[i*4])<<16) |
                                            (uint32_t(data[i*4+1])<<8) | data[i*4+2];
                    *static_cast<std::shared_ptr<Image>*>(out) = std::move(result);
                });
        } else if (key.starts_with("file:")) {
            const auto bytes = Bytes(fs::path(argv[4]) / key.substr(5));
            int w=0,h=0,n=0;
            auto* data=stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w,&h,&n,4);
            if (data) {
                image=std::make_shared<Image>(); image->w=w; image->h=h;
                image->pixels.resize(static_cast<size_t>(w)*h);
                for(size_t i=0;i<image->pixels.size();++i)
                    image->pixels[i]=(uint32_t(data[i*4+3])<<24)|(uint32_t(data[i*4])<<16)|
                                      (uint32_t(data[i*4+1])<<8)|data[i*4+2];
                stbi_image_free(data);
            }
        }
        cache[key]=image;
        return image;
    };
    // Trigger name loading before the snapshot used by the renderer.
    if (!images("module:map")) return 7;
    api->sample(instance, &host);
    for(int i=0;i<6;++i) {
        const auto prefix="za.p"+std::to_string(i);
        if(fixture.ints[prefix+".v"] && !images("module:icon/"+std::to_string(fixture.ints[prefix+".icon"])))
            return 32;
    }
    if(const char* actions=std::getenv("EDEN_ZA_PREVIEW_ACTIONS")) {
        std::istringstream stream(actions); std::string action;
        while(std::getline(stream,action,',')) {
            const auto colon=action.find(':');
            if(colon==std::string::npos || !ext->on_action(instance,action.substr(0,colon).c_str(),
                                                         std::stoll(action.substr(colon+1)))) return 33;
        }
        api->sample(instance,&host);
    }
    StateSnapshot snapshot;
    for (const auto& [k,v]:fixture.ints) snapshot.ints[k]=v;
    for (const auto& [k,v]:fixture.floats) snapshot.floats[k]=v;
    for (const auto& [k,v]:fixture.texts) snapshot.texts[k]=v;
    Manifest manifest;
    std::ifstream input(fs::path(argv[4]) / "manifest.json");
    if (!ParseDualScreenManifest(nlohmann::json::parse(input), manifest)) return 8;
    auto font_image=images("file:font/za.png");
    auto metrics_bytes=Bytes(fs::path(argv[4])/"font/za.mfnt");
    auto u32at=[&](size_t i){uint32_t v;std::memcpy(&v,metrics_bytes.data()+i,4);return v;};
    if (metrics_bytes.size()<0x60 || !font_image) return 9;
    FontMetrics font; font.first_codepoint=0x20; font.line_height=u32at(0x1c);
    const auto count=u32at(0x20), first=u32at(0x28);
    if (first+uint64_t(count)*14>metrics_bytes.size()) return 10;
    for(uint32_t i=1;i<count;++i){
        FontGlyph g{}; const auto* p=metrics_bytes.data()+first+i*14;
        std::memcpy(&g.x,p,2);std::memcpy(&g.y,p+2,2);std::memcpy(&g.w,p+4,2);
        std::memcpy(&g.h,p+6,2);std::memcpy(&g.bearing_x,p+8,2);
        std::memcpy(&g.bearing_y,p+10,2);std::memcpy(&g.advance,p+12,2);
        font.glyphs.push_back(g);
    }
    fs::create_directories(argv[5]);
    for(const auto& page:manifest.pages){
        Canvas canvas; canvas.Resize(manifest.canvas_w,manifest.canvas_h);
        canvas.SetFont(font_image.get(),&font);
        if (!RenderPage(canvas,manifest,page,snapshot,images)) return 11;
        std::vector<uint8_t> rgba(canvas.Pixels().size()*4);
        for(size_t i=0;i<canvas.Pixels().size();++i){const auto c=canvas.Pixels()[i];
            rgba[i*4]=c>>16;rgba[i*4+1]=c>>8;rgba[i*4+2]=c;rgba[i*4+3]=c>>24;}
        const auto path=fs::path(argv[5])/(page.id+".png");
        if (!stbi_write_png(path.string().c_str(),canvas.Width(),canvas.Height(),4,rgba.data(),canvas.Width()*4)) return 12;
        std::printf("preview: %s\n",path.string().c_str());
    }
    api->destroy(instance); FreeLibrary(library);
    return 0;
}
