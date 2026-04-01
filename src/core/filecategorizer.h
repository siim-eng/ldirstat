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
    Cache,
    Library,
    Log,
    Music,
    ObjectGenerated,
    Source,
    Video,
    Executable,
};

enum class FileType : std::uint16_t {
    Unknown = 0,
    Executable,
    Cache,
    Ext7z,
    ExtA,
    ExtAac,
    ExtApk,
    ExtAppImage,
    ExtAvi,
    ExtBak,
    ExtBz2,
    ExtC,
    ExtCPlusPlus,
    ExtCc,
    ExtClass,
    ExtCommitMeta,
    ExtConf,
    ExtCp,
    ExtCpp,
    ExtCrdownload,
    ExtCs,
    ExtCss,
    ExtCsv,
    ExtCxx,
    ExtDb,
    ExtDb3,
    ExtDeb,
    ExtDesktop,
    ExtDll,
    ExtDocx,
    ExtDylib,
    ExtErr,
    ExtFile,
    ExtFlac,
    ExtFlatpak,
    ExtFlv,
    ExtFrm,
    ExtGif,
    ExtGo,
    ExtGz,
    ExtH,
    ExtHh,
    ExtHp,
    ExtHpp,
    ExtHtml,
    ExtHxx,
    ExtI,
    ExtIbd,
    ExtIco,
    ExtIni,
    ExtIso,
    ExtJava,
    ExtJournal,
    ExtJournalTilde,
    ExtJpeg,
    ExtJpg,
    ExtJs,
    ExtJson,
    ExtJsx,
    ExtKo,
    ExtKt,
    ExtKts,
    ExtLog,
    ExtLz,
    ExtLz4,
    ExtM4a,
    ExtMd,
    ExtMdb,
    ExtMkv,
    ExtMm,
    ExtMov,
    ExtMp3,
    ExtMp4,
    ExtMyd,
    ExtMyi,
    ExtO,
    ExtObj,
    ExtOdp,
    ExtOds,
    ExtOdt,
    ExtOgg,
    ExtOld,
    ExtOut,
    ExtPart,
    ExtPdf,
    ExtPid,
    ExtPl,
    ExtPng,
    ExtPptx,
    ExtPyc,
    ExtPy,
    ExtQcow2,
    ExtRar,
    ExtRb,
    ExtRpm,
    ExtRs,
    ExtRtf,
    ExtS,
    ExtScss,
    ExtService,
    ExtSh,
    ExtSnap,
    ExtSo,
    ExtSql,
    ExtSqlite,
    ExtSqlite3,
    ExtSvg,
    ExtSwp,
    ExtTar,
    ExtTbz2,
    ExtTgz,
    ExtTiff,
    ExtTmp,
    ExtToml,
    ExtTs,
    ExtTsx,
    ExtTxt,
    ExtUi,
    ExtVmdk,
    ExtWasm,
    ExtWav,
    ExtWebm,
    ExtWebp,
    ExtWma,
    ExtWmv,
    ExtWoff2,
    ExtXlsx,
    ExtXlsm,
    ExtXml,
    ExtXz,
    ExtYaml,
    ExtYml,
    ExtZip,
    ExtZst,
    ExtZstd,
    VersionedSharedLibrary,
};

class FileCategorizer final {
public:
    struct FileTypeSpec {
        FileType type;
        std::string_view extension;
        FileCategory category;
    };

    static constexpr std::size_t kCategoryCount = static_cast<std::size_t>(FileCategory::Executable) + 1;
    static constexpr std::size_t kTypeCount = static_cast<std::size_t>(FileType::VersionedSharedLibrary) + 1;

    static constexpr std::size_t categoryIndex(FileCategory category) { return static_cast<std::size_t>(category); }
    static constexpr std::size_t typeIndex(FileType type) { return static_cast<std::size_t>(type); }

    static constexpr const char *categoryName(FileCategory category) {
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
        case FileCategory::Cache: return "cache";
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

    static constexpr const char *displayCategoryName(FileCategory category) {
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
        case FileCategory::Cache: return "Cache";
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

    static inline FileCategory categoryForType(FileType type) {
        const std::size_t index = typeIndex(type);
        if (index >= kTypeCategories_.size()) return FileCategory::Unknown;
        return kTypeCategories_[index];
    }

    static inline FileType categorize(std::string_view utf8Path) {
        const BasenameView base = findBasename(utf8Path);
        const ExtensionView ext = findExtension(base);
        return categorizeResolved(base, ext);
    }

    struct Result {
        FileType type;
        const char *extensionPtr;
        std::size_t extensionLen;
    };

    static inline Result categorizeWithExtension(std::string_view utf8Path) {
        const BasenameView base = findBasename(utf8Path);
        const ExtensionView ext = findExtension(base);
        return {categorizeResolved(base, ext), ext.ptr, ext.len};
    }

private:
    struct BasenameView {
        const char *ptr;
        std::size_t len;
    };

    struct ExtensionView {
        const char *ptr;
        std::size_t len;
    };

    // Metadata source of truth for FileType -> FileCategory and extension display.
    // The hot-path extension matcher stays hand-written below to keep scanning fast.
    static constexpr auto kExtensionTypes_ = std::to_array<FileTypeSpec>({
        {FileType::Ext7z, "7z", FileCategory::Archive},
        {FileType::ExtA, "a", FileCategory::Library},
        {FileType::ExtAac, "aac", FileCategory::Music},
        {FileType::ExtApk, "apk", FileCategory::Package},
        {FileType::ExtAppImage, "appimage", FileCategory::Package},
        {FileType::ExtAvi, "avi", FileCategory::Video},
        {FileType::ExtBak, "bak", FileCategory::BackupTemp},
        {FileType::ExtBz2, "bz2", FileCategory::Compressed},
        {FileType::ExtC, "c", FileCategory::Source},
        {FileType::ExtCPlusPlus, "c++", FileCategory::Source},
        {FileType::ExtCc, "cc", FileCategory::Source},
        {FileType::ExtClass, "class", FileCategory::ObjectGenerated},
        {FileType::ExtCommitMeta, "commitmeta", FileCategory::ObjectGenerated},
        {FileType::ExtConf, "conf", FileCategory::Document},
        {FileType::ExtCp, "cp", FileCategory::Source},
        {FileType::ExtCpp, "cpp", FileCategory::Source},
        {FileType::ExtCrdownload, "crdownload", FileCategory::BackupTemp},
        {FileType::ExtCs, "cs", FileCategory::Source},
        {FileType::ExtCss, "css", FileCategory::Source},
        {FileType::ExtCsv, "csv", FileCategory::Document},
        {FileType::ExtCxx, "cxx", FileCategory::Source},
        {FileType::ExtDb, "db", FileCategory::Database},
        {FileType::ExtDb3, "db3", FileCategory::Database},
        {FileType::ExtDeb, "deb", FileCategory::Package},
        {FileType::ExtDesktop, "desktop", FileCategory::Document},
        {FileType::ExtDll, "dll", FileCategory::Library},
        {FileType::ExtDocx, "docx", FileCategory::Document},
        {FileType::ExtDylib, "dylib", FileCategory::Library},
        {FileType::Executable, "elf", FileCategory::Executable},
        {FileType::ExtErr, "err", FileCategory::Log},
        {FileType::Executable, "exe", FileCategory::Executable},
        {FileType::ExtFile, "file", FileCategory::ObjectGenerated},
        {FileType::ExtFlac, "flac", FileCategory::Music},
        {FileType::ExtFlatpak, "flatpak", FileCategory::Package},
        {FileType::ExtFlv, "flv", FileCategory::Video},
        {FileType::ExtFrm, "frm", FileCategory::Database},
        {FileType::ExtGif, "gif", FileCategory::Image},
        {FileType::ExtGo, "go", FileCategory::Source},
        {FileType::ExtGz, "gz", FileCategory::Compressed},
        {FileType::ExtH, "h", FileCategory::Source},
        {FileType::ExtHh, "hh", FileCategory::Source},
        {FileType::ExtHp, "hp", FileCategory::Source},
        {FileType::ExtHpp, "hpp", FileCategory::Source},
        {FileType::ExtHtml, "html", FileCategory::Document},
        {FileType::ExtHxx, "hxx", FileCategory::Source},
        {FileType::ExtI, "i", FileCategory::Source},
        {FileType::ExtIbd, "ibd", FileCategory::Database},
        {FileType::ExtIco, "ico", FileCategory::Image},
        {FileType::ExtIni, "ini", FileCategory::Document},
        {FileType::ExtIso, "iso", FileCategory::DiskImage},
        {FileType::ExtJava, "java", FileCategory::Source},
        {FileType::ExtJournal, "journal", FileCategory::Log},
        {FileType::ExtJournalTilde, "journal~", FileCategory::Log},
        {FileType::ExtJpeg, "jpeg", FileCategory::Image},
        {FileType::ExtJpg, "jpg", FileCategory::Image},
        {FileType::ExtJs, "js", FileCategory::Source},
        {FileType::ExtJson, "json", FileCategory::Document},
        {FileType::ExtJsx, "jsx", FileCategory::Source},
        {FileType::ExtKo, "ko", FileCategory::ObjectGenerated},
        {FileType::ExtKt, "kt", FileCategory::Source},
        {FileType::ExtKts, "kts", FileCategory::Source},
        {FileType::ExtLog, "log", FileCategory::Log},
        {FileType::ExtLz, "lz", FileCategory::Compressed},
        {FileType::ExtLz4, "lz4", FileCategory::Compressed},
        {FileType::ExtM4a, "m4a", FileCategory::Music},
        {FileType::ExtMd, "md", FileCategory::Document},
        {FileType::ExtMdb, "mdb", FileCategory::Database},
        {FileType::ExtMkv, "mkv", FileCategory::Video},
        {FileType::ExtMm, "mm", FileCategory::Source},
        {FileType::ExtMov, "mov", FileCategory::Video},
        {FileType::ExtMp3, "mp3", FileCategory::Music},
        {FileType::ExtMp4, "mp4", FileCategory::Video},
        {FileType::ExtMyd, "myd", FileCategory::Database},
        {FileType::ExtMyi, "myi", FileCategory::Database},
        {FileType::ExtO, "o", FileCategory::ObjectGenerated},
        {FileType::ExtObj, "obj", FileCategory::ObjectGenerated},
        {FileType::ExtOdp, "odp", FileCategory::Document},
        {FileType::ExtOds, "ods", FileCategory::Document},
        {FileType::ExtOdt, "odt", FileCategory::Document},
        {FileType::ExtOgg, "ogg", FileCategory::Music},
        {FileType::ExtOld, "old", FileCategory::BackupTemp},
        {FileType::ExtOut, "out", FileCategory::Log},
        {FileType::ExtPart, "part", FileCategory::BackupTemp},
        {FileType::ExtPdf, "pdf", FileCategory::Document},
        {FileType::ExtPid, "pid", FileCategory::Log},
        {FileType::ExtPl, "pl", FileCategory::Source},
        {FileType::ExtPng, "png", FileCategory::Image},
        {FileType::ExtPptx, "pptx", FileCategory::Document},
        {FileType::ExtPyc, "pyc", FileCategory::ObjectGenerated},
        {FileType::ExtPy, "py", FileCategory::Source},
        {FileType::ExtQcow2, "qcow2", FileCategory::DiskImage},
        {FileType::ExtRar, "rar", FileCategory::Archive},
        {FileType::ExtRb, "rb", FileCategory::Source},
        {FileType::ExtRpm, "rpm", FileCategory::Package},
        {FileType::ExtRs, "rs", FileCategory::Source},
        {FileType::ExtRtf, "rtf", FileCategory::Document},
        {FileType::ExtS, "s", FileCategory::Source},
        {FileType::ExtScss, "scss", FileCategory::Source},
        {FileType::ExtService, "service", FileCategory::Document},
        {FileType::ExtSh, "sh", FileCategory::Source},
        {FileType::ExtSnap, "snap", FileCategory::Package},
        {FileType::ExtSo, "so", FileCategory::Library},
        {FileType::ExtSql, "sql", FileCategory::Source},
        {FileType::ExtSqlite, "sqlite", FileCategory::Database},
        {FileType::ExtSqlite3, "sqlite3", FileCategory::Database},
        {FileType::ExtSvg, "svg", FileCategory::Image},
        {FileType::ExtSwp, "swp", FileCategory::BackupTemp},
        {FileType::ExtTar, "tar", FileCategory::Archive},
        {FileType::ExtTbz2, "tbz2", FileCategory::Archive},
        {FileType::ExtTgz, "tgz", FileCategory::Archive},
        {FileType::ExtTiff, "tiff", FileCategory::Image},
        {FileType::ExtTmp, "tmp", FileCategory::BackupTemp},
        {FileType::ExtToml, "toml", FileCategory::Document},
        {FileType::ExtTs, "ts", FileCategory::Source},
        {FileType::ExtTsx, "tsx", FileCategory::Source},
        {FileType::ExtTxt, "txt", FileCategory::Document},
        {FileType::ExtUi, "ui", FileCategory::Source},
        {FileType::ExtVmdk, "vmdk", FileCategory::DiskImage},
        {FileType::ExtWasm, "wasm", FileCategory::ObjectGenerated},
        {FileType::ExtWav, "wav", FileCategory::Music},
        {FileType::ExtWebm, "webm", FileCategory::Video},
        {FileType::ExtWebp, "webp", FileCategory::Image},
        {FileType::ExtWma, "wma", FileCategory::Music},
        {FileType::ExtWmv, "wmv", FileCategory::Video},
        {FileType::ExtWoff2, "woff2", FileCategory::ObjectGenerated},
        {FileType::ExtXlsx, "xlsx", FileCategory::Document},
        {FileType::ExtXlsm, "xlsm", FileCategory::Document},
        {FileType::ExtXml, "xml", FileCategory::Document},
        {FileType::ExtXz, "xz", FileCategory::Compressed},
        {FileType::ExtYaml, "yaml", FileCategory::Document},
        {FileType::ExtYml, "yml", FileCategory::Document},
        {FileType::ExtZip, "zip", FileCategory::Archive},
        {FileType::ExtZst, "zst", FileCategory::Compressed},
        {FileType::ExtZstd, "zstd", FileCategory::Compressed},
    });

    static constexpr std::array<FileCategory, kTypeCount> buildTypeCategories() {
        std::array<FileCategory, kTypeCount> categories{};
        categories[typeIndex(FileType::Executable)] = FileCategory::Executable;
        categories[typeIndex(FileType::Cache)] = FileCategory::Cache;
        categories[typeIndex(FileType::VersionedSharedLibrary)] = FileCategory::Library;

        for (const FileTypeSpec &spec : kExtensionTypes_)
            categories[typeIndex(spec.type)] = spec.category;

        return categories;
    }

    static const std::array<FileCategory, kTypeCount> kTypeCategories_;

    static inline bool isSlash(char c) { return c == '/'; }

    static inline char asciiLower(char c) {
        const unsigned char uc = static_cast<unsigned char>(c);
        return (uc >= 'A' && uc <= 'Z') ? static_cast<char>(uc | 0x20u) : c;
    }

    static constexpr std::uint64_t lowerMaskForLen(std::size_t len) {
        std::uint64_t mask = 0;
        for (std::size_t i = 0; i < len; ++i)
            mask |= (std::uint64_t{0x20} << (i * 8));
        return mask;
    }

    static constexpr std::uint64_t packLiteral(const char *s, std::size_t len) {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < len; ++i) {
            auto c = static_cast<unsigned char>(s[i]);
            if (c >= 'A' && c <= 'Z') c |= 0x20;
            value |= static_cast<std::uint64_t>(c) << (i * 8);
        }
        return value;
    }

    template<std::size_t N> static constexpr std::uint64_t lit(const char (&s)[N]) {
        static_assert(N >= 2, "literal must not be empty");
        static_assert(N - 1 <= 8, "only supports up to 8 chars");
        return packLiteral(s, N - 1);
    }

    template<std::size_t N>
    static inline bool equalsLiteralIgnoreCase(const char *s, std::size_t len, const char (&literal)[N]) {
        static_assert(N >= 2, "literal must not be empty");
        if (len != N - 1) return false;

        for (std::size_t i = 0; i < len; ++i) {
            if (asciiLower(s[i]) != literal[i]) return false;
        }

        return true;
    }

    static inline std::uint64_t loadLowerAsciiUpTo8(const char *s, std::size_t len) {
        std::uint64_t value = 0;
        std::memcpy(&value, s, len);
        value |= lowerMaskForLen(len);
        return value;
    }

    static inline BasenameView findBasename(std::string_view path) {
        const char *s = path.data();
        std::size_t end = path.size();

        while (end > 0 && isSlash(s[end - 1]))
            --end;
        if (end == 0) return {nullptr, 0};

        std::size_t base = end;
        while (base > 0 && !isSlash(s[base - 1]))
            --base;

        return {s + base, end - base};
    }

    static inline ExtensionView findExtension(BasenameView base) {
        if (base.ptr == nullptr || base.len == 0) return {nullptr, 0};

        const char *s = base.ptr;
        const std::size_t end = base.len;

        for (std::size_t i = end; i > 0; --i) {
            if (s[i - 1] != '.') continue;

            const std::size_t dot = i - 1;
            if (dot == 0) return {nullptr, 0};

            const std::size_t extLen = end - (dot + 1);
            if (extLen == 0 || extLen > 10) return {nullptr, 0};

            return {s + dot + 1, extLen};
        }

        return {nullptr, 0};
    }

    static inline FileType categorizeCaseSensitive(BasenameView base) {
        if (base.ptr == nullptr || base.len < 7) return FileType::Unknown;

        const char *s = base.ptr;
        if (s[0] != 'l' || s[1] != 'i' || s[2] != 'b') return FileType::Unknown;

        for (std::size_t i = 3; i + 3 < base.len; ++i) {
            if (s[i] == '.' && s[i + 1] == 's' && s[i + 2] == 'o' && s[i + 3] == '.') return FileType::VersionedSharedLibrary;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeResolved(BasenameView base, ExtensionView ext) {
        if (ext.ptr != nullptr) {
            const FileType type = categorizeExtension(ext.ptr, ext.len);
            if (type != FileType::Unknown) return type;
        }

        return categorizeCaseSensitive(base);
    }

    static inline FileType categorizeExtension(const char *ext, std::size_t len) {
        if (len == 0 || len > 10) return FileType::Unknown;

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
            default: return FileType::Unknown;
            }
        }

        switch (len) {
        case 10: return categorizeLen10(ext, len, c0);
        default: return FileType::Unknown;
        }
    }

    static inline FileType categorizeLen1(std::uint64_t v, char c0) {
        switch (c0) {
        case 'a':
            if (v == lit("a")) return FileType::ExtA;
            break;
        case 'c':
            if (v == lit("c")) return FileType::ExtC;
            break;
        case 'h':
            if (v == lit("h")) return FileType::ExtH;
            break;
        case 'i':
            if (v == lit("i")) return FileType::ExtI;
            break;
        case 'o':
            if (v == lit("o")) return FileType::ExtO;
            break;
        case 's':
            if (v == lit("s")) return FileType::ExtS;
            break;
        default: break;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeLen2(std::uint64_t v, char c0) {
        switch (c0) {
        case '7':
            if (v == lit("7z")) return FileType::Ext7z;
            break;
        case 'c':
            if (v == lit("cc")) return FileType::ExtCc;
            if (v == lit("cp")) return FileType::ExtCp;
            if (v == lit("cs")) return FileType::ExtCs;
            break;
        case 'd':
            if (v == lit("db")) return FileType::ExtDb;
            break;
        case 'g':
            if (v == lit("go")) return FileType::ExtGo;
            if (v == lit("gz")) return FileType::ExtGz;
            break;
        case 'h':
            if (v == lit("hh")) return FileType::ExtHh;
            if (v == lit("hp")) return FileType::ExtHp;
            break;
        case 'j':
            if (v == lit("js")) return FileType::ExtJs;
            break;
        case 'k':
            if (v == lit("ko")) return FileType::ExtKo;
            if (v == lit("kt")) return FileType::ExtKt;
            break;
        case 'l':
            if (v == lit("lz")) return FileType::ExtLz;
            break;
        case 'm':
            if (v == lit("md")) return FileType::ExtMd;
            if (v == lit("mm")) return FileType::ExtMm;
            break;
        case 'p':
            if (v == lit("pl")) return FileType::ExtPl;
            if (v == lit("py")) return FileType::ExtPy;
            break;
        case 'r':
            if (v == lit("rb")) return FileType::ExtRb;
            if (v == lit("rs")) return FileType::ExtRs;
            break;
        case 's':
            if (v == lit("sh")) return FileType::ExtSh;
            if (v == lit("so")) return FileType::ExtSo;
            break;
        case 't':
            if (v == lit("ts")) return FileType::ExtTs;
            break;
        case 'u':
            if (v == lit("ui")) return FileType::ExtUi;
            break;
        case 'x':
            if (v == lit("xz")) return FileType::ExtXz;
            break;
        default: break;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeLen3(std::uint64_t v, char c0) {
        switch (c0) {
        case 'a':
            if (v == lit("aac")) return FileType::ExtAac;
            if (v == lit("apk")) return FileType::ExtApk;
            if (v == lit("avi")) return FileType::ExtAvi;
            break;
        case 'b':
            if (v == lit("bak")) return FileType::ExtBak;
            if (v == lit("bz2")) return FileType::ExtBz2;
            break;
        case 'c':
            if (v == lit("c++")) return FileType::ExtCPlusPlus;
            if (v == lit("cpp")) return FileType::ExtCpp;
            if (v == lit("css")) return FileType::ExtCss;
            if (v == lit("csv")) return FileType::ExtCsv;
            if (v == lit("cxx")) return FileType::ExtCxx;
            break;
        case 'd':
            if (v == lit("db3")) return FileType::ExtDb3;
            if (v == lit("deb")) return FileType::ExtDeb;
            if (v == lit("dll")) return FileType::ExtDll;
            break;
        case 'e':
            if (v == lit("elf")) return FileType::Executable;
            if (v == lit("err")) return FileType::ExtErr;
            if (v == lit("exe")) return FileType::Executable;
            break;
        case 'f':
            if (v == lit("frm")) return FileType::ExtFrm;
            if (v == lit("flv")) return FileType::ExtFlv;
            break;
        case 'g':
            if (v == lit("gif")) return FileType::ExtGif;
            break;
        case 'h':
            if (v == lit("hpp")) return FileType::ExtHpp;
            if (v == lit("hxx")) return FileType::ExtHxx;
            break;
        case 'i':
            if (v == lit("ibd")) return FileType::ExtIbd;
            if (v == lit("ico")) return FileType::ExtIco;
            if (v == lit("ini")) return FileType::ExtIni;
            if (v == lit("iso")) return FileType::ExtIso;
            break;
        case 'j':
            if (v == lit("jpg")) return FileType::ExtJpg;
            if (v == lit("jsx")) return FileType::ExtJsx;
            break;
        case 'k':
            if (v == lit("kts")) return FileType::ExtKts;
            break;
        case 'l':
            if (v == lit("lz4")) return FileType::ExtLz4;
            if (v == lit("log")) return FileType::ExtLog;
            break;
        case 'm':
            if (v == lit("m4a")) return FileType::ExtM4a;
            if (v == lit("mdb")) return FileType::ExtMdb;
            if (v == lit("mkv")) return FileType::ExtMkv;
            if (v == lit("mov")) return FileType::ExtMov;
            if (v == lit("mp3")) return FileType::ExtMp3;
            if (v == lit("mp4")) return FileType::ExtMp4;
            if (v == lit("myd")) return FileType::ExtMyd;
            if (v == lit("myi")) return FileType::ExtMyi;
            break;
        case 'o':
            if (v == lit("obj")) return FileType::ExtObj;
            if (v == lit("odp")) return FileType::ExtOdp;
            if (v == lit("ods")) return FileType::ExtOds;
            if (v == lit("odt")) return FileType::ExtOdt;
            if (v == lit("ogg")) return FileType::ExtOgg;
            if (v == lit("old")) return FileType::ExtOld;
            if (v == lit("out")) return FileType::ExtOut;
            break;
        case 'p':
            if (v == lit("pdf")) return FileType::ExtPdf;
            if (v == lit("pid")) return FileType::ExtPid;
            if (v == lit("png")) return FileType::ExtPng;
            if (v == lit("pyc")) return FileType::ExtPyc;
            break;
        case 'r':
            if (v == lit("rar")) return FileType::ExtRar;
            if (v == lit("rpm")) return FileType::ExtRpm;
            if (v == lit("rtf")) return FileType::ExtRtf;
            break;
        case 's':
            if (v == lit("sql")) return FileType::ExtSql;
            if (v == lit("svg")) return FileType::ExtSvg;
            if (v == lit("swp")) return FileType::ExtSwp;
            break;
        case 't':
            if (v == lit("tar")) return FileType::ExtTar;
            if (v == lit("tgz")) return FileType::ExtTgz;
            if (v == lit("tmp")) return FileType::ExtTmp;
            if (v == lit("tsx")) return FileType::ExtTsx;
            if (v == lit("txt")) return FileType::ExtTxt;
            break;
        case 'w':
            if (v == lit("wav")) return FileType::ExtWav;
            if (v == lit("wma")) return FileType::ExtWma;
            if (v == lit("wmv")) return FileType::ExtWmv;
            break;
        case 'x':
            if (v == lit("xml")) return FileType::ExtXml;
            break;
        case 'y':
            if (v == lit("yml")) return FileType::ExtYml;
            break;
        case 'z':
            if (v == lit("zip")) return FileType::ExtZip;
            if (v == lit("zst")) return FileType::ExtZst;
            break;
        default: break;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeLen4(std::uint64_t v, char c0) {
        switch (c0) {
        case 'c':
            if (v == lit("conf")) return FileType::ExtConf;
            break;
        case 'd':
            if (v == lit("docx")) return FileType::ExtDocx;
            break;
        case 'f':
            if (v == lit("file")) return FileType::ExtFile;
            if (v == lit("flac")) return FileType::ExtFlac;
            break;
        case 'h':
            if (v == lit("html")) return FileType::ExtHtml;
            break;
        case 'j':
            if (v == lit("java")) return FileType::ExtJava;
            if (v == lit("jpeg")) return FileType::ExtJpeg;
            if (v == lit("json")) return FileType::ExtJson;
            break;
        case 'p':
            if (v == lit("part")) return FileType::ExtPart;
            if (v == lit("pptx")) return FileType::ExtPptx;
            break;
        case 's':
            if (v == lit("scss")) return FileType::ExtScss;
            if (v == lit("snap")) return FileType::ExtSnap;
            break;
        case 't':
            if (v == lit("tbz2")) return FileType::ExtTbz2;
            if (v == lit("tiff")) return FileType::ExtTiff;
            if (v == lit("toml")) return FileType::ExtToml;
            break;
        case 'v':
            if (v == lit("vmdk")) return FileType::ExtVmdk;
            break;
        case 'w':
            if (v == lit("wasm")) return FileType::ExtWasm;
            if (v == lit("webm")) return FileType::ExtWebm;
            if (v == lit("webp")) return FileType::ExtWebp;
            break;
        case 'x':
            if (v == lit("xlsx")) return FileType::ExtXlsx;
            if (v == lit("xlsm")) return FileType::ExtXlsm;
            break;
        case 'y':
            if (v == lit("yaml")) return FileType::ExtYaml;
            break;
        case 'z':
            if (v == lit("zstd")) return FileType::ExtZstd;
            break;
        default: break;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeLen5(std::uint64_t v, char c0) {
        switch (c0) {
        case 'c':
            if (v == lit("class")) return FileType::ExtClass;
            break;
        case 'd':
            if (v == lit("dylib")) return FileType::ExtDylib;
            break;
        case 'q':
            if (v == lit("qcow2")) return FileType::ExtQcow2;
            break;
        case 'w':
            if (v == lit("woff2")) return FileType::ExtWoff2;
            break;
        default: break;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeLen6(std::uint64_t v, char c0) {
        switch (c0) {
        case 's':
            if (v == lit("sqlite")) return FileType::ExtSqlite;
            break;
        default: break;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeLen7(std::uint64_t v, char c0) {
        switch (c0) {
        case 'd':
            if (v == lit("desktop")) return FileType::ExtDesktop;
            break;
        case 'f':
            if (v == lit("flatpak")) return FileType::ExtFlatpak;
            break;
        case 'j':
            if (v == lit("journal")) return FileType::ExtJournal;
            break;
        case 's':
            if (v == lit("service")) return FileType::ExtService;
            if (v == lit("sqlite3")) return FileType::ExtSqlite3;
            break;
        default: break;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeLen8(std::uint64_t v, char c0) {
        switch (c0) {
        case 'a':
            if (v == lit("appimage")) return FileType::ExtAppImage;
            break;
        case 'j':
            if (v == lit("journal~")) return FileType::ExtJournalTilde;
            break;
        default: break;
        }

        return FileType::Unknown;
    }

    static inline FileType categorizeLen10(const char *ext, std::size_t len, char c0) {
        switch (c0) {
        case 'c':
            if (equalsLiteralIgnoreCase(ext, len, "commitmeta")) return FileType::ExtCommitMeta;
            if (equalsLiteralIgnoreCase(ext, len, "crdownload")) return FileType::ExtCrdownload;
            break;
        default: break;
        }

        return FileType::Unknown;
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

    explicit FileCategoryCounter(const DirEntryStore &entryStore)
        : entryStore_(entryStore) {
        reset();
    }

    void reset() {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            items_[i].category = static_cast<FileCategory>(i);
            items_[i].count = 0;
            items_[i].totalSize = 0;
        }
    }

    void countTree(EntryRef root) {
        reset();
        if (!root.valid()) return;

        const DirEntry &rootEntry = entryStore_[root];
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
            const DirEntry &entry = entryStore_[child];
            if (entry.isDir() && entry.childCount > 0 && !dirPopped) {
                stack.push_back(child);
                child = entry.firstChild;
                continue;
            }

            if (entry.isFile()) countFile(entry);

            dirPopped = false;
            child = entry.nextSibling;
            if (!child.valid()) {
                child = stack.back();
                stack.pop_back();
                dirPopped = true;
            }
        }
    }

    const Items &items() const { return items_; }

private:
    void countFile(const DirEntry &entry) {
        Item &item = items_[FileCategorizer::categoryIndex(FileCategorizer::categoryForType(entry.fileType))];
        ++item.count;
        item.totalSize += layoutSizeOf(entry);
    }

    const DirEntryStore &entryStore_;
    Items items_{};
};

inline const std::array<ldirstat::FileCategory, ldirstat::FileCategorizer::kTypeCount> ldirstat::FileCategorizer::kTypeCategories_ =
    ldirstat::FileCategorizer::buildTypeCategories();

} // namespace ldirstat
