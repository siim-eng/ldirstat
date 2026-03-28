#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "direntrystore.h"

namespace ldirstat {

enum class FileCategory : std::uint16_t {
    Unknown = 0,
    Archive,
    Compressed,
    Database,
    DiskImage,
    Document,
    Package,
    Image,
    BackupTemp,
    Library,
    Log,
    Music,
    ObjectGenerated,
    Source,
    Video,
    Executable,
};

class FileCategorizer final {
public:
    static constexpr std::size_t kCategoryCount =
        static_cast<std::size_t>(FileCategory::Executable) + 1;

    [[nodiscard]] static constexpr std::size_t categoryIndex(FileCategory category) noexcept {
        return static_cast<std::size_t>(category);
    }

    [[nodiscard]] static constexpr const char* categoryName(FileCategory category) noexcept {
        switch (category) {
        case FileCategory::Unknown: return "unknown";
        case FileCategory::Archive: return "archive";
        case FileCategory::Compressed: return "compressed";
        case FileCategory::Database: return "database";
        case FileCategory::DiskImage: return "disk_image";
        case FileCategory::Document: return "document";
        case FileCategory::Package: return "package";
        case FileCategory::Image: return "image";
        case FileCategory::BackupTemp: return "backup_temp";
        case FileCategory::Library: return "library";
        case FileCategory::Log: return "log";
        case FileCategory::Music: return "music";
        case FileCategory::ObjectGenerated: return "object_generated";
        case FileCategory::Source: return "source";
        case FileCategory::Video: return "video";
        case FileCategory::Executable: return "executable";
        }

        return "unknown";
    }

    [[nodiscard]] static constexpr const char* displayCategoryName(FileCategory category) noexcept {
        switch (category) {
        case FileCategory::Unknown: return "Unknown";
        case FileCategory::Archive: return "Archive";
        case FileCategory::Compressed: return "Compressed";
        case FileCategory::Database: return "Database";
        case FileCategory::DiskImage: return "Disk image";
        case FileCategory::Document: return "Document";
        case FileCategory::Package: return "Package";
        case FileCategory::Image: return "Image";
        case FileCategory::BackupTemp: return "Backup/temp";
        case FileCategory::Library: return "Library";
        case FileCategory::Log: return "Log";
        case FileCategory::Music: return "Music";
        case FileCategory::ObjectGenerated: return "Object/generated";
        case FileCategory::Source: return "Source";
        case FileCategory::Video: return "Video";
        case FileCategory::Executable: return "Executable";
        }

        return "Unknown";
    }

    [[nodiscard]] static inline FileCategory categorize(std::string_view utf8Path) noexcept {
        const BasenameView base = findBasename(utf8Path);
        const ExtensionView ext = findExtension(base);
        return categorizeResolved(base, ext);
    }

    struct Result {
        FileCategory category;
        const char* extensionPtr;
        std::uint8_t extensionLen;
    };

    [[nodiscard]] static inline Result categorizeWithExtension(std::string_view utf8Path) noexcept {
        const BasenameView base = findBasename(utf8Path);
        const ExtensionView ext = findExtension(base);
        return {categorizeResolved(base, ext), ext.ptr, ext.len};
    }

private:
    struct BasenameView {
        const char* ptr;
        std::size_t len;
    };

    struct ExtensionView {
        const char* ptr;
        std::uint8_t len;
    };

    [[nodiscard]] static inline bool isSlash(char c) noexcept {
        return c == '/';
    }

    [[nodiscard]] static inline char asciiLower(char c) noexcept {
        const unsigned char uc = static_cast<unsigned char>(c);
        return (uc >= 'A' && uc <= 'Z') ? static_cast<char>(uc | 0x20u) : c;
    }

    [[nodiscard]] static constexpr std::uint64_t lowerMaskForLen(std::uint8_t len) noexcept {
        std::uint64_t mask = 0;
        for (std::uint8_t i = 0; i < len; ++i)
            mask |= (std::uint64_t{0x20} << (i * 8));
        return mask;
    }

    [[nodiscard]] static constexpr std::uint64_t packLiteral(const char* s,
                                                             std::uint8_t len) noexcept {
        std::uint64_t value = 0;
        for (std::uint8_t i = 0; i < len; ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const unsigned char lower =
                (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c | 0x20u) : c;
            value |= (std::uint64_t{lower} << (i * 8));
        }
        return value;
    }

    template <std::size_t N>
    [[nodiscard]] static constexpr std::uint64_t lit(const char (&s)[N]) noexcept {
        static_assert(N >= 2, "literal must not be empty");
        static_assert(N - 1 <= 8, "only supports up to 8 chars");
        return packLiteral(s, static_cast<std::uint8_t>(N - 1));
    }

    template <std::size_t N>
    [[nodiscard]] static inline bool equalsLiteralIgnoreCase(const char* s,
                                                             std::uint8_t len,
                                                             const char (&literal)[N]) noexcept {
        static_assert(N >= 2, "literal must not be empty");
        if (len != N - 1)
            return false;

        for (std::uint8_t i = 0; i < len; ++i) {
            if (asciiLower(s[i]) != literal[i])
                return false;
        }

        return true;
    }

    [[nodiscard]] static inline std::uint64_t loadLowerAsciiUpTo8(const char* s,
                                                                  std::uint8_t len) noexcept {
        std::uint64_t value = 0;
        std::memcpy(&value, s, len);
        value |= lowerMaskForLen(len);
        return value;
    }

    [[nodiscard]] static inline BasenameView findBasename(std::string_view path) noexcept {
        const char* s = path.data();
        std::size_t end = path.size();

        while (end > 0 && isSlash(s[end - 1]))
            --end;
        if (end == 0)
            return {nullptr, 0};

        std::size_t base = end;
        while (base > 0 && !isSlash(s[base - 1]))
            --base;

        return {s + base, end - base};
    }

    [[nodiscard]] static inline ExtensionView findExtension(BasenameView base) noexcept {
        if (base.ptr == nullptr || base.len == 0)
            return {nullptr, 0};

        const char* s = base.ptr;
        const std::size_t end = base.len;

        for (std::size_t i = end; i > 0; --i) {
            if (s[i - 1] != '.')
                continue;

            const std::size_t dot = i - 1;
            if (dot == 0)
                return {nullptr, 0};

            const std::size_t extLen = end - (dot + 1);
            if (extLen == 0 || extLen > 10)
                return {nullptr, 0};

            return {s + dot + 1, static_cast<std::uint8_t>(extLen)};
        }

        return {nullptr, 0};
    }

    [[nodiscard]] static inline FileCategory categorizeCaseSensitive(BasenameView base) noexcept {
        if (base.ptr == nullptr || base.len < 7)
            return FileCategory::Unknown;

        // check if it is lib*.so.*
        const char* s = base.ptr;
        if (s[0] != 'l' || s[1] != 'i' || s[2] != 'b')
            return FileCategory::Unknown;

        for (std::size_t i = 3; i + 3 < base.len; ++i) {
            if (s[i] == '.' && s[i + 1] == 's' && s[i + 2] == 'o' && s[i + 3] == '.')
                return FileCategory::Library;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeResolved(BasenameView base,
                                                                ExtensionView ext) noexcept {
        if (ext.ptr != nullptr) {
            const FileCategory category = categorizeExtension(ext.ptr, ext.len);
            if (category != FileCategory::Unknown)
                return category;
        }

        return categorizeCaseSensitive(base);
    }

    [[nodiscard]] static inline FileCategory categorizeExtension(const char* ext,
                                                                 std::uint8_t len) noexcept {
        if (len == 0 || len > 10)
            return FileCategory::Unknown;

        const char c0 = asciiLower(ext[0]);

        if (len <= 8) {
            const std::uint64_t v = loadLowerAsciiUpTo8(ext, len);

            switch (len) {
            case 1: return categorizeLen1(v, c0);
            case 2: return categorizeLen2(v, c0);
            case 3: return categorizeLen3(v, c0);
            case 4: return categorizeLen4(v, c0);
            case 5: return categorizeLen5(v, c0);
            case 6: return categorizeLen6(v, c0);
            case 7: return categorizeLen7(v, c0);
            case 8: return categorizeLen8(v, c0);
            default: return FileCategory::Unknown;
            }
        }

        switch (len) {
        case 10: return categorizeLen10(ext, len, c0);
        default: return FileCategory::Unknown;
        }
    }

    [[nodiscard]] static inline FileCategory categorizeLen1(std::uint64_t v, char c0) noexcept {
        switch (c0) {
        case 'a':
            if (v == lit("a")) return FileCategory::Library;
            break;
        case 'c':
            if (v == lit("c")) return FileCategory::Source;
            break;
        case 'h':
            if (v == lit("h")) return FileCategory::Source;
            break;
        case 'i':
            if (v == lit("i")) return FileCategory::Source;
            break;
        case 'o':
            if (v == lit("o")) return FileCategory::ObjectGenerated;
            break;
        case 's':
            if (v == lit("s")) return FileCategory::Source;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeLen2(std::uint64_t v, char c0) noexcept {
        switch (c0) {
        case '7':
            if (v == lit("7z")) return FileCategory::Archive;
            break;
        case 'c':
            if (v == lit("cc")) return FileCategory::Source;
            if (v == lit("cp")) return FileCategory::Source;
            if (v == lit("cs")) return FileCategory::Source;
            break;
        case 'd':
            if (v == lit("db")) return FileCategory::Database;
            break;
        case 'g':
            if (v == lit("go")) return FileCategory::Source;
            if (v == lit("gz")) return FileCategory::Compressed;
            break;
        case 'h':
            if (v == lit("hh")) return FileCategory::Source;
            if (v == lit("hp")) return FileCategory::Source;
            break;
        case 'j':
            if (v == lit("js")) return FileCategory::Source;
            break;
        case 'k':
            if (v == lit("kt")) return FileCategory::Source;
            break;
        case 'l':
            if (v == lit("lz")) return FileCategory::Compressed;
            break;
        case 'm':
            if (v == lit("md")) return FileCategory::Document;
            if (v == lit("mm")) return FileCategory::Source;
            break;
        case 'p':
            if (v == lit("pl")) return FileCategory::Source;
            if (v == lit("py")) return FileCategory::Source;
            break;
        case 'r':
            if (v == lit("rb")) return FileCategory::Source;
            if (v == lit("rs")) return FileCategory::Source;
            break;
        case 's':
            if (v == lit("sh")) return FileCategory::Source;
            if (v == lit("so")) return FileCategory::Library;
            break;
        case 't':
            if (v == lit("ts")) return FileCategory::Source;
            break;
        case 'x':
            if (v == lit("xz")) return FileCategory::Compressed;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeLen3(std::uint64_t v, char c0) noexcept {
        switch (c0) {
        case 'a':
            if (v == lit("aac")) return FileCategory::Music;
            if (v == lit("apk")) return FileCategory::Package;
            if (v == lit("avi")) return FileCategory::Video;
            break;
        case 'b':
            if (v == lit("bak")) return FileCategory::BackupTemp;
            if (v == lit("bz2")) return FileCategory::Compressed;
            break;
        case 'c':
            if (v == lit("c++")) return FileCategory::Source;
            if (v == lit("cpp")) return FileCategory::Source;
            if (v == lit("css")) return FileCategory::Source;
            if (v == lit("csv")) return FileCategory::Document;
            if (v == lit("cxx")) return FileCategory::Source;
            break;
        case 'd':
            if (v == lit("db3")) return FileCategory::Database;
            if (v == lit("deb")) return FileCategory::Package;
            if (v == lit("dll")) return FileCategory::Library;
            break;
        case 'e':
            if (v == lit("elf")) return FileCategory::Executable;
            if (v == lit("err")) return FileCategory::Log;
            if (v == lit("exe")) return FileCategory::Executable;
            break;
        case 'f':
            if (v == lit("frm")) return FileCategory::Database;
            if (v == lit("flv")) return FileCategory::Video;
            break;
        case 'g':
            if (v == lit("gif")) return FileCategory::Image;
            break;
        case 'h':
            if (v == lit("hpp")) return FileCategory::Source;
            if (v == lit("hxx")) return FileCategory::Source;
            break;
        case 'i':
            if (v == lit("ibd")) return FileCategory::Database;
            if (v == lit("ico")) return FileCategory::Image;
            if (v == lit("iso")) return FileCategory::DiskImage;
            break;
        case 'j':
            if (v == lit("jpg")) return FileCategory::Image;
            if (v == lit("jsx")) return FileCategory::Source;
            break;
        case 'k':
            if (v == lit("kts")) return FileCategory::Source;
            break;
        case 'l':
            if (v == lit("lz4")) return FileCategory::Compressed;
            if (v == lit("log")) return FileCategory::Log;
            break;
        case 'm':
            if (v == lit("m4a")) return FileCategory::Music;
            if (v == lit("mdb")) return FileCategory::Database;
            if (v == lit("mkv")) return FileCategory::Video;
            if (v == lit("myd")) return FileCategory::Database;
            if (v == lit("myi")) return FileCategory::Database;
            if (v == lit("mov")) return FileCategory::Video;
            if (v == lit("mp3")) return FileCategory::Music;
            if (v == lit("mp4")) return FileCategory::Video;
            break;
        case 'o':
            if (v == lit("obj")) return FileCategory::ObjectGenerated;
            if (v == lit("odp")) return FileCategory::Document;
            if (v == lit("ods")) return FileCategory::Document;
            if (v == lit("odt")) return FileCategory::Document;
            if (v == lit("ogg")) return FileCategory::Music;
            if (v == lit("old")) return FileCategory::BackupTemp;
            if (v == lit("out")) return FileCategory::Log;
            break;
        case 'p':
            if (v == lit("pdf")) return FileCategory::Document;
            if (v == lit("pid")) return FileCategory::Log;
            if (v == lit("png")) return FileCategory::Image;
            break;
        case 'r':
            if (v == lit("rar")) return FileCategory::Archive;
            if (v == lit("rpm")) return FileCategory::Package;
            if (v == lit("rtf")) return FileCategory::Document;
            break;
        case 's':
            if (v == lit("sql")) return FileCategory::Source;
            if (v == lit("swp")) return FileCategory::BackupTemp;
            break;
        case 't':
            if (v == lit("tgz")) return FileCategory::Archive;
            if (v == lit("tmp")) return FileCategory::BackupTemp;
            if (v == lit("tsx")) return FileCategory::Source;
            if (v == lit("txt")) return FileCategory::Document;
            break;
        case 'w':
            if (v == lit("wav")) return FileCategory::Music;
            if (v == lit("wma")) return FileCategory::Music;
            if (v == lit("wmv")) return FileCategory::Video;
            break;
        case 'x':
            if (v == lit("xml")) return FileCategory::Document;
            break;
        case 'y':
            if (v == lit("yml")) return FileCategory::Document;
            break;
        case 'z':
            if (v == lit("zip")) return FileCategory::Archive;
            if (v == lit("zst")) return FileCategory::Compressed;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeLen4(std::uint64_t v, char c0) noexcept {
        switch (c0) {
        case 'd':
            if (v == lit("docx")) return FileCategory::Document;
            break;
        case 'f':
            if (v == lit("file")) return FileCategory::ObjectGenerated;
            if (v == lit("flac")) return FileCategory::Music;
            break;
        case 'h':
            if (v == lit("html")) return FileCategory::Document;
            break;
        case 'j':
            if (v == lit("java")) return FileCategory::Source;
            if (v == lit("jpeg")) return FileCategory::Image;
            if (v == lit("json")) return FileCategory::Document;
            break;
        case 'p':
            if (v == lit("part")) return FileCategory::BackupTemp;
            if (v == lit("pptx")) return FileCategory::Document;
            break;
        case 's':
            if (v == lit("scss")) return FileCategory::Source;
            if (v == lit("snap")) return FileCategory::Package;
            break;
        case 't':
            if (v == lit("tbz2")) return FileCategory::Archive;
            if (v == lit("tiff")) return FileCategory::Image;
            break;
        case 'v':
            if (v == lit("vmdk")) return FileCategory::DiskImage;
            break;
        case 'w':
            if (v == lit("wasm")) return FileCategory::ObjectGenerated;
            if (v == lit("webp")) return FileCategory::Image;
            break;
        case 'x':
            if (v == lit("xlsx")) return FileCategory::Document;
            if (v == lit("xlsm")) return FileCategory::Document;
            break;
        case 'y':
            if (v == lit("yaml")) return FileCategory::Document;
            break;
        case 'z':
            if (v == lit("zstd")) return FileCategory::Compressed;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeLen5(std::uint64_t v, char c0) noexcept {
        switch (c0) {
        case 'c':
            if (v == lit("class")) return FileCategory::ObjectGenerated;
            break;
        case 'd':
            if (v == lit("dylib")) return FileCategory::Library;
            break;
        case 'q':
            if (v == lit("qcow2")) return FileCategory::DiskImage;
            break;
        case 'w':
            if (v == lit("woff2")) return FileCategory::ObjectGenerated;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeLen6(std::uint64_t v, char c0) noexcept {
        switch (c0) {
        case 's':
            if (v == lit("sqlite")) return FileCategory::Database;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeLen7(std::uint64_t v, char c0) noexcept {
        switch (c0) {
        case 'f':
            if (v == lit("flatpak")) return FileCategory::Package;
            break;
        case 's':
            if (v == lit("sqlite3")) return FileCategory::Database;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeLen8(std::uint64_t v, char c0) noexcept {
        switch (c0) {
        case 'a':
            if (v == lit("appimage")) return FileCategory::Package;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }

    [[nodiscard]] static inline FileCategory categorizeLen10(const char* ext,
                                                             std::uint8_t len,
                                                             char c0) noexcept {
        switch (c0) {
        case 'c':
            if (equalsLiteralIgnoreCase(ext, len, "commitmeta"))
                return FileCategory::ObjectGenerated;
            if (equalsLiteralIgnoreCase(ext, len, "crdownload"))
                return FileCategory::BackupTemp;
            break;
        default:
            break;
        }

        return FileCategory::Unknown;
    }
};

class FileCategoryCounter final {
public:
    struct Item {
        FileCategory category = FileCategory::Unknown;
        std::uint64_t count = 0;
        std::uint64_t totalSize = 0;
    };

    using Items = std::array<Item, FileCategorizer::kCategoryCount>;

    explicit FileCategoryCounter(const DirEntryStore& entryStore)
        : entryStore_(entryStore) {
        reset();
    }

    void reset() noexcept {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            items_[i].category = static_cast<FileCategory>(i);
            items_[i].count = 0;
            items_[i].totalSize = 0;
        }
    }

    void countTree(EntryRef root) {
        reset();
        if (!root.valid())
            return;

        const DirEntry& rootEntry = entryStore_[root];
        if (rootEntry.isFile()) {
            countFile(rootEntry);
            return;
        }

        std::vector<EntryRef> stack;
        stack.reserve(256);
        stack.push_back(root);

        EntryRef child = rootEntry.firstChild;
        bool dirPopped = false;
        while (!stack.empty() && child.valid()) {
            const DirEntry& entry = entryStore_[child];
            if (entry.isDir() && entry.childCount > 0 && !dirPopped) {
                stack.push_back(child);
                child = entry.firstChild;
                continue;
            }

            if (entry.isFile())
                countFile(entry);

            dirPopped = false;
            child = entry.nextSibling;
            if (!child.valid()) {
                child = stack.back();
                stack.pop_back();
                dirPopped = true;
            }
        }
    }

    [[nodiscard]] const Items& items() const noexcept { return items_; }

private:
    void countFile(const DirEntry& entry) noexcept {
        Item& item = items_[FileCategorizer::categoryIndex(entry.fileCategory)];
        ++item.count;
        item.totalSize += layoutSizeOf(entry);
    }

    const DirEntryStore& entryStore_;
    Items items_{};
};

} // namespace ldirstat
