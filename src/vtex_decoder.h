#pragma once
#include <cstdint>
#include <vector>

struct DecodedTexture {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // 4 bytes per pixel
    bool valid = false;
};

class VTEXDecoder {
public:
    static DecodedTexture decode(const std::vector<uint8_t>& data);
};