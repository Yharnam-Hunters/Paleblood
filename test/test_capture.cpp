/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

#include "runtime/capture.h"
#include "runtime/recorder.hpp"

unsigned char *rt_image;

static int failures;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static std::string read_file(const std::string &path)
{
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main()
{
    char temp[] = "/tmp/pb-capture-test.XXXXXX";
    char *root = mkdtemp(temp);
    CHECK(root != nullptr);
    if (!root) return 1;
    setenv("BB_CAPTURE_DIR", root, 1);
    setenv("BB_CAPTURE_RUN", "deterministic-run_1", 1);
    CHECK(std::strcmp(rt_capture_run_id(), "deterministic-run_1") == 0);
    CHECK(rt_capture_begin("capture_writer_test") == 0);

    const std::string writer_dir = std::string(root) + "/capture_writer_test";
    const std::string case_path = writer_dir + "/deterministic-run_1_0000.json";
    rt_capture_write("capture_writer_test", 0, "{\"schema\": 1}");
    CHECK(read_file(case_path) == "{\"schema\": 1}\n");
    struct stat st {};
    CHECK(stat(case_path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    rt_capture_write("capture_writer_test", 0, "{\"schema\": 1, \"overwrite\": true}");
    CHECK(read_file(case_path) == "{\"schema\": 1}\n");
    CHECK(rt_capture_begin("../escape") == -1);

    rt_image = nullptr;
    char low[16] {}, high[16] {};
    char *first = reinterpret_cast<uintptr_t>(low) > reinterpret_cast<uintptr_t>(high) ? low : high;
    char *second = first == low ? high : low;
    std::memset(first, 0x11, 4);
    std::memset(second, 0x22, 4);
    rt::Recorder recorder;
    CHECK(recorder.begin("capture_recorder_test"));
    recorder.bytes(first, 0, 4);
    recorder.bytes(second, 0, 4);
    recorder.write("0x00400000", "\"rdi\": \"buf:b0\"");
    const std::string recorder_path = std::string(root) + "/capture_recorder_test/deterministic-run_1_0000.json";
    const std::string json = read_file(recorder_path);
    CHECK(json.find("\"capture\": {\"function\": \"capture_recorder_test\", \"run_id\": \"deterministic-run_1\"}") != std::string::npos);
    CHECK(json.find("\"b0\": {\"size\": 16}, \"b1\": {\"size\": 16}") != std::string::npos);
    CHECK(json.find("\"bytes\": \"11111111\"") != std::string::npos);
    CHECK(json.find("\"bytes\": \"22222222\"") != std::string::npos);

    const std::string blocked_root = std::string(root) + "/regular-file";
    { std::ofstream f(blocked_root); f << "keep"; }
    setenv("BB_CAPTURE_DIR", blocked_root.c_str(), 1);
    rt_capture_write("capture_writer_test", 77, "{\"no\": \"write\"}");
    CHECK(read_file(blocked_root) == "keep");

    // All files are in this fresh, explicitly named test directory.
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    CHECK(!ec);
    return failures ? 1 : 0;
}
