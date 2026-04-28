#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <algorithm>
namespace fs = std::filesystem;

static std::string normPath(std::string s) {
    for (char &c : s) if (c == '\\') c = '/';
    return s;
}

struct PakEntry {
    char name[32];
    uint32_t offset;
    uint32_t size;
};

struct FileSpec {
    std::string path;
    bool required;
};

int main() {
    /* Auto-discover all binaries under assets/ */
    std::vector<std::string> mapBins;
    if (fs::is_directory("assets")) {
        for (auto &entry : fs::recursive_directory_iterator("assets")) {
            auto ext = entry.path().extension();
            if (ext == ".bin" || ext == ".til" || ext == ".mus")
                mapBins.push_back(normPath(entry.path().string()));
        }
        std::sort(mapBins.begin(), mapBins.end());
    }
    if (mapBins.empty()) {
        std::cerr << "No .bin/.til files found in assets/\n";
        return 1;
    }

    std::vector<FileSpec> specs;
    for (auto &m : mapBins) specs.push_back({ m, true });
    specs.push_back({ "assets/data/player.dat",       true  });
    specs.push_back({ "assets/data/items.dat",        false });
    specs.push_back({ "assets/data/enemies.dat",      false });
    specs.push_back({ "assets/data/dialog.dat",       false });
    specs.push_back({ "assets/data/quests.dat",       false });
    specs.push_back({ "assets/data/loottables.dat",   false });
    specs.push_back({ "assets/data/npcs.dat",         false });

    /* Resolve which files are present */
    std::vector<std::string> files;
    for (auto &s : specs) {
        std::ifstream test(s.path, std::ios::binary);
        if (test) {
            files.push_back(s.path);
        } else if (s.required) {
            std::cerr << "Required file missing: " << s.path << "\n";
            return 1;
        } else {
            std::cerr << "Optional file not found, skipping: " << s.path << "\n";
        }
    }

    std::ofstream out("data.pak", std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open data.pak for writing\n";
        return 1;
    }

    uint16_t count = (uint16_t)files.size();
    out.write("PAK0", 4);
    out.write((char*)&count, 2);

    std::vector<PakEntry> entries(count);
    out.write((char*)entries.data(), count * sizeof(PakEntry));

    uint32_t offset = 4 + 2 + count * sizeof(PakEntry);

    for (int i = 0; i < (int)count; i++) {
        std::ifstream in(files[i], std::ios::binary);
        if (!in) {
            std::cerr << "Failed to open: " << files[i] << "\n";
            return 1;
        }

        in.seekg(0, std::ios::end);
        uint32_t size = (uint32_t)in.tellg();
        in.seekg(0);

        strncpy(entries[i].name, files[i].c_str(), 31);
        entries[i].name[31] = '\0';
        entries[i].offset = offset;
        entries[i].size   = size;

        std::vector<char> buffer(size);
        in.read(buffer.data(), size);

        out.seekp(offset);
        out.write(buffer.data(), size);

        offset += size;
    }

    out.seekp(6);
    out.write((char*)entries.data(), count * sizeof(PakEntry));

    std::cout << "Packed " << count << " file(s) into data.pak\n";
    return 0;
}
