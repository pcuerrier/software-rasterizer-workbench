#include "rasterizer.h"

static void DrawRectangle(void* memory, int w, int h, int pitch,
                          float startX, float startY, float rectWidth, float rectHeight,
                          uint32_t colorARGB)
{
    int minX = (int)startX;
    int minY = (int)startY;
    int maxX = (int)(startX + rectWidth);
    int maxY = (int)(startY + rectHeight);

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

static void FlushRenderCommands(RenderCommandBuffer* cmds, void* pixelBuffer, int w, int h, int pitch)
{
    uint8_t* at  = (uint8_t*)cmds->storage->base;
    uint8_t* end = at + cmds->storage->used;

    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float cameraZoom = 1.0f;

    while (at < end)
    {
        RenderCommandType type = *(RenderCommandType*)at;
        switch (type)
        {
            case RENDER_CMD_CLEAR:
            {
                RenderCmdClear* cmd = (RenderCmdClear*)at;
                DrawRectangle(pixelBuffer, w, h, pitch, 0.0f, 0.0f, (float)w, (float)h, cmd->colorARGB);
                at += sizeof(RenderCmdClear);
            } break;

            case RENDER_CMD_RECT:
            {
                RenderCmdRect* cmd = (RenderCmdRect*)at;
                float screenX = ((float)cmd->x - cameraX) * cameraZoom + (float)w * 0.5f; // Center the camera
                float screenY = ((float)cmd->y - cameraY) * cameraZoom + (float)h * 0.5f;
                float rectW   = (float)cmd->w * cameraZoom;
                float rectH   = (float)cmd->h * cameraZoom;
                DrawRectangle(pixelBuffer, w, h, pitch,
                              screenX, screenY, rectW, rectH, cmd->colorARGB);
                at += sizeof(RenderCmdRect);
            } break;

            case RENDER_CMD_SET_CAMERA:
            {
                RenderCmdSetCamera* cmd = (RenderCmdSetCamera*)at;
                cameraX = cmd->x;
                cameraY = cmd->y;
                cameraZoom = cmd->zoom;
                at += sizeof(RenderCmdSetCamera);
            } break;

            default:
            {
                at = end;
            } break;
        }
    }
}