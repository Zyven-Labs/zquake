#include <catch2/catch_test_macros.hpp>
#include "filesystem/virtual_fs.hpp"
#include "filesystem/pak_archive.hpp"
#include <filesystem>
#include <fstream>

TEST_CASE("MemoryFS operations", "[filesystem]") {
    using namespace zq;
    using namespace zq::fs;
    
    SECTION("Write and read file") {
        MemoryFS fs;
        std::vector<uint8_t> data{'H', 'e', 'l', 'l', 'o'};
        fs.WriteFile("test.txt", data.data(), data.size());
        REQUIRE(fs.Exists("test.txt"));
        
        auto read_data = fs.ReadFileAll("test.txt");
        REQUIRE(read_data.has_value());
        REQUIRE(read_data->size() == 5);
        REQUIRE((*read_data)[0] == 'H');
        REQUIRE((*read_data)[4] == 'o');
    }
    
    SECTION("Delete and rename") {
        MemoryFS fs;
        std::vector<uint8_t> data{'X'};
        fs.WriteFile("file_a.txt", data.data(), 1);
        REQUIRE(fs.Exists("file_a.txt"));
        
        fs.Rename("file_a.txt", "file_b.txt");
        REQUIRE(!fs.Exists("file_a.txt"));
        REQUIRE(fs.Exists("file_b.txt"));
        
        fs.Delete("file_b.txt");
        REQUIRE(!fs.Exists("file_b.txt"));
    }
    
    SECTION("Create directory") {
        MemoryFS fs;
        fs.CreateDir("dir1");
        REQUIRE(fs.Exists("dir1"));
    }
}

TEST_CASE("Path normalization", "[filesystem][path]") {
    using namespace zq;
    using namespace zq::fs;
    
    SECTION("Normalize path") {
        MemoryFS fs;
        String result = fs.NormalizePath("a/b/../c");
        REQUIRE(result == "a/c");
    }
    
    SECTION("Absolute path detection") {
        MemoryFS fs;
        REQUIRE(fs.IsAbsolutePath("/absolute/path"));
        REQUIRE(!fs.IsAbsolutePath("relative/path"));
    }
}

TEST_CASE("VirtualFS search paths and file operations", "[filesystem][vfs]") {
    using namespace zq;
    using namespace zq::fs;
    
    // Create a temporary directory structure on disk
    std::string temp_dir = "/tmp/zquake_vfs_test";
    std::filesystem::create_directories(temp_dir + "/subdir");
    
    {
        std::ofstream f(temp_dir + "/hello.txt");
        f << "Hello from disk!";
    }
    {
        std::ofstream f(temp_dir + "/subdir/nested.txt");
        f << "Nested content";
    }
    
    VirtualFS vfs;
    vfs.AddSearchPath(temp_dir);
    
    SECTION("File exists in search path") {
        REQUIRE(vfs.Exists("hello.txt"));
        REQUIRE(vfs.Exists("subdir/nested.txt"));
        REQUIRE(!vfs.Exists("nonexistent.txt"));
    }
    
    SECTION("Read file from search path") {
        auto data = vfs.ReadFileAll("hello.txt");
        REQUIRE(data.has_value());
        std::string str(data->begin(), data->end());
        REQUIRE(str == "Hello from disk!");
    }
    
    SECTION("Read nested file") {
        auto data = vfs.ReadFileAll("subdir/nested.txt");
        REQUIRE(data.has_value());
        std::string str(data->begin(), data->end());
        REQUIRE(str == "Nested content");
    }
    
    SECTION("Write file through VFS") {
        std::string content = "Written by VFS";
        bool ok = vfs.WriteFile("written.txt", content.data(), content.size());
        REQUIRE(ok);
        REQUIRE(vfs.Exists("written.txt"));
        
        auto readback = vfs.ReadFileAll("written.txt");
        REQUIRE(readback.has_value());
        std::string str(readback->begin(), readback->end());
        REQUIRE(str == content);
    }
    
    SECTION("List directory") {
        auto entries = vfs.ListDir("");
        REQUIRE(!entries.empty());
        bool found_hello = false;
        for (const auto& e : entries) {
            if (e.name == "hello.txt") found_hello = true;
        }
        REQUIRE(found_hello);
    }
    
    // Cleanup
    std::filesystem::remove_all(temp_dir);
}
