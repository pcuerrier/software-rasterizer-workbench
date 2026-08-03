#include "shared.h"

struct Tile
{
    int32_t x;
    int32_t y;
    uint32_t colorARGB;
};

struct TileMap
{
    Tile* tiles;
    int32_t width;
    int32_t height;
};

struct Camera
{
    float x;
    float y;
    float zoom;
};

struct GameState
{
    TileMap tileMap;
    Camera camera;
};

extern "C" __declspec(dllexport) APP_INIT(AppInit)
{
    LOG_INFO(logFn, "Initializing game.");

    // Initialize game state
    GameState* gameState = (GameState*)PushSize(&memory.permanentStorage, sizeof(GameState));
    gameState->tileMap.width = 50;
    gameState->tileMap.height = 50;
    gameState->camera.x = 0.0f;
    gameState->camera.y = 0.0f;
    gameState->camera.zoom = 1.0f;
    size_t tileCount = (size_t)(gameState->tileMap.width * gameState->tileMap.height);
    gameState->tileMap.tiles = (Tile*)PushSize(&memory.permanentStorage, sizeof(Tile) * tileCount);
    for (int32_t y = 0; y < gameState->tileMap.height; ++y)
    {
        for (int32_t x = 0; x < gameState->tileMap.width; ++x)
        {
            Tile* tile = &gameState->tileMap.tiles[y * gameState->tileMap.width + x];
            tile->x = x;
            tile->y = y;
            tile->colorARGB = 0xFF000000 | ((x * 5) << 16) | ((y * 5) << 0); // Gradient color
        }
    }
}

extern "C" __declspec(dllexport) APP_UPDATE(AppUpdate)
{
    GameState* gameState = (GameState*)memory.permanentStorage.base;
    TileMap* tileMap = &gameState->tileMap;
    Camera* camera = &gameState->camera;

    if (userInput.controllers[0].moveUp.endedDown)
    {
        camera->y -= 5.0f; // Move camera up
    }
    if (userInput.controllers[0].moveDown.endedDown)
    {
        camera->y += 5.0f; // Move camera down
    }
    if (userInput.controllers[0].moveLeft.endedDown)
    {
        camera->x -= 5.0f; // Move camera left
    }
    if (userInput.controllers[0].moveRight.endedDown)
    {
        camera->x += 5.0f; // Move camera right
    }
    if (userInput.controllers[0].zoomIn.endedDown)
    {
        camera->zoom *= 1.1f; // Zoom in
    }
    if (userInput.controllers[0].zoomOut.endedDown)
    {
        camera->zoom /= 1.1f; // Zoom out
    }
    const int tileSize = 32; // Size of each tile in pixels

    const float viewW = (float)render_cmds->width / camera->zoom; // Width of the view in world coordinates
    const float viewH = (float)render_cmds->height / camera->zoom; // Height of the view in world coordinates
    const float halfW = viewW * 0.5f;
    const float halfH = viewH * 0.5f;
    const float mapX = (float)(tileMap->width * tileSize);
    const float mapY = (float)(tileMap->height * tileSize);

    if (viewW >= mapX) camera->x = mapX * 0.5f; // Center camera if view is larger than map
    else
    {
        if ((camera->x < halfW)) camera->x = halfW;
        if ((camera->x > mapX - halfW)) camera->x = mapX - halfW;
    }
    
    if (viewH >= mapY) camera->y = mapY * 0.5f; // Center camera if view is larger than map
    else
    {
        if ((camera->y < halfH)) camera->y = halfH;
        if ((camera->y > mapY - halfH)) camera->y = mapY - halfH;
    }

    /*if (userInput.mouse.left.endedDown)
    {
        LOG_INFO(logFn, "Mouse left button pressed at (%.2f, %.2f)", userInput.mouse.x, userInput.mouse.y);
    }
    if (userInput.controllers[0].moveUp.endedDown)
    {
        LOG_INFO(logFn, "Controller 0: Move Up button pressed.");
    }*/
   
    PushRenderCmdClear(render_cmds, 0xFF115588); // Clear to a greenish color
    PushRenderCmdSetCamera(render_cmds, camera->x, camera->y, camera->zoom);

    const float viewLeft = camera->x - viewW / 2.0f;
    const float viewTop = camera->y - viewH / 2.0f;
    int startCol = (int)(viewLeft / (float)tileSize);
    int startRow = (int)(viewTop / (float)tileSize);
    int endCol = (int)((viewLeft + viewW) / tileSize) + 1;
    int endRow = (int)((viewTop + viewH) / tileSize) + 1;

    if (startCol < 0) startCol = 0;
    if (startRow < 0) startRow = 0;
    if (endCol > tileMap->width) endCol = tileMap->width;
    if (endRow > tileMap->height) endRow = tileMap->height;
    for (int32_t y = startRow; y < endRow; ++y)
    {
        for (int32_t x = startCol; x < endCol; ++x)
        {
            Tile* tile = &tileMap->tiles[y * tileMap->width + x];
            int32_t worldX = x * tileSize;
            int32_t worldY = y * tileSize;
            PushRenderCmdRect(render_cmds, worldX, worldY, tileSize, tileSize, tile->colorARGB);
        }
    }
}
