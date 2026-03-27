#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string_view>

#include "filecategorizer.h"

namespace {

using ldirstat::FileCategorizer;
using ldirstat::FileCategory;

struct CategoryCase {
    std::string_view path;
    FileCategory expected;
};

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
        {"sheet.xlsx", FileCategory::Document},
        {"sheet.xlsm", FileCategory::Document},
        {"slides.pptx", FileCategory::Document},
        {"page.html", FileCategory::Document},
        {"data.json", FileCategory::Document},
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
        {"stdout.OUT", FileCategory::Log},
        {"service.pid", FileCategory::Log},
        {"song.mp3", FileCategory::Music},
        {"track.FLAC", FileCategory::Music},
        {"track.aac", FileCategory::Music},
        {"track.ogg", FileCategory::Music},
        {"track.m4a", FileCategory::Music},
        {"track.wma", FileCategory::Music},
        {"build.o", FileCategory::ObjectGenerated},
        {"bytecode.CLASS", FileCategory::ObjectGenerated},
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
        {"component.tsx", FileCategory::Source},
        {"page.jsx", FileCategory::Source},
        {"program.java", FileCategory::Source},
        {"theme.scss", FileCategory::Source},
        {"movie.mp4", FileCategory::Video},
        {"clip.MKV", FileCategory::Video},
        {"clip.mov", FileCategory::Video},
        {"clip.flv", FileCategory::Video},
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

TEST_CASE("returns null extension info when no supported extension is present") {
    const FileCategorizer::Result result = FileCategorizer::categorizeWithExtension(".gitignore");

    CHECK(result.category == FileCategory::Unknown);
    CHECK(result.extensionPtr == nullptr);
    CHECK(result.extensionLen == 0);
}

} // namespace
