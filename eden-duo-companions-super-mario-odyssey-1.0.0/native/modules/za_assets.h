// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Pokémon Legends Z-A's own art and text, decoded at runtime from the player's romfs.
//
// The game stores everything in one Trinity archive: `data.trpfd` is a FlatBuffer listing
// 9,892 pack paths with their sizes, `data.trpfs` is the container itself (a 3.3 GB file with
// a FlatBuffer index at its end mapping an FNV-1a hash of each path to a byte offset). Each
// pack is a small "trpak": a header table, a hash table, and one or more inner files, each
// either stored raw or compressed with Oodle Kraken. Inside are ordinary Switch containers
// (BNTX textures, SARC archives).
//
// Nothing here ships a byte of the game: every call reads the player's own installation
// through the host's read_romfs, so a package stays asset-free (the same rule the other
// companions follow). Only offsets and hashes are pinned, and those are structure, not art.
//
// What this file exposes:
//   * SpeciesName(): the French species names out of the game's encrypted message table.
//   * Icon(): the 160x160 party/box thumbnail of a species (BC7, block-linear).
//   * Map(): the full Lumiose plan (BC1 4096x4096) box-filtered down for the map page.

#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace za {

/// An RGBA8 picture, tightly packed, ready for the host's image sink.
struct Picture {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool Valid() const {
        return width != 0 && height != 0 &&
               rgba.size() == static_cast<std::size_t>(width) * height * 4;
    }
    void Clear() {
        width = 0;
        height = 0;
        rgba.clear();
    }
};

class Assets {
public:
    /// The host's read_romfs: a bare path is a romfs path; returns the bytes copied, or the
    /// file's size when `output` is null.
    using ReadFn = std::size_t (*)(void* userdata, const char* path, std::uint64_t offset,
                                   void* output, std::size_t size);

    /// Called once, from the module's create(). Safe to call from any thread afterwards.
    void SetReader(void* userdata, ReadFn read);

    /// 1-based species id -> French name, UTF-8 (gendered species read "NidoranF"/"NidoranM").
    /// Returns "" before Load() has succeeded and for ids outside the table.
    const char* SpeciesName(int species);
    /// True once the pack index and the name table are in memory.
    [[nodiscard]] bool NamesReady() const;
    /// Builds the pack index and the name table if it has not been tried yet. Safe to call
    /// from any thread; the first caller does the work (a few hundred milliseconds) and the
    /// rest wait. Returns whether names are available.
    bool EnsureNames();
    std::string ItemName(int item);
    std::string ItemDescription(int item);
    struct DexEntry {int species=0,form=0,number=0;};
    std::vector<DexEntry> Pokedex(int category);
    struct Mission {std::string id,title,description,objectives,work;};
    std::vector<Mission> Missions(int category);
    struct Personal {int type1=-1,type2=-1,growth=-1;};
    Personal SpeciesInfo(int internal_species,int form);
    int MoveType(int move);
    std::uint32_t ExperienceAt(int growth,int level);
    struct Zone {int number=0,texture=0;float x=0,y=0,w=0,h=0;};
    struct Center {std::string id;float x=0,z=0;int destination=0;std::string name;};
    std::vector<Zone> Zones();
    int MapZoneAt(int x,int y);
    std::vector<Center> Centers();
    bool CenterIcon(Picture& out);
    bool ZoneIcon(int index,Picture& out);
    bool TypeIcon(int type,Picture& out);
    std::string MoveName(int move);
    std::string MoveDescription(int move);
    std::string SpeciesDescription(int internal_species);
    bool HasSpecies(int species) const;

    /// The party/box icon of a species (160x160). `form` is the save's form id; 0 is the
    /// ordinary sprite and is what the pages show. False when the pack is missing.
    bool Icon(int species, int form, Picture& out, bool shiny=false);
    /// Bag artwork, indexed by each BNTX's own item_<id> name, not by pack order.
    bool ItemIcon(int item, Picture& out);
    /// The whole Lumiose plan, box-filtered down to `size` x `size` (the page draws it 1:1).
    bool Map(unsigned size, Picture& out);

private:
    struct PackRef {
        std::uint64_t offset = 0;
        std::uint32_t size = 0;
    };

    /// One icon pack found while indexing: the save's form selects among them.
    struct IconRef {
        int form = 0;
        std::string path;
    };

    bool LoadLocked(); ///< builds packs_ and names_; the caller holds mutex_
    bool ReadRomfs(const char* path, std::uint64_t offset, void* out, std::size_t size);
    std::uint64_t RomfsSize(const char* path);
    bool Fetch(const char* path, std::vector<std::uint8_t>& out);
    bool FetchInner(const char* path, std::size_t index, std::vector<std::uint8_t>& out);
    bool IconLocked(int species, int form, Picture& out, bool shiny);
    bool IndexItemsLocked();

    mutable std::mutex mutex_;
    void* userdata_ = nullptr;
    ReadFn read_ = nullptr;
    std::string archive_root_;
    std::vector<std::string> item_names_;
    std::vector<std::string> item_descriptions_;
    std::vector<DexEntry> pokedex_;
    std::vector<Mission> missions_[3];
    std::vector<std::string> move_names_;
    std::vector<std::string> move_descriptions_;
    std::vector<std::string> species_descriptions_;
    bool item_index_tried_ = false;
    std::vector<std::uint8_t> item_pack_;
    std::unordered_map<int, std::size_t> item_indices_;
    std::unordered_map<int, Picture> item_icons_;
    std::vector<int> item_icon_order_;
    std::chrono::steady_clock::time_point retry_at_{};
    bool tried_ = false;
    bool ready_ = false;
    std::unordered_map<std::uint64_t, PackRef> packs_;
    std::vector<Zone> zones_;
    std::vector<Center> centers_;
    std::vector<std::uint8_t> map_ui_bntx_;
    std::unordered_map<int, std::vector<IconRef>> icon_packs_;
    std::vector<std::string> names_;
    std::unordered_map<int, Picture> icons_;
    std::vector<int> icon_order_;
    Picture map_;
    std::vector<std::uint8_t> map_zone_hits_;
    std::unordered_map<int,Personal> personal_;
    std::vector<int> move_types_;
    std::vector<std::uint32_t> experience_;
    std::unordered_map<int,Picture> type_icons_;
    std::vector<std::uint8_t> type_bntx_;
};

} // namespace za
