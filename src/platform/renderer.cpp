#include "renderer.h"

static void DrawRectangle(void* memory, int w, int h, int pitch,
                          float startX, float startY, int rectWidth, int rectHeight,
                          uint32_t colorARGB)
{
    int minX = (int)startX;
    int minY = (int)startY;
    int maxX = minX + rectWidth;
    int maxY = minY + rectHeight;

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX > w) maxX = w;
    if (maxY > h) maxY = h;

    uint8_t* row = (uint8_t*)memory + (minX * 4) + (minY * pitch);
    for (int y = minY; y < maxY; ++y)
    {
        uint32_t* pixel = (uint32_t*)row;
        for (int x = minX; x < maxX; ++x)
            *pixel++ = colorARGB;
        row += pitch;
    }
}

void FlushRenderCommands(RenderCommandBuffer* cmds, void* pixelBuffer, int w, int h, int pitch)
{
    uint8_t* at  = (uint8_t*)cmds->storage->base;
    uint8_t* end = at + cmds->storage->used;

    while (at < end)
    {
        RenderCommandType type = *(RenderCommandType*)at;
        switch (type)
        {
        case RENDER_CMD_CLEAR:
        {
            RenderCmdClear* cmd = (RenderCmdClear*)at;
            DrawRectangle(pixelBuffer, w, h, pitch, 0.0f, 0.0f, w, h, cmd->colorARGB);
            at += sizeof(RenderCmdClear);
            break;
        }
        case RENDER_CMD_RECT:
        {
            RenderCmdRect* cmd = (RenderCmdRect*)at;
            DrawRectangle(pixelBuffer, w, h, pitch,
                          (float)cmd->x, (float)cmd->y, cmd->w, cmd->h, cmd->colorARGB);
            at += sizeof(RenderCmdRect);
            break;
        }
        default:
            at = end;
            break;
        }
    }
}

void ResizeRenderBuffer(
    SDL_Renderer*  renderer,
    int            newWidth,
    int            newHeight,
    SDL_Texture**  outTexture,
    void**         outPixels,
    int*           outWidth,
    int*           outHeight,
    int*           outPitch)
{
    // Tear down old resources (safe on first call when both are null)
    if (*outTexture)
    {
        SDL_DestroyTexture(*outTexture);
        *outTexture = nullptr;
    }
    if (*outPixels)
    {
        SDL_free(*outPixels);
        *outPixels = nullptr;
    }

    *outWidth  = newWidth;
    *outHeight = newHeight;
    *outPitch  = newWidth * 4; // 4 bytes per pixel (ARGB8888)

    *outPixels  = SDL_malloc((size_t)(newWidth * newHeight * 4));
    *outTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        newWidth, newHeight
    );
    SDL_SetTextureScaleMode(*outTexture, SDL_SCALEMODE_NEAREST);

    PLATFORM_LOG_INFO("Render buffer resized to %dx%d", newWidth, newHeight);
}

void Render(
    SDL_Renderer* renderer,
    SDL_Texture*  texture,
    void*         pixelBuffer,
    int           width,
    int           height,
    int           pitch,
    int           windowWidth,
    int           windowHeight)
{
    void* texPixels = nullptr;
    int   texPitch  = 0;
    if (SDL_LockTexture(texture, NULL, &texPixels, &texPitch))
    {
        uint8_t* src = (uint8_t*)pixelBuffer;
        uint8_t* dst = (uint8_t*)texPixels;
        for (int y = 0; y < height; ++y)
        {
            SDL_memcpy(dst, src, (size_t)(width * 4));
            src += pitch;
            dst += texPitch;
        }
        SDL_UnlockTexture(texture);
    }
    int scale_x = windowWidth / width;      // integer division
    int scale_y = windowHeight / height;
    int scale   = std::min(scale_x, scale_y);
    if (scale < 1) scale = 1;                 // window smaller than internal res
    SDL_FRect dst = {
        (float)(windowWidth - width * scale) / 2.0f,
        (float)(windowHeight - height * scale) / 2.0f,
        (float)(width * scale),
        (float)(height * scale)
    };
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}