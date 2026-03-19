#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint64_t kFilesPerDirectory = 1000;
constexpr uint32_t kSubdirsPerDirectory = 4;
constexpr size_t kMaxFileSize = 8 * 1024;

bool parseFileCount(const char* text, uint64_t& value) {
    char* end = nullptr;
    errno = 0;
    unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    value = static_cast<uint64_t>(parsed);
    return true;
}

std::string makeDirectoryName(uint64_t dirIndex) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "dir%08llu",
                  static_cast<unsigned long long>(dirIndex));
    return buffer;
}

std::string makeFileName(uint64_t fileIndex) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "file%010llu.bin",
                  static_cast<unsigned long long>(fileIndex));
    return buffer;
}

uint64_t directoryCountForFiles(uint64_t remainingFiles) {
    if (remainingFiles == 0) {
        return 0;
    }

    return (remainingFiles + kFilesPerDirectory - 1) / kFilesPerDirectory;
}

bool createFile(const std::filesystem::path& filePath,
                size_t fileSize,
                const std::vector<char>& fileData) {
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    if (fileSize > 0) {
        out.write(fileData.data(), static_cast<std::streamsize>(fileSize));
    }

    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <file_count>\n", argv[0]);
        return 1;
    }

    uint64_t fileLimit = 0;
    if (!parseFileCount(argv[1], fileLimit)) {
        std::fprintf(stderr, "file_count must be a non-negative integer\n");
        return 1;
    }

    std::filesystem::path rootPath =
        std::filesystem::current_path() / ("gentree_" + std::to_string(fileLimit));
    std::error_code error;

    if (std::filesystem::exists(rootPath, error)) {
        std::fprintf(stderr, "output path already exists: %s\n", rootPath.c_str());
        return 1;
    }
    if (error) {
        std::fprintf(stderr, "failed to inspect output path %s: %s\n",
                     rootPath.c_str(), error.message().c_str());
        return 1;
    }

    if (!std::filesystem::create_directory(rootPath, error)) {
        std::fprintf(stderr, "failed to create root directory %s: %s\n",
                     rootPath.c_str(), error.message().c_str());
        return 1;
    }

    std::vector<char> fileData(kMaxFileSize);
    for (size_t i = 0; i < fileData.size(); ++i) {
        fileData[i] = static_cast<char>(i & 0xff);
    }

    std::mt19937_64 rng(0x4c64697273746174ULL);
    std::uniform_int_distribution<size_t> sizeDist(0, kMaxFileSize);

    uint64_t remainingFiles = fileLimit;
    uint64_t createdFiles = 0;
    uint64_t createdDirectories = 1;
    uint64_t nextDirId = 0;
    uint64_t nextFileId = 0;
    uint64_t totalBytes = 0;

    std::vector<std::filesystem::path> currentLevel;
    currentLevel.reserve(kSubdirsPerDirectory);

    uint64_t initialDirCount = std::min<uint64_t>(
        kSubdirsPerDirectory, directoryCountForFiles(remainingFiles));
    for (uint64_t i = 0; i < initialDirCount; ++i) {
        std::filesystem::path dirPath = rootPath / makeDirectoryName(nextDirId++);
        if (!std::filesystem::create_directory(dirPath, error)) {
            std::fprintf(stderr, "failed to create directory %s: %s\n",
                         dirPath.c_str(), error.message().c_str());
            return 1;
        }
        currentLevel.push_back(std::move(dirPath));
        ++createdDirectories;
    }

    auto startTime = std::chrono::steady_clock::now();

    while (remainingFiles > 0 && !currentLevel.empty()) {
        for (const std::filesystem::path& dirPath : currentLevel) {
            if (remainingFiles == 0) {
                break;
            }

            uint64_t filesHere = std::min<uint64_t>(remainingFiles, kFilesPerDirectory);
            for (uint64_t i = 0; i < filesHere; ++i) {
                size_t fileSize = sizeDist(rng);
                std::filesystem::path filePath = dirPath / makeFileName(nextFileId++);
                if (!createFile(filePath, fileSize, fileData)) {
                    std::fprintf(stderr, "failed to create file %s\n", filePath.c_str());
                    return 1;
                }
                totalBytes += fileSize;
            }

            remainingFiles -= filesHere;
            createdFiles += filesHere;
        }

        if (remainingFiles == 0) {
            break;
        }

        uint64_t nextLevelDirCount = std::min<uint64_t>(
            static_cast<uint64_t>(currentLevel.size()) * kSubdirsPerDirectory,
            directoryCountForFiles(remainingFiles));

        std::vector<std::filesystem::path> nextLevel;
        nextLevel.reserve(nextLevelDirCount);

        std::deque<size_t> parentQueue;
        for (size_t i = 0; i < currentLevel.size(); ++i) {
            parentQueue.push_back(i);
        }

        std::vector<uint32_t> childCounts(currentLevel.size(), 0);
        while (nextLevel.size() < nextLevelDirCount && !parentQueue.empty()) {
            size_t parentIndex = parentQueue.front();
            parentQueue.pop_front();

            std::filesystem::path dirPath =
                currentLevel[parentIndex] / makeDirectoryName(nextDirId++);
            if (!std::filesystem::create_directory(dirPath, error)) {
                std::fprintf(stderr, "failed to create directory %s: %s\n",
                             dirPath.c_str(), error.message().c_str());
                return 1;
            }

            nextLevel.push_back(std::move(dirPath));
            ++createdDirectories;
            ++childCounts[parentIndex];

            if (childCounts[parentIndex] < kSubdirsPerDirectory) {
                parentQueue.push_back(parentIndex);
            }
        }

        currentLevel = std::move(nextLevel);
    }

    auto endTime = std::chrono::steady_clock::now();
    double elapsedMs =
        std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count()
        / 1000.0;

    if (createdFiles != fileLimit) {
        std::fprintf(stderr, "generation stopped early after %llu files\n",
                     static_cast<unsigned long long>(createdFiles));
        return 1;
    }

    std::printf("root:  %s\n", rootPath.c_str());
    std::printf("dirs:  %llu\n", static_cast<unsigned long long>(createdDirectories));
    std::printf("files: %llu\n", static_cast<unsigned long long>(createdFiles));
    std::printf("bytes: %llu\n", static_cast<unsigned long long>(totalBytes));
    std::printf("time:  %.3f ms\n", elapsedMs);

    return 0;
}
