#include "vtex_decoder.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static uint32_t readU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t readU16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static std::vector<uint8_t> lz4Decode(const uint8_t* src, size_t srcLen, size_t outSize) {
    std::vector<uint8_t> out;
    out.reserve(outSize);
    size_t i = 0;
    while (i < srcLen) {
        uint8_t token = src[i++];
        int lit = token >> 4;
        if (lit == 15) {
            while (i < srcLen) {
                uint8_t b = src[i++];
                lit += b;
                if (b != 255) break;
            }
        }
        for (int k = 0; k < lit && i < srcLen; k++) out.push_back(src[i++]);
        if (i >= srcLen) break;
        uint16_t off = (uint16_t)(src[i] | (src[i + 1] << 8)); i += 2;
        int mlen = token & 15;
        if (mlen == 15) {
            while (i < srcLen) {
                uint8_t b = src[i++];
                mlen += b;
                if (b != 255) break;
            }
        }
        mlen += 4;
        int start = (int)out.size() - off;
        if (off == 0 || start < 0) throw std::runtime_error("bad LZ4");
        for (int k = 0; k < mlen; k++) out.push_back(out[start + k]);
    }
    return out;
}

static void rgb565(uint16_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    uint8_t r5 = (v >> 11) & 31;
    uint8_t g6 = (v >> 5) & 63;
    uint8_t b5 = v & 31;
    r = (r5 << 3) | (r5 >> 2);
    g = (g6 << 2) | (g6 >> 4);
    b = (b5 << 3) | (b5 >> 2);
}

static std::vector<uint8_t> decodeBC1(const uint8_t* data, int w, int h) {
    std::vector<uint8_t> px(w * h * 4, 0);
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    size_t pos = 0;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            uint16_t c0 = readU16(data + pos);
            uint16_t c1 = readU16(data + pos + 2);
            uint32_t bits = readU32(data + pos + 4);
            pos += 8;
            uint8_t col[4][4];
            rgb565(c0, col[0][0], col[0][1], col[0][2]); col[0][3] = 255;
            rgb565(c1, col[1][0], col[1][1], col[1][2]); col[1][3] = 255;
            if (c0 > c1) {
                for (int k = 0; k < 3; k++) {
                    col[2][k] = (uint8_t)((2 * col[0][k] + col[1][k]) / 3);
                    col[3][k] = (uint8_t)((col[0][k] + 2 * col[1][k]) / 3);
                }
                col[2][3] = col[3][3] = 255;
            }
            else {
                for (int k = 0; k < 3; k++)
                    col[2][k] = (uint8_t)((col[0][k] + col[1][k]) / 2);
                col[2][3] = 255;
                col[3][0] = col[3][1] = col[3][2] = col[3][3] = 0;
            }
            for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {
                int x = bx * 4 + i, y = by * 4 + j;
                if (x < w && y < h) {
                    int idx = (bits >> (2 * (4 * j + i))) & 3;
                    size_t o = ((size_t)y * w + x) * 4;
                    px[o] = col[idx][0]; px[o + 1] = col[idx][1];
                    px[o + 2] = col[idx][2]; px[o + 3] = col[idx][3];
                }
            }
        }
    }
    return px;
}

static std::vector<uint8_t> decodeBC3(const uint8_t* data, int w, int h) {
    std::vector<uint8_t> px(w * h * 4, 0);
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    size_t pos = 0;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            uint8_t a0 = data[pos], a1 = data[pos + 1];
            uint64_t abits = 0;
            for (int k = 0; k < 6; k++) abits |= (uint64_t)data[pos + 2 + k] << (8 * k);
            uint8_t alphas[8];
            alphas[0] = a0; alphas[1] = a1;
            if (a0 > a1) {
                for (int k = 1; k <= 6; k++) alphas[k + 1] = (uint8_t)(((7 - k) * a0 + k * a1) / 7);
            }
            else {
                for (int k = 1; k <= 4; k++) alphas[k + 1] = (uint8_t)(((5 - k) * a0 + k * a1) / 5);
                alphas[6] = 0; alphas[7] = 255;
            }
            uint16_t c0 = readU16(data + pos + 8);
            uint16_t c1 = readU16(data + pos + 10);
            uint32_t bits = readU32(data + pos + 12);
            pos += 16;
            uint8_t col[4][3];
            rgb565(c0, col[0][0], col[0][1], col[0][2]);
            rgb565(c1, col[1][0], col[1][1], col[1][2]);
            for (int k = 0; k < 3; k++) {
                col[2][k] = (uint8_t)((2 * col[0][k] + col[1][k]) / 3);
                col[3][k] = (uint8_t)((col[0][k] + 2 * col[1][k]) / 3);
            }
            for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {
                int x = bx * 4 + i, y = by * 4 + j;
                if (x < w && y < h) {
                    int idx = (bits >> (2 * (4 * j + i))) & 3;
                    int ai = (int)((abits >> (3 * (4 * j + i))) & 7);
                    size_t o = ((size_t)y * w + x) * 4;
                    px[o] = col[idx][0]; px[o + 1] = col[idx][1];
                    px[o + 2] = col[idx][2]; px[o + 3] = alphas[ai];
                }
            }
        }
    }
    return px;
}

DecodedTexture VTEXDecoder::decode(const std::vector<uint8_t>& data) {
    DecodedTexture out;
    if (data.size() < 16) return out;
    const uint8_t* d = data.data();
    size_t sz = data.size();

    uint32_t blockOff = readU32(d + 8);
    uint32_t blockCnt = readU32(d + 12);
    size_t pos = 8 + blockOff;
    size_t dataOff = 0;

    for (uint32_t b = 0; b < blockCnt && pos + 12 <= sz; b++) {
        if (memcmp(d + pos, "DATA", 4) == 0) {
            uint32_t off = readU32(d + pos + 4);
            dataOff = pos + 4 + off;
        }
        pos += 12;
    }
    if (dataOff == 0 || dataOff + 40 > sz) return out;

    const uint8_t* h = d + dataOff;
    uint16_t width = readU16(h + 20);
    uint16_t height = readU16(h + 22);
    uint8_t  fmt = h[26];
    uint32_t extraOff = readU32(h + 32);
    uint32_t extraCnt = readU32(h + 36);

    if (width == 0 || height == 0 || width > 8192 || height > 8192) return out;

    if (fmt >= 15 && fmt <= 18) {
        size_t imgStart = sz > 1024 ? sz - 1024 : 0;
        for (size_t i = 0; i < sz - 4; ++i) {
            if ((d[i] == 0x89 && d[i + 1] == 0x50 && d[i + 2] == 0x4E && d[i + 3] == 0x47) ||
                (d[i] == 0xFF && d[i + 1] == 0xD8 && d[i + 2] == 0xFF)) {
                imgStart = i;
                break;
            }
        }
        if (imgStart < sz) {
            int w = 0, h = 0, channels = 0;
            unsigned char* pixels = stbi_load_from_memory(d + imgStart, (int)(sz - imgStart), &w, &h, &channels, 4);
            if (pixels) {
                out.width = w;
                out.height = h;
                out.rgba.assign(pixels, pixels + (size_t)w * h * 4);
                out.valid = true;
                stbi_image_free(pixels);
                return out;
            }
        }
        return out;
    }

    bool isLz4 = false;
    std::vector<int32_t> comp;
    size_t p = dataOff + 32 + extraOff;
    for (uint32_t k = 0; k < extraCnt && p + 12 <= sz; k++) {
        uint32_t t = readU32(d + p);
        uint32_t o = readU32(d + p + 4);
        if (t == 3) {
            size_t base = p + 4 + o;
            if (base + 12 <= sz) {
                isLz4 = (readU32(d + base) == 1);
                uint32_t mo = readU32(d + base + 4);
                uint32_t mc = readU32(d + base + 8);
                for (uint32_t j = 0; j < mc && base + 12 + mo + j * 4 + 4 <= sz; j++)
                    comp.push_back((int32_t)readU32(d + base + 12 + mo + j * 4));
            }
        }
        p += 12;
    }

    int bpp = 4, blockBytes = 0;
    switch (fmt) {
    case 1:  blockBytes = 8;  break;  // DXT1
    case 2:  blockBytes = 16; break;  // DXT5
    case 3:  bpp = 1; break;          // I8
    case 4:  bpp = 4; break;          // RGBA8888
    case 20: blockBytes = 16; break;  // BC7
    case 22: bpp = 2; break;          // IA88
    case 28: bpp = 4; break;          // BGRA8888
    default: return out;
    }

    size_t mip0 = 0;
    if (blockBytes > 0) {
        int bw = (width + 3) / 4;
        int bh = (height + 3) / 4;
        mip0 = (size_t)bw * bh * blockBytes;
    }
    else {
        mip0 = (size_t)width * height * bpp;
    }

    size_t readSize = mip0;
    bool needsLz4 = false;
    if (!comp.empty() && isLz4 && comp[0] > 0 && (size_t)comp[0] < mip0) {
        readSize = comp[0]; needsLz4 = true;
    }
    if (readSize > sz) return out;
    size_t start = sz - readSize;

    std::vector<uint8_t> raw;
    if (needsLz4) {
        try { raw = lz4Decode(d + start, readSize, mip0); }
        catch (...) { return out; }
    }
    else {
        raw.assign(d + start, d + sz);
    }

    std::vector<uint8_t> rgba;

    if (fmt == 1) {
        rgba = decodeBC1(raw.data(), width, height);
    }
    else if (fmt == 2) {
        rgba = decodeBC3(raw.data(), width, height);
    }
    else if (fmt == 20) {
        // BC7 - decode block by block (bcdec v1+ decodes one 4x4 block at a time)
        int bw = (width + 3) / 4;
        int bh = (height + 3) / 4;
        int bcW = bw * 4;
        int bcH = bh * 4;
        std::vector<uint8_t> decoded((size_t)bcW * bcH * 4);

        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                const uint8_t* src = raw.data() + ((size_t)by * bw + bx) * 16;
                uint8_t* dst = decoded.data() + ((size_t)by * 4 * bcW + bx * 4) * 4;
                bcdec_bc7(src, dst, bcW * 4);
            }
        }

        if (bcW != width || bcH != height) {
            rgba.resize((size_t)width * height * 4);
            for (int y = 0; y < height; ++y) {
                memcpy(&rgba[(size_t)y * width * 4],
                    &decoded[(size_t)y * bcW * 4],
                    (size_t)width * 4);
            }
        }
        else {
            rgba = std::move(decoded);
        }
    }
    else if (fmt == 4) {
        size_t N = std::min(raw.size(), (size_t)width * height * 4);
        rgba.assign(raw.begin(), raw.begin() + N);
    }
    else if (fmt == 28) {
        size_t N = std::min(raw.size(), (size_t)width * height * 4);
        rgba.resize(N);
        for (size_t i = 0; i + 3 < N; i += 4) {
            rgba[i] = raw[i + 2];
            rgba[i + 1] = raw[i + 1];
            rgba[i + 2] = raw[i];
            rgba[i + 3] = raw[i + 3];
        }
    }
    else if (fmt == 3) {
        rgba.resize((size_t)width * height * 4);
        for (int i = 0; i < width * height; i++) {
            uint8_t v = raw[i];
            rgba[i * 4] = v; rgba[i * 4 + 1] = v; rgba[i * 4 + 2] = v; rgba[i * 4 + 3] = 255;
        }
    }
    else if (fmt == 22) {
        rgba.resize((size_t)width * height * 4);
        for (int i = 0; i < width * height; i++) {
            uint8_t v = raw[i * 2], a = raw[i * 2 + 1];
            rgba[i * 4] = v; rgba[i * 4 + 1] = v; rgba[i * 4 + 2] = v; rgba[i * 4 + 3] = a;
        }
    }
    else return out;

    if (rgba.size() < (size_t)width * height * 4) return out;
    out.width = width; out.height = height;
    out.rgba = std::move(rgba);
    out.valid = true;
    return out;
}