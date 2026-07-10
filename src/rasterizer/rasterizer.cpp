#include "rasterizer.h"

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

static void DrawTilemapLayer(void* memory, int w, int h, int pitch,
                             uint32_t* tilesetPixels, int tilesetWidth, int tilesetHeight, int tileSize,
                             uint8_t* tiles, int mapWidth, int mapHeight,
                             float cameraX, float cameraY)
{
    // Calculate the range of tiles to draw based on the camera position and render dimensions
    const int camX        = (int)cameraX;
    const int camY        = (int)cameraY;
    const int tilesPerRow = tilesetWidth / tileSize;

    int startCol = camX / tileSize;
    int startRow = camY / tileSize;
    int endCol   = (camX + w) / tileSize + 1;
    int endRow   = (camY + h) / tileSize + 1;

    // Clamp to the bounds of the tilemap
    if (startCol < 0) startCol = 0;
    if (startRow < 0) startRow = 0;
    if (endCol > mapWidth) endCol = mapWidth;
    if (endRow > mapHeight) endRow = mapHeight;

    for (int row = startRow; row < endRow; ++row)
    {
        for (int col = startCol; col < endCol; ++col)
        {
            uint8_t tileIndex = tiles[row * mapWidth + col];
            if (tileIndex == 0) continue; // Skip empty tiles

            // Calculate the source rectangle in the tileset
            int srcX = (tileIndex % tilesPerRow) * tileSize;
            int srcY = (tileIndex / tilesPerRow) * tileSize;

            // Calculate the destination position on the screen
            float destX = (float)(col * tileSize) - cameraX;
            float destY = (float)(row * tileSize) - cameraY;

            // Draw the tile
            for (int y = 0; y < tileSize; ++y)
            {
                for (int x = 0; x < tileSize; ++x)
                {
                    uint32_t colorARGB = tilesetPixels[(srcY + y) * tilesetWidth + (srcX + x)];
                    if ((colorARGB & 0xFF000000) == 0) continue; // Skip transparent pixels

                    int pixelX = (int)destX + x;
                    int pixelY = (int)destY + y;

                    if (pixelX >= 0 && pixelX < w && pixelY >= 0 && pixelY < h)
                    {
                        uint32_t* pixel = (uint32_t*)((uint8_t*)memory + (pixelX * 4) + (pixelY * pitch));
                        *pixel = colorARGB;
                    }
                }
            }
        }
    }
}

static void FlushRenderCommands(RenderCommandBuffer* cmds, void* pixelBuffer, int w, int h, int pitch)
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
            } break;
            case RENDER_CMD_RECT:
            {
                RenderCmdRect* cmd = (RenderCmdRect*)at;
                DrawRectangle(pixelBuffer, w, h, pitch,
                            (float)cmd->x, (float)cmd->y, cmd->w, cmd->h, cmd->colorARGB);
                at += sizeof(RenderCmdRect);
            } break;
            case RENDER_CMD_TILEMAP_LAYER:
            {
                RenderCmdTilemapLayer* cmd = (RenderCmdTilemapLayer*)at;
                DrawTilemapLayer(pixelBuffer, w, h, pitch,
                                 cmd->tilesetPixels, cmd->tilesetWidth, cmd->tilesetHeight, cmd->tileSize,
                                 cmd->tiles, cmd->mapWidth, cmd->mapHeight,
                                 cmd->cameraX, cmd->cameraY);
                at += sizeof(RenderCmdTilemapLayer);
            } break;
            default:
            {
                at = end;
            } break;
        }
    }
}