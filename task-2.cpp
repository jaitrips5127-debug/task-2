#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <zlib.h>
#include <chrono>

const int CHUNK_SIZE = 1024 * 1024; // 1 MB

std::mutex mtx;

// Compress a chunk
std::vector<char> compressChunk(const std::vector<char>& input) {
    uLong srcLen = input.size();
    uLong destLen = compressBound(srcLen);
    std::vector<char> output(destLen);

    if (compress((Bytef*)output.data(), &destLen, (const Bytef*)input.data(), srcLen) != Z_OK) {
        throw std::runtime_error("Compression failed.");
    }
    output.resize(destLen);
    return output;
}

// Decompress a chunk
std::vector<char> decompressChunk(const std::vector<char>& input, uLong originalSize) {
    std::vector<char> output(originalSize);
    if (uncompress((Bytef*)output.data(), &originalSize, (const Bytef*)input.data(), input.size()) != Z_OK) {
        throw std::runtime_error("Decompression failed.");
    }
    return output;
}

void compressFile(const std::string& inputPath, const std::string& outputPath) {
    std::ifstream inFile(inputPath, std::ios::binary);
    std::ofstream outFile(outputPath, std::ios::binary);

    std::vector<std::thread> threads;
    std::vector<std::vector<char>> chunks;
    std::vector<std::vector<char>> compressedChunks;

    while (!inFile.eof()) {
        std::vector<char> buffer(CHUNK_SIZE);
        inFile.read(buffer.data(), CHUNK_SIZE);
        buffer.resize(inFile.gcount());
        chunks.push_back(buffer);
    }

    compressedChunks.resize(chunks.size());

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < chunks.size(); ++i) {
        threads.emplace_back([&, i]() {
            auto compressed = compressChunk(chunks[i]);

            std::lock_guard<std::mutex> lock(mtx);
            compressedChunks[i] = compressed;
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (const auto& chunk : compressedChunks) {
        uint32_t chunkSize = chunk.size();
        outFile.write((char*)&chunkSize, sizeof(chunkSize));
        outFile.write(chunk.data(), chunkSize);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Compression done in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms\n";
}

void decompressFile(const std::string& inputPath, const std::string& outputPath, const std::vector<uLong>& originalSizes) {
    std::ifstream inFile(inputPath, std::ios::binary);
    std::ofstream outFile(outputPath, std::ios::binary);

    std::vector<std::thread> threads;
    std::vector<std::vector<char>> compressedChunks;
    std::vector<std::vector<char>> decompressedChunks(originalSizes.size());

    for (size_t i = 0; i < originalSizes.size(); ++i) {
        uint32_t chunkSize;
        inFile.read((char*)&chunkSize, sizeof(chunkSize));

        std::vector<char> buffer(chunkSize);
        inFile.read(buffer.data(), chunkSize);
        compressedChunks.push_back(buffer);
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < compressedChunks.size(); ++i) {
        threads.emplace_back([&, i]() {
            auto decompressed = decompressChunk(compressedChunks[i], originalSizes[i]);

            std::lock_guard<std::mutex> lock(mtx);
            decompressedChunks[i] = decompressed;
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (const auto& chunk : decompressedChunks) {
        outFile.write(chunk.data(), chunk.size());
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Decompression done in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms\n";
}

int main() {
    std::string inputFile = "input.txt";
    std::string compressedFile = "compressed.dat";
    std::string decompressedFile = "decompressed.txt";

    std::ifstream in(inputFile, std::ios::binary);
    std::vector<uLong> originalSizes;

    while (!in.eof()) {
        std::vector<char> buffer(CHUNK_SIZE);
        in.read(buffer.data(), CHUNK_SIZE);
        size_t readSize = in.gcount();
        if (readSize == 0) break;

        buffer.resize(readSize);
        originalSizes.push_back(readSize);
    }
    in.close();

    compressFile(inputFile, compressedFile);
    decompressFile(compressedFile, decompressedFile, originalSizes);

    return 0;
}