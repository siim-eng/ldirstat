#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filecategorizer.h"
#include "scanner.h"

namespace {

using ldirstat::FileCategorizer;
using ldirstat::FileCategory;
using ldirstat::FileType;
namespace fs = std::filesystem;

struct TypeCase {
    std::string_view path;
    FileType expectedType;
    FileCategory expectedCategory;
};

struct TempDir {
    fs::path path;

    TempDir() {
        std::string templ = (fs::temp_directory_path() / "ldirstat-scanner-XXXXXX").string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char* created = ::mkdtemp(buffer.data());
        REQUIRE(created != nullptr);
        path = created;
    }

    ~TempDir() {
        if (path.empty())
            return;
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void writeFileWithMode(const fs::path& path, mode_t mode) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    REQUIRE(fd >= 0);
    const char payload[] = "x";
    REQUIRE(::write(fd, payload, sizeof(payload) - 1) == static_cast<ssize_t>(sizeof(payload) - 1));
    REQUIRE(::fchmod(fd, mode) == 0);
    REQUIRE(::close(fd) == 0);
}

ldirstat::EntryRef findChildByName(const ldirstat::DirEntryStore& entries,
                                   const ldirstat::NameStore& names,
                                   ldirstat::EntryRef dirRef,
                                   std::string_view name) {
    ldirstat::EntryRef child = entries[dirRef].firstChild;
    while (child.valid()) {
        if (names.get(entries[child].name) == name)
            return child;
        child = entries[child].nextSibling;
    }
    return ldirstat::kNoEntry;
}

ldirstat::EntryRef findDescendantByPath(const ldirstat::DirEntryStore& entries,
                                        const ldirstat::NameStore& names,
                                        ldirstat::EntryRef rootRef,
                                        std::initializer_list<std::string_view> path) {
    ldirstat::EntryRef current = rootRef;
    for (const std::string_view segment : path) {
        current = findChildByName(entries, names, current, segment);
        if (!current.valid()) return ldirstat::kNoEntry;
    }
    return current;
}

TEST_CASE("categorizes supported extensions") {
    constexpr TypeCase cases[] = {
#define TYPE_CASE(path, type, category) TypeCase{path, FileType::type, FileCategory::category}
        TYPE_CASE("archive.zip", ExtZip, Archive),
        TYPE_CASE("archive.7Z", Ext7z, Archive),
        TYPE_CASE("bundle.rar", ExtRar, Archive),
        TYPE_CASE("bundle.tgz", ExtTgz, Archive),
        TYPE_CASE("bundle.tbz2", ExtTbz2, Archive),
        TYPE_CASE("bundle.gz", ExtGz, Compressed),
        TYPE_CASE("backup.lz", ExtLz, Compressed),
        TYPE_CASE("backup.xz", ExtXz, Compressed),
        TYPE_CASE("backup.zst", ExtZst, Compressed),
        TYPE_CASE("backup.zstd", ExtZstd, Compressed),
        TYPE_CASE("cache.lz4", ExtLz4, Compressed),
        TYPE_CASE("db.sqlite", ExtSqlite, Database),
        TYPE_CASE("db.sqlite3", ExtSqlite3, Database),
        TYPE_CASE("db.db3", ExtDb3, Database),
        TYPE_CASE("table.ibd", ExtIbd, Database),
        TYPE_CASE("table.FRM", ExtFrm, Database),
        TYPE_CASE("table.MYD", ExtMyd, Database),
        TYPE_CASE("table.myi", ExtMyi, Database),
        TYPE_CASE("legacy.MDB", ExtMdb, Database),
        TYPE_CASE("disk.iso", ExtIso, DiskImage),
        TYPE_CASE("vm.qcow2", ExtQcow2, DiskImage),
        TYPE_CASE("doc.pdf", ExtPdf, Document),
        TYPE_CASE("notes.DOCX", ExtDocx, Document),
        TYPE_CASE("notes.md", ExtMd, Document),
        TYPE_CASE("system.conf", ExtConf, Document),
        TYPE_CASE("settings.ini", ExtIni, Document),
        TYPE_CASE("launcher.desktop", ExtDesktop, Document),
        TYPE_CASE("unit.service", ExtService, Document),
        TYPE_CASE("sheet.xlsx", ExtXlsx, Document),
        TYPE_CASE("sheet.xlsm", ExtXlsm, Document),
        TYPE_CASE("slides.pptx", ExtPptx, Document),
        TYPE_CASE("page.html", ExtHtml, Document),
        TYPE_CASE("data.json", ExtJson, Document),
        TYPE_CASE("config.toml", ExtToml, Document),
        TYPE_CASE("data.xml", ExtXml, Document),
        TYPE_CASE("config.yml", ExtYml, Document),
        TYPE_CASE("config.yaml", ExtYaml, Document),
        TYPE_CASE("table.csv", ExtCsv, Document),
        TYPE_CASE("rich.rtf", ExtRtf, Document),
        TYPE_CASE("notes.odt", ExtOdt, Document),
        TYPE_CASE("package.deb", ExtDeb, Package),
        TYPE_CASE("release.RPM", ExtRpm, Package),
        TYPE_CASE("mobile.apk", ExtApk, Package),
        TYPE_CASE("bundle.snap", ExtSnap, Package),
        TYPE_CASE("bundle.flatpak", ExtFlatpak, Package),
        TYPE_CASE("tool.AppImage", ExtAppImage, Package),
        TYPE_CASE("image.png", ExtPng, Image),
        TYPE_CASE("photo.JPG", ExtJpg, Image),
        TYPE_CASE("photo.JPEG", ExtJpeg, Image),
        TYPE_CASE("icon.ico", ExtIco, Image),
        TYPE_CASE("icon.svg", ExtSvg, Image),
        TYPE_CASE("photo.webp", ExtWebp, Image),
        TYPE_CASE("scan.tiff", ExtTiff, Image),
        TYPE_CASE("scratch.tmp", ExtTmp, BackupTemp),
        TYPE_CASE("save.OLD", ExtOld, BackupTemp),
        TYPE_CASE("editor.swp", ExtSwp, BackupTemp),
        TYPE_CASE("download.part", ExtPart, BackupTemp),
        TYPE_CASE("download.crdownload", ExtCrdownload, BackupTemp),
        TYPE_CASE("lib.so", ExtSo, Library),
        TYPE_CASE("archive.A", ExtA, Library),
        TYPE_CASE("plugin.dll", ExtDll, Library),
        TYPE_CASE("plugin.dylib", ExtDylib, Library),
        TYPE_CASE("stderr.err", ExtErr, Log),
        TYPE_CASE("system.journal", ExtJournal, Log),
        TYPE_CASE("system.journal~", ExtJournalTilde, Log),
        TYPE_CASE("stdout.OUT", ExtOut, Log),
        TYPE_CASE("service.pid", ExtPid, Log),
        TYPE_CASE("song.mp3", ExtMp3, Music),
        TYPE_CASE("track.FLAC", ExtFlac, Music),
        TYPE_CASE("track.aac", ExtAac, Music),
        TYPE_CASE("track.ogg", ExtOgg, Music),
        TYPE_CASE("track.m4a", ExtM4a, Music),
        TYPE_CASE("track.wma", ExtWma, Music),
        TYPE_CASE("module.ko", ExtKo, ObjectGenerated),
        TYPE_CASE("build.o", ExtO, ObjectGenerated),
        TYPE_CASE("build.file", ExtFile, ObjectGenerated),
        TYPE_CASE("bytecode.CLASS", ExtClass, ObjectGenerated),
        TYPE_CASE("cache.pyc", ExtPyc, ObjectGenerated),
        TYPE_CASE("snapshot.commitmeta", ExtCommitMeta, ObjectGenerated),
        TYPE_CASE("module.wasm", ExtWasm, ObjectGenerated),
        TYPE_CASE("font.woff2", ExtWoff2, ObjectGenerated),
        TYPE_CASE("main.c", ExtC, Source),
        TYPE_CASE("asm.s", ExtS, Source),
        TYPE_CASE("iface.i", ExtI, Source),
        TYPE_CASE("main.cpp", ExtCpp, Source),
        TYPE_CASE("main.cc", ExtCc, Source),
        TYPE_CASE("main.cp", ExtCp, Source),
        TYPE_CASE("main.cxx", ExtCxx, Source),
        TYPE_CASE("main.c++", ExtCPlusPlus, Source),
        TYPE_CASE("script.cs", ExtCs, Source),
        TYPE_CASE("script.js", ExtJs, Source),
        TYPE_CASE("style.css", ExtCss, Source),
        TYPE_CASE("schema.sql", ExtSql, Source),
        TYPE_CASE("header.H", ExtH, Source),
        TYPE_CASE("header.hh", ExtHh, Source),
        TYPE_CASE("header.hp", ExtHp, Source),
        TYPE_CASE("header.hpp", ExtHpp, Source),
        TYPE_CASE("header.hxx", ExtHxx, Source),
        TYPE_CASE("script.go", ExtGo, Source),
        TYPE_CASE("script.kt", ExtKt, Source),
        TYPE_CASE("script.kts", ExtKts, Source),
        TYPE_CASE("notes.mm", ExtMm, Source),
        TYPE_CASE("script.pl", ExtPl, Source),
        TYPE_CASE("script.Py", ExtPy, Source),
        TYPE_CASE("script.rb", ExtRb, Source),
        TYPE_CASE("script.rs", ExtRs, Source),
        TYPE_CASE("script.sh", ExtSh, Source),
        TYPE_CASE("script.ts", ExtTs, Source),
        TYPE_CASE("dialog.ui", ExtUi, Source),
        TYPE_CASE("component.tsx", ExtTsx, Source),
        TYPE_CASE("page.jsx", ExtJsx, Source),
        TYPE_CASE("program.java", ExtJava, Source),
        TYPE_CASE("theme.scss", ExtScss, Source),
        TYPE_CASE("bundle.tar", ExtTar, Archive),
        TYPE_CASE("movie.mp4", ExtMp4, Video),
        TYPE_CASE("clip.MKV", ExtMkv, Video),
        TYPE_CASE("clip.mov", ExtMov, Video),
        TYPE_CASE("clip.flv", ExtFlv, Video),
        TYPE_CASE("clip.webm", ExtWebm, Video),
        TYPE_CASE("clip.wmv", ExtWmv, Video),
        TYPE_CASE("/captures/sample.AVI", ExtAvi, Video),
        TYPE_CASE("binary.elf", Executable, Executable),
        TYPE_CASE("program.EXE", Executable, Executable),
#undef TYPE_CASE
    };

    for (const auto &testCase : cases) {
        CAPTURE(testCase.path);
        const FileType result = FileCategorizer::categorize(testCase.path);
        CHECK(result == testCase.expectedType);
        CHECK(FileCategorizer::categoryForType(result) == testCase.expectedCategory);
    }
}

TEST_CASE("handles full paths and extension views without copying") {
    constexpr std::string_view path = "/tmp/reports/Quarterly.PDF";
    const std::size_t extensionOffset = path.find_last_of('.') + 1;

    const FileCategorizer::Result result = FileCategorizer::categorizeWithExtension(path);

    CHECK(result.type == FileType::ExtPdf);
    CHECK(FileCategorizer::categoryForType(result.type) == FileCategory::Document);
    REQUIRE(result.extensionPtr != nullptr);
    CHECK(result.extensionLen == 3);
    CHECK(result.extensionPtr == path.data() + extensionOffset);
    CHECK(std::string_view(result.extensionPtr, result.extensionLen) == "PDF");
}

TEST_CASE("returns canonical extensions for extension-backed file types") {
    CHECK(FileCategorizer::extensionForType(FileType::ExtPdf) == "pdf");
    CHECK(FileCategorizer::extensionForType(FileType::ExtCPlusPlus) == "c++");
    CHECK(FileCategorizer::extensionForType(FileType::ExtJournalTilde) == "journal~");
    CHECK(FileCategorizer::extensionForType(FileType::Unknown).empty());
    CHECK(FileCategorizer::extensionForType(FileType::Executable).empty());
    CHECK(FileCategorizer::extensionForType(FileType::Cache).empty());
    CHECK(FileCategorizer::extensionForType(FileType::VersionedSharedLibrary).empty());
}

TEST_CASE("returns friendly display names for categories") {
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Unknown)) == "Unknown");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Archive)) == "Archive");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Compressed)) == "Compressed");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Database)) == "Database");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::DiskImage)) == "Disk image");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Document)) == "Document");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Package)) == "Package");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Image)) == "Image");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::BackupTemp)) == "Backup/temp");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Cache)) == "Cache");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Library)) == "Library");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Log)) == "Log");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Music)) == "Music");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::ObjectGenerated)) == "Object/generated");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Source)) == "Source");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Video)) == "Video");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Executable)) == "Executable");
}

TEST_CASE("ignores trailing separators and leading-dot names") {
    CHECK(FileCategorizer::categorize("/tmp/folder/") == FileType::Unknown);
    CHECK(FileCategorizer::categorize(".gitignore") == FileType::Unknown);
    CHECK(FileCategorizer::categorize("/home/user/.bashrc") == FileType::Unknown);
    CHECK(FileCategorizer::categorize(".config.txt") == FileType::ExtTxt);
    CHECK(FileCategorizer::categoryForType(FileCategorizer::categorize(".config.txt")) == FileCategory::Document);
}

TEST_CASE("treats backslash as a normal filename character") {
    CHECK(FileCategorizer::categorize(R"(dir\file.JPG)") == FileType::ExtJpg);
    CHECK(FileCategorizer::categoryForType(FileCategorizer::categorize(R"(dir\file.JPG)")) == FileCategory::Image);
}

TEST_CASE("returns unknown for missing unsupported or deferred cases") {
    constexpr TypeCase cases[] = {
        {"", FileType::Unknown, FileCategory::Unknown},
        {"README", FileType::Unknown, FileCategory::Unknown},
        {"name.", FileType::Unknown, FileCategory::Unknown},
        {"file.unknown", FileType::Unknown, FileCategory::Unknown},
        {"file.longext", FileType::Unknown, FileCategory::Unknown},
        {"installer.run", FileType::Unknown, FileCategory::Unknown},
        {"payload.bin", FileType::Unknown, FileCategory::Unknown},
        {"archive.toast", FileType::Unknown, FileCategory::Unknown},
    };

    for (const auto &testCase : cases) {
        CAPTURE(testCase.path);
        const FileType result = FileCategorizer::categorize(testCase.path);
        CHECK(result == testCase.expectedType);
        CHECK(FileCategorizer::categoryForType(result) == testCase.expectedCategory);
    }

    CHECK(FileCategorizer::categorize("archive.tar.gz") == FileType::ExtGz);
    CHECK(FileCategorizer::categoryForType(FileCategorizer::categorize("archive.tar.gz")) == FileCategory::Compressed);
    CHECK(FileCategorizer::categorize("archive.pkg.tar.zst") == FileType::ExtZst);
    CHECK(FileCategorizer::categoryForType(FileCategorizer::categorize("archive.pkg.tar.zst")) == FileCategory::Compressed);
}

TEST_CASE("detects versioned shared libraries with a case-sensitive fallback") {
    CHECK(FileCategorizer::categorize("libfoo.so.1") == FileType::VersionedSharedLibrary);
    CHECK(FileCategorizer::categoryForType(FileCategorizer::categorize("libfoo.so.1")) == FileCategory::Library);
    CHECK(FileCategorizer::categorize("/usr/lib/libbar.so.1.2") == FileType::VersionedSharedLibrary);
    CHECK(FileCategorizer::categoryForType(FileCategorizer::categorize("/usr/lib/libbar.so.1.2")) == FileCategory::Library);
    CHECK(FileCategorizer::categorize("libc++.so.debug") == FileType::VersionedSharedLibrary);
    CHECK(FileCategorizer::categoryForType(FileCategorizer::categorize("libc++.so.debug")) == FileCategory::Library);
    CHECK(FileCategorizer::categorize("libwidget.so.debugsymbols") == FileType::VersionedSharedLibrary);
    CHECK(FileCategorizer::categoryForType(FileCategorizer::categorize("libwidget.so.debugsymbols")) == FileCategory::Library);

    CHECK(FileCategorizer::categorize("Libfoo.so.1") == FileType::Unknown);
    CHECK(FileCategorizer::categorize("libfoo.SO.1") == FileType::Unknown);
    CHECK(FileCategorizer::categorize("plugin.so.1") == FileType::Unknown);
}

TEST_CASE("returns null extension info when no supported extension is present") {
    const FileCategorizer::Result result = FileCategorizer::categorizeWithExtension(".gitignore");

    CHECK(result.type == FileType::Unknown);
    CHECK(result.extensionPtr == nullptr);
    CHECK(result.extensionLen == 0);
}

TEST_CASE("counts file categories across a direntry tree") {
    ldirstat::DirEntryStore entryStore;
    auto entryCursor = entryStore.allocateAppendCursor();

    const ldirstat::EntryRef rootRef = entryStore.add(entryCursor);
    const ldirstat::EntryRef docRef = entryStore.add(entryCursor);
    const ldirstat::EntryRef musicRef = entryStore.add(entryCursor);
    const ldirstat::EntryRef subdirRef = entryStore.add(entryCursor);
    const ldirstat::EntryRef sourceRef = entryStore.add(entryCursor);
    const ldirstat::EntryRef unknownRef = entryStore.add(entryCursor);

    auto& root = entryStore[rootRef];
    root.type = ldirstat::EntryType::Directory;
    root.firstChild = docRef;
    root.childCount = 3;

    auto& doc = entryStore[docRef];
    doc.parent = rootRef;
    doc.type = ldirstat::EntryType::File;
    doc.fileType = FileType::ExtPdf;
    doc.hardLinks = 1;
    doc.size = 100;
    doc.nextSibling = musicRef;

    auto& music = entryStore[musicRef];
    music.parent = rootRef;
    music.type = ldirstat::EntryType::File;
    music.fileType = FileType::ExtMp3;
    music.hardLinks = 2;
    music.size = 200;
    music.nextSibling = subdirRef;

    auto& subdir = entryStore[subdirRef];
    subdir.parent = rootRef;
    subdir.type = ldirstat::EntryType::Directory;
    subdir.firstChild = sourceRef;
    subdir.childCount = 2;

    auto& source = entryStore[sourceRef];
    source.parent = subdirRef;
    source.type = ldirstat::EntryType::File;
    source.fileType = FileType::ExtCpp;
    source.hardLinks = 1;
    source.size = 50;
    source.nextSibling = unknownRef;

    auto& unknown = entryStore[unknownRef];
    unknown.parent = subdirRef;
    unknown.type = ldirstat::EntryType::File;
    unknown.fileType = FileType::Unknown;
    unknown.hardLinks = 1;
    unknown.size = 30;

    ldirstat::FileCategoryCounter counter(entryStore);
    counter.countTree(rootRef);

    const auto& items = counter.items();
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Document)].count == 1);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Document)].totalSize == 100);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Music)].count == 1);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Music)].totalSize == 100);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Source)].count == 1);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Source)].totalSize == 50);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Unknown)].count == 1);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Unknown)].totalSize == 30);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Archive)].count == 0);
    CHECK(items[FileCategorizer::categoryIndex(FileCategory::Archive)].totalSize == 0);

    const auto& typeItems = counter.typeItems();
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::ExtPdf)].count == 1);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::ExtPdf)].totalSize == 100);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::ExtMp3)].count == 1);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::ExtMp3)].totalSize == 100);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::ExtCpp)].count == 1);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::ExtCpp)].totalSize == 50);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::Unknown)].count == 1);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::Unknown)].totalSize == 30);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::Executable)].count == 0);
    CHECK(typeItems[FileCategorizer::typeIndex(FileType::Executable)].totalSize == 0);
}

TEST_CASE("scanner detects extensionless executables from mode bits") {
    TempDir tempDir;
    writeFileWithMode(tempDir.path / "tool", 0755);
    writeFileWithMode(tempDir.path / "script.sh", 0755);
    writeFileWithMode(tempDir.path / "libdemo.so.1", 0755);
    writeFileWithMode(tempDir.path / "README", 0644);
    REQUIRE(::symlink("tool", (tempDir.path / "tool-link").c_str()) == 0);

    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::Scanner scanner(entryStore, nameStore);

    const ldirstat::EntryRef root = scanner.scan(tempDir.path.string(), 1);
    REQUIRE(root.valid());

    const ldirstat::EntryRef tool = findChildByName(entryStore, nameStore, root, "tool");
    const ldirstat::EntryRef script = findChildByName(entryStore, nameStore, root, "script.sh");
    const ldirstat::EntryRef library = findChildByName(entryStore, nameStore, root, "libdemo.so.1");
    const ldirstat::EntryRef readme = findChildByName(entryStore, nameStore, root, "README");
    const ldirstat::EntryRef toolLink = findChildByName(entryStore, nameStore, root, "tool-link");

    REQUIRE(tool.valid());
    REQUIRE(script.valid());
    REQUIRE(library.valid());
    REQUIRE(readme.valid());
    REQUIRE(toolLink.valid());

    CHECK(entryStore[tool].type == ldirstat::EntryType::File);
    CHECK(entryStore[tool].fileType == FileType::Executable);
    CHECK(FileCategorizer::categoryForType(entryStore[tool].fileType) == FileCategory::Executable);

    CHECK(entryStore[script].type == ldirstat::EntryType::File);
    CHECK(entryStore[script].fileType == FileType::ExtSh);
    CHECK(FileCategorizer::categoryForType(entryStore[script].fileType) == FileCategory::Source);

    CHECK(entryStore[library].type == ldirstat::EntryType::File);
    CHECK(entryStore[library].fileType == FileType::VersionedSharedLibrary);
    CHECK(FileCategorizer::categoryForType(entryStore[library].fileType) == FileCategory::Library);

    CHECK(entryStore[readme].type == ldirstat::EntryType::File);
    CHECK(entryStore[readme].fileType == FileType::Unknown);
    CHECK(FileCategorizer::categoryForType(entryStore[readme].fileType) == FileCategory::Unknown);

    CHECK(entryStore[toolLink].type == ldirstat::EntryType::Symlink);
}

TEST_CASE("scanner assigns cache category for unknown files under cache subtrees") {
    TempDir tempDir;
    REQUIRE(fs::create_directories(tempDir.path / ".cache" / "nested" / "deeper"));
    REQUIRE(fs::create_directories(tempDir.path / "cache"));
    REQUIRE(fs::create_directories(tempDir.path / "mycache"));
    REQUIRE(fs::create_directories(tempDir.path / "cache-data"));

    writeFileWithMode(tempDir.path / ".cache" / "opaque", 0644);
    writeFileWithMode(tempDir.path / ".cache" / "icon.png", 0644);
    writeFileWithMode(tempDir.path / ".cache" / "tool", 0755);
    writeFileWithMode(tempDir.path / ".cache" / "nested" / "deeper" / "blob", 0644);
    writeFileWithMode(tempDir.path / "cache" / "entry", 0644);
    writeFileWithMode(tempDir.path / "mycache" / "entry", 0644);
    writeFileWithMode(tempDir.path / "cache-data" / "entry", 0644);

    ldirstat::DirEntryStore entryStore;
    ldirstat::NameStore nameStore;
    ldirstat::Scanner scanner(entryStore, nameStore);

    const ldirstat::EntryRef root = scanner.scan(tempDir.path.string(), 1);
    REQUIRE(root.valid());

    const ldirstat::EntryRef cacheUnknown = findDescendantByPath(entryStore, nameStore, root, {".cache", "opaque"});
    const ldirstat::EntryRef cacheImage = findDescendantByPath(entryStore, nameStore, root, {".cache", "icon.png"});
    const ldirstat::EntryRef cacheExecutable = findDescendantByPath(entryStore, nameStore, root, {".cache", "tool"});
    const ldirstat::EntryRef nestedCacheUnknown =
        findDescendantByPath(entryStore, nameStore, root, {".cache", "nested", "deeper", "blob"});
    const ldirstat::EntryRef plainCacheUnknown = findDescendantByPath(entryStore, nameStore, root, {"cache", "entry"});
    const ldirstat::EntryRef mycacheUnknown = findDescendantByPath(entryStore, nameStore, root, {"mycache", "entry"});
    const ldirstat::EntryRef cacheDataUnknown = findDescendantByPath(entryStore, nameStore, root, {"cache-data", "entry"});

    REQUIRE(cacheUnknown.valid());
    REQUIRE(cacheImage.valid());
    REQUIRE(cacheExecutable.valid());
    REQUIRE(nestedCacheUnknown.valid());
    REQUIRE(plainCacheUnknown.valid());
    REQUIRE(mycacheUnknown.valid());
    REQUIRE(cacheDataUnknown.valid());

    CHECK(entryStore[cacheUnknown].fileType == FileType::Cache);
    CHECK(FileCategorizer::categoryForType(entryStore[cacheUnknown].fileType) == FileCategory::Cache);
    CHECK(entryStore[cacheImage].fileType == FileType::ExtPng);
    CHECK(FileCategorizer::categoryForType(entryStore[cacheImage].fileType) == FileCategory::Image);
    CHECK(entryStore[cacheExecutable].fileType == FileType::Executable);
    CHECK(FileCategorizer::categoryForType(entryStore[cacheExecutable].fileType) == FileCategory::Executable);
    CHECK(entryStore[nestedCacheUnknown].fileType == FileType::Cache);
    CHECK(FileCategorizer::categoryForType(entryStore[nestedCacheUnknown].fileType) == FileCategory::Cache);
    CHECK(entryStore[plainCacheUnknown].fileType == FileType::Cache);
    CHECK(FileCategorizer::categoryForType(entryStore[plainCacheUnknown].fileType) == FileCategory::Cache);
    CHECK(entryStore[mycacheUnknown].fileType == FileType::Unknown);
    CHECK(FileCategorizer::categoryForType(entryStore[mycacheUnknown].fileType) == FileCategory::Unknown);
    CHECK(entryStore[cacheDataUnknown].fileType == FileType::Unknown);
    CHECK(FileCategorizer::categoryForType(entryStore[cacheDataUnknown].fileType) == FileCategory::Unknown);
}

TEST_CASE("scanner assigns cache category when the scan root itself is a cache directory") {
    TempDir tempDir;
    REQUIRE(fs::create_directories(tempDir.path / ".cache" / "nested"));
    REQUIRE(fs::create_directories(tempDir.path / "cache"));

    writeFileWithMode(tempDir.path / ".cache" / "root-opaque", 0644);
    writeFileWithMode(tempDir.path / ".cache" / "nested" / "child", 0644);
    writeFileWithMode(tempDir.path / "cache" / "root-opaque", 0644);

    {
        ldirstat::DirEntryStore entryStore;
        ldirstat::NameStore nameStore;
        ldirstat::Scanner scanner(entryStore, nameStore);

        const ldirstat::EntryRef root = scanner.scan((tempDir.path / ".cache").string(), 1);
        REQUIRE(root.valid());

        const ldirstat::EntryRef rootOpaque = findChildByName(entryStore, nameStore, root, "root-opaque");
        const ldirstat::EntryRef nestedChild = findDescendantByPath(entryStore, nameStore, root, {"nested", "child"});

        REQUIRE(rootOpaque.valid());
        REQUIRE(nestedChild.valid());

        CHECK(entryStore[rootOpaque].fileType == FileType::Cache);
        CHECK(FileCategorizer::categoryForType(entryStore[rootOpaque].fileType) == FileCategory::Cache);
        CHECK(entryStore[nestedChild].fileType == FileType::Cache);
        CHECK(FileCategorizer::categoryForType(entryStore[nestedChild].fileType) == FileCategory::Cache);
    }

    {
        ldirstat::DirEntryStore entryStore;
        ldirstat::NameStore nameStore;
        ldirstat::Scanner scanner(entryStore, nameStore);

        const ldirstat::EntryRef root = scanner.scan((tempDir.path / "cache").string(), 1);
        REQUIRE(root.valid());

        const ldirstat::EntryRef rootOpaque = findChildByName(entryStore, nameStore, root, "root-opaque");

        REQUIRE(rootOpaque.valid());
        CHECK(entryStore[rootOpaque].fileType == FileType::Cache);
        CHECK(FileCategorizer::categoryForType(entryStore[rootOpaque].fileType) == FileCategory::Cache);
    }
}

} // namespace
