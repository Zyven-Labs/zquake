#include "render/software_renderer.hpp"
#include "render/font8x8.hpp"
#include <SDL.h>

namespace zq::render {

SoftwareRenderer::SoftwareRenderer() = default;

SoftwareRenderer::~SoftwareRenderer() {
    Destroy();
}

bool SoftwareRenderer::CreateWindow(int width, int height, const char* title) {
    window_ = SDL_CreateWindow(title,
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, SDL_WINDOW_SHOWN);
    if (!window_) return false;
    
    surface_ = SDL_GetWindowSurface(window_);
    if (!surface_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    
    width_ = width;
    height_ = height;
    return true;
}

void SoftwareRenderer::Destroy() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        surface_ = nullptr;
        width_ = 0;
        height_ = 0;
    }
}

bool SoftwareRenderer::IsValid() const {
    return window_ != nullptr && surface_ != nullptr;
}

void SoftwareRenderer::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    if (a == 0) return;
    
    // Lock surface if needed
    if (SDL_MUSTLOCK(surface_)) SDL_LockSurface(surface_);
    
    int bpp = surface_->format->BytesPerPixel;
    uint8_t* pixel = static_cast<uint8_t*>(surface_->pixels) + y * surface_->pitch + x * bpp;
    
    uint32_t color = SDL_MapRGBA(surface_->format, r, g, b, a);
    
    if (a < 255) {
        // Alpha blend
        uint32_t existing = *reinterpret_cast<uint32_t*>(pixel);
        uint8_t er, eg, eb, ea;
        SDL_GetRGBA(existing, surface_->format, &er, &eg, &eb, &ea);
        r = (r * a + er * (255 - a)) / 255;
        g = (g * a + eg * (255 - a)) / 255;
        b = (b * a + eb * (255 - a)) / 255;
        color = SDL_MapRGBA(surface_->format, r, g, b, 255);
    }
    
    switch (bpp) {
        case 1: *pixel = (uint8_t)color; break;
        case 2: *(uint16_t*)pixel = (uint16_t)color; break;
        case 3:
            pixel[0] = (color >> 0) & 0xFF;
            pixel[1] = (color >> 8) & 0xFF;
            pixel[2] = (color >> 16) & 0xFF;
            break;
        case 4: *(uint32_t*)pixel = color; break;
    }
    
    if (SDL_MUSTLOCK(surface_)) SDL_UnlockSurface(surface_);
}

void SoftwareRenderer::BeginFrame(uint8_t r, uint8_t g, uint8_t b) {
    // Clear to solid color
    uint32_t color = SDL_MapRGBA(surface_->format, r, g, b, 255);
    SDL_FillRect(surface_, nullptr, color);
}

void SoftwareRenderer::EndFrame() {
    SDL_UpdateWindowSurface(window_);
}

void SoftwareRenderer::DrawChar(int x, int y, uint8_t ch, uint8_t r, uint8_t g, uint8_t b) {
    if (ch < 32 || ch > 126) return;
    int idx = ch - 32;
    
    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8_basic[idx][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                SetPixel(x + col, y + row, r, g, b);
            }
        }
    }
}

void SoftwareRenderer::DrawText(int x, int y, std::string_view text, uint8_t r, uint8_t g, uint8_t b) {
    int cx = x;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\n') {
            cx = x;
            y += 10;
            continue;
        }
        DrawChar(cx, y, static_cast<uint8_t>(text[i]), r, g, b);
        cx += 8;
    }
}

void SoftwareRenderer::DrawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            SetPixel(px, py, r, g, b, a);
        }
    }
}

} // namespace zq::render