#pragma once
#include <cstdint>
#include <string_view>

struct SDL_Window;
struct SDL_Surface;

namespace zq::render {

class SoftwareRenderer {
public:
    SoftwareRenderer();
    ~SoftwareRenderer();
    
    bool CreateWindow(int width, int height, const char* title);
    void Destroy();
    bool IsValid() const;
    
    void BeginFrame(uint8_t r, uint8_t g, uint8_t b);
    void EndFrame();
    
    void DrawChar(int x, int y, uint8_t ch, uint8_t r, uint8_t g, uint8_t b);
    void DrawText(int x, int y, std::string_view text, uint8_t r, uint8_t g, uint8_t b);
    void DrawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    
private:
    SDL_Window* window_ = nullptr;
    SDL_Surface* surface_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    
    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
};

}