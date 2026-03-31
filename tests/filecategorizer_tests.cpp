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
namespace fs = std::filesystem;

struct CategoryCase {
    std::string_view path;
    FileCategory expected;
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

TEST_CASE("categorizes supported extensions") {
    constexpr CategoryCase cases[] = {
        {"archive.zip", FileCategory::Archive},
        {"archive.7Z", FileCategory::Archive},
        {"bundle.rar", FileCategory::Archive},
        {"bundle.tgz", FileCategory::Archive},
        {"bundle.tbz2", FileCategory::Archive},
        {"bundle.gz", FileCategory::Compressed},
        {"backup.lz", FileCategory::Compressed},
        {"backup.xz", FileCategory::Compressed},
        {"backup.zst", FileCategory::Compressed},
        {"backup.zstd", FileCategory::Compressed},
        {"cache.lz4", FileCategory::Compressed},
        {"db.sqlite", FileCategory::Database},
        {"db.sqlite3", FileCategory::Database},
        {"db.db3", FileCategory::Database},
        {"table.ibd", FileCategory::Database},
        {"table.FRM", FileCategory::Database},
        {"table.MYD", FileCategory::Database},
        {"table.myi", FileCategory::Database},
        {"legacy.MDB", FileCategory::Database},
        {"disk.iso", FileCategory::DiskImage},
        {"vm.qcow2", FileCategory::DiskImage},
        {"doc.pdf", FileCategory::Document},
        {"notes.DOCX", FileCategory::Document},
        {"notes.md", FileCategory::Document},
        {"system.conf", FileCategory::Document},
        {"settings.ini", FileCategory::Document},
        {"launcher.desktop", FileCategory::Document},
        {"unit.service", FileCategory::Document},
        {"sheet.xlsx", FileCategory::Document},
        {"sheet.xlsm", FileCategory::Document},
        {"slides.pptx", FileCategory::Document},
        {"page.html", FileCategory::Document},
        {"data.json", FileCategory::Document},
        {"config.toml", FileCategory::Document},
        {"data.xml", FileCategory::Document},
        {"config.yml", FileCategory::Document},
        {"config.yaml", FileCategory::Document},
        {"table.csv", FileCategory::Document},
        {"rich.rtf", FileCategory::Document},
        {"notes.odt", FileCategory::Document},
        {"package.deb", FileCategory::Package},
        {"release.RPM", FileCategory::Package},
        {"mobile.apk", FileCategory::Package},
        {"bundle.snap", FileCategory::Package},
        {"bundle.flatpak", FileCategory::Package},
        {"tool.AppImage", FileCategory::Package},
        {"image.png", FileCategory::Image},
        {"photo.JPG", FileCategory::Image},
        {"photo.JPEG", FileCategory::Image},
        {"icon.ico", FileCategory::Image},
        {"icon.svg", FileCategory::Image},
        {"photo.webp", FileCategory::Image},
        {"scan.tiff", FileCategory::Image},
        {"scratch.tmp", FileCategory::BackupTemp},
        {"save.OLD", FileCategory::BackupTemp},
        {"editor.swp", FileCategory::BackupTemp},
        {"download.part", FileCategory::BackupTemp},
        {"download.crdownload", FileCategory::BackupTemp},
        {"lib.so", FileCategory::Library},
        {"archive.A", FileCategory::Library},
        {"plugin.dll", FileCategory::Library},
        {"plugin.dylib", FileCategory::Library},
        {"stderr.err", FileCategory::Log},
        {"system.journal", FileCategory::Log},
        {"system.journal~", FileCategory::Log},
        {"stdout.OUT", FileCategory::Log},
        {"service.pid", FileCategory::Log},
        {"song.mp3", FileCategory::Music},
        {"track.FLAC", FileCategory::Music},
        {"track.aac", FileCategory::Music},
        {"track.ogg", FileCategory::Music},
        {"track.m4a", FileCategory::Music},
        {"track.wma", FileCategory::Music},
        {"module.ko", FileCategory::ObjectGenerated},
        {"build.o", FileCategory::ObjectGenerated},
        {"build.file", FileCategory::ObjectGenerated},
        {"bytecode.CLASS", FileCategory::ObjectGenerated},
        {"cache.pyc", FileCategory::ObjectGenerated},
        {"snapshot.commitmeta", FileCategory::ObjectGenerated},
        {"module.wasm", FileCategory::ObjectGenerated},
        {"font.woff2", FileCategory::ObjectGenerated},
        {"main.c", FileCategory::Source},
        {"asm.s", FileCategory::Source},
        {"iface.i", FileCategory::Source},
        {"main.cpp", FileCategory::Source},
        {"main.cc", FileCategory::Source},
        {"main.cp", FileCategory::Source},
        {"main.cxx", FileCategory::Source},
        {"main.c++", FileCategory::Source},
        {"script.cs", FileCategory::Source},
        {"script.js", FileCategory::Source},
        {"style.css", FileCategory::Source},
        {"schema.sql", FileCategory::Source},
        {"header.H", FileCategory::Source},
        {"header.hh", FileCategory::Source},
        {"header.hp", FileCategory::Source},
        {"header.hpp", FileCategory::Source},
        {"header.hxx", FileCategory::Source},
        {"script.go", FileCategory::Source},
        {"script.kt", FileCategory::Source},
        {"script.kts", FileCategory::Source},
        {"notes.mm", FileCategory::Source},
        {"script.pl", FileCategory::Source},
        {"script.Py", FileCategory::Source},
        {"script.rb", FileCategory::Source},
        {"script.rs", FileCategory::Source},
        {"script.sh", FileCategory::Source},
        {"script.ts", FileCategory::Source},
        {"dialog.ui", FileCategory::Source},
        {"component.tsx", FileCategory::Source},
        {"page.jsx", FileCategory::Source},
        {"program.java", FileCategory::Source},
        {"theme.scss", FileCategory::Source},
        {"bundle.tar", FileCategory::Archive},
        {"movie.mp4", FileCategory::Video},
        {"clip.MKV", FileCategory::Video},
        {"clip.mov", FileCategory::Video},
        {"clip.flv", FileCategory::Video},
        {"clip.webm", FileCategory::Video},
        {"clip.wmv", FileCategory::Video},
        {"/captures/sample.AVI", FileCategory::Video},
        {"binary.elf", FileCategory::Executable},
        {"program.EXE", FileCategory::Executable},
    };

    for (const auto& testCase : cases) {
        CAPTURE(testCase.path);
        CHECK(FileCategorizer::categorize(testCase.path) == testCase.expected);
    }
}

TEST_CASE("handles full paths and extension views without copying") {
    constexpr std::string_view path = "/tmp/reports/Quarterly.PDF";
    const std::size_t extensionOffset = path.find_last_of('.') + 1;

    const FileCategorizer::Result result = FileCategorizer::categorizeWithExtension(path);

    CHECK(result.category == FileCategory::Document);
    REQUIRE(result.extensionPtr != nullptr);
    CHECK(result.extensionLen == 3);
    CHECK(result.extensionPtr == path.data() + extensionOffset);
    CHECK(std::string_view(result.extensionPtr, result.extensionLen) == "PDF");
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
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Library)) == "Library");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Log)) == "Log");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Music)) == "Music");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::ObjectGenerated)) == "Object/generated");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Source)) == "Source");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Video)) == "Video");
    CHECK(std::string_view(FileCategorizer::displayCategoryName(FileCategory::Executable)) == "Executable");
}

TEST_CASE("ignores trailing separators and leading-dot names") {
    CHECK(FileCategorizer::categorize("/tmp/folder/") == FileCategory::Unknown);
    CHECK(FileCategorizer::categorize(".gitignore") == FileCategory::Unknown);
    CHECK(FileCategorizer::categorize("/home/user/.bashrc") == FileCategory::Unknown);
    CHECK(FileCategorizer::categorize(".config.txt") == FileCategory::Document);
}

TEST_CASE("treats backslash as a normal filename character") {
    CHECK(FileCategorizer::categorize(R"(dir\file.JPG)") == FileCategory::Image);
}

TEST_CASE("returns unknown for missing unsupported or deferred cases") {
    constexpr CategoryCase cases[] = {
        {"", FileCategory::Unknown},
        {"README", FileCategory::Unknown},
        {"name.", FileCategory::Unknown},
        {"file.unknown", FileCategory::Unknown},
        {"file.longext", FileCategory::Unknown},
        {"installer.run", FileCategory::Unknown},
        {"payload.bin", FileCategory::Unknown},
        {"archive.toast", FileCategory::Unknown},
    };

    for (const auto& testCase : cases) {
        CAPTURE(testCase.path);
        CHECK(FileCategorizer::categorize(testCase.path) == testCase.expected);
    }

    CHECK(FileCategorizer::categorize("archive.tar.gz") == FileCategory::Compressed);
    CHECK(FileCategorizer::categorize("archive.pkg.tar.zst") == FileCategory::Compressed);
}

TEST_CASE("detects versioned shared libraries with a case-sensitive fallback") {
    CHECK(FileCategorizer::categorize("libfoo.so.1") == FileCategory::Library);
    CHECK(FileCategorizer::categorize("/usr/lib/libbar.so.1.2") == FileCategory::Library);
    CHECK(FileCategorizer::categorize("libc++.so.debug") == FileCategory::Library);
    CHECK(FileCategorizer::categorize("libwidget.so.debugsymbols") == FileCategory::Library);

    CHECK(FileCategorizer::categorize("Libfoo.so.1") == FileCategory::Unknown);
    CHECK(FileCategorizer::categorize("libfoo.SO.1") == FileCategory::Unknown);
    CHECK(FileCategorizer::categorize("plugin.so.1") == FileCategory::Unknown);
}

TEST_CASE("returns null extension info when no supported extension is present") {
    const FileCategorizer::Result result = FileCategorizer::categorizeWithExtension(".gitignore");

    CHECK(result.category == FileCategory::Unknown);
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
    doc.fileCategory = FileCategory::Document;
    doc.hardLinks = 1;
    doc.size = 100;
    doc.nextSibling = musicRef;

    auto& music = entryStore[musicRef];
    music.parent = rootRef;
    music.type = ldirstat::EntryType::File;
    music.fileCategory = FileCategory::Music;
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
    source.fileCategory = FileCategory::Source;
    source.hardLinks = 1;
    source.size = 50;
    source.nextSibling = unknownRef;

    auto& unknown = entryStore[unknownRef];
    unknown.parent = subdirRef;
    unknown.type = ldirstat::EntryType::File;
    unknown.fileCategory = FileCategory::Unknown;
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
    CHECK(entryStore[tool].fileCategory == FileCategory::Executable);

    CHECK(entryStore[script].type == ldirstat::EntryType::File);
    CHECK(entryStore[script].fileCategory == FileCategory::Source);

    CHECK(entryStore[library].type == ldirstat::EntryType::File);
    CHECK(entryStore[library].fileCategory == FileCategory::Library);

    CHECK(entryStore[readme].type == ldirstat::EntryType::File);
    CHECK(entryStore[readme].fileCategory == FileCategory::Unknown);

    CHECK(entryStore[toolLink].type == ldirstat::EntryType::Symlink);
}

} // namespace
