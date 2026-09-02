#ifndef __UTILS_H__
#define __UTILS_H__

#include "commons.h"
#include "json.hpp"

#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>

// ---------------------------------------------------------------------------
// Every data path in this project (configs, inputs, parameters) is written
// relative to the repository root.  PROJECT_ROOT is baked in by CMake at
// configure time, so the binaries resolve those paths the same way no matter
// which directory they are launched from.
// ---------------------------------------------------------------------------
#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

inline std::string project_path(const std::string& relative_path) {
    if (relative_path.empty() || relative_path.front() == '/') {
        return relative_path;                     // already absolute, use as-is
    }
    return (std::filesystem::path(PROJECT_ROOT) / relative_path).string();
}


void read_next_elements(size_t n, float* buffer, size_t offset, const char* filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::string("Failed to open file: ") + filepath);
    }

    if (buffer == NULL){
        buffer = new float[n];
    }

    size_t pos = 0;
    while (file >> buffer[pos]) {
        if(offset > 0){
            offset--;
            continue;
        }

        pos++;

        if(pos == n){
            break;
        }
    }
}

float reveal_field_element_after_scaling(IntFp x){
    uint64_t clt_x = x.reveal();
    
    int64_t signed_clt_x = (clt_x >= (PR - 1)/2) ? -(PR - clt_x) : clt_x;
    
    return (signed_clt_x * 1.0) / (1LL << FXPSCALE);
}

#endif