#include "shared.h"

#include <math.h>   // sinf(), floorf()
#include <stdio.h>  // snprintf() — used by the LOG_* macros in shared.h
#include "font8x8.h"

// ---------------------------------------------------------------------------------------------------------------------
//  Tile & map constants
// ---------------------------------------------------------------------------------------------------------------------

#define TILE_W       32    // Isometric diamond width in pixels
#define TILE_H       16    // Isometric diamond height in pixels
#define HALF_TILE_W  16    // TILE_W / 2
#define HALF_TILE_H   8    // TILE_H / 2
#define TILE_SPRITE  32    // Source sprite size (width and height)

// Tile IDs
enum TileID : uint8_t
{
    TILE_VOID           = 0,   // Not drawn
    TILE_FLOOR          = 1,   // Walkable stone floor
    TILE_WALL_SE        = 2,   // Wall — SE face (two faces visible, back-left corner)
    TILE_WALL_SW        = 3,   // Wall — SW face (right edge)
    TILE_WALL_NE        = 4,   // Wall — NE face (left edge)
    TILE_WALL_NW        = 5,   // Wall — NW face (front, close to camera)
    TILE_WALL_CORNER_N  = 6,   // Wall — north corner
    TILE_WALL_CORNER_E  = 7,   // Wall — east corner
    TILE_WALL_CORNER_W  = 8,   // Wall — west corner
    TILE_WALL_CORNER_S  = 9,   // Wall — south corner
    TILE_CORRIDOR_FLOOR = 10,  // Walkable corridor floor variant
    TILE_DOOR_CLOSED    = 11,  // Impassable closed door
    TILE_DOOR_OPEN      = 12,  // Passable open door
    TILE_STAIR_DOWN     = 13,  // Staircase leading down
    TILE_STAIR_UP       = 14,  // Staircase leading up
    TILE_CHEST          = 15,  // Loot container (walkable)
    TILE_WATER          = 16,  // Impassable water terrain
    TILE_LAVA           = 17,  // Impassable lava terrain
    TILE_COUNT          = 18
};

// Layer indices
#define MAP_LAYERS    3
#define LAYER_FLOOR   0   // Base terrain: floor, water, lava...
#define LAYER_OBJECT  1   // Walls, doors, furniture
#define LAYER_OVERLAY 2   // Roof tiles, decals — drawn on top of units


// Colors (0xAARRGGBB)
#define COLOR_BG         0xFF1A1A1A
#define COLOR_HIGHLIGHT  0xFFFFFF55

#define OSD_REFRESH_INTERVAL 60

// ---------------------------------------------------------------------------------------------------------------------
//  Tile registry
// ---------------------------------------------------------------------------------------------------------------------

struct TileDef
{
    uint8_t spriteCol;  // Column in the tileset grid
    uint8_t spriteRow;  // Row in the tileset grid
    int8_t  yOffset;    // Pixels to raise sprite above floor plane (TILE_SPRITE/2 for walls)
    bool    walkable;   // Units can move through this tile
    bool    opaque;     // Blocks line-of-sight (reserved for future fog-of-war / lighting)
};

// Indexed directly by TileID.  TILE_COUNT static_assert below enforces alignment.
static const TileDef s_TileDefs[] =
{
    //                            col  row  yOff           walk   opaque
    /* TILE_VOID          */ {  0,   0,   0,             false, false },
    /* TILE_FLOOR         */ {  0,   0,   0,             true,  false },
    /* TILE_WALL_SE       */ {  0,   2,   TILE_SPRITE/2, false, true  },
    /* TILE_WALL_SW       */ {  1,   2,   TILE_SPRITE/2, false, true  },
    /* TILE_WALL_NE       */ {  0,   3,   TILE_SPRITE/2, false, true  },
    /* TILE_WALL_NW       */ {  1,   3,   TILE_SPRITE/2, false, true  },
    /* TILE_WALL_CORNER_N */ {  2,   2,   TILE_SPRITE/2, false, true  },
    /* TILE_WALL_CORNER_E */ {  3,   2,   TILE_SPRITE/2, false, true  },
    /* TILE_WALL_CORNER_W */ {  2,   3,   TILE_SPRITE/2, false, true  },
    /* TILE_WALL_CORNER_S */ {  3,   3,   TILE_SPRITE/2, false, true  },
    /* TILE_CORRIDOR_FLOOR */ { 0,   4,   0,             true,  false },
    /* TILE_DOOR_CLOSED    */ { 1,   4,   0,             false, true  },
    /* TILE_DOOR_OPEN      */ { 2,   4,   0,             true,  false },
    /* TILE_STAIR_DOWN     */ { 3,   4,   0,             true,  false },
    /* TILE_STAIR_UP       */ { 4,   4,   0,             true,  false },
    /* TILE_CHEST          */ { 5,   4,   0,             true,  false },
    /* TILE_WATER          */ { 6,   4,   0,             false, false },
    /* TILE_LAVA           */ { 7,   4,   0,             false, false },
};
static_assert(
    sizeof(s_TileDefs) / sizeof(s_TileDefs[0]) == TILE_COUNT,
    "s_TileDefs entry count does not match TileID::TILE_COUNT"
);

// ---------------------------------------------------------------------------------------------------------------------
//  Data structures
// ---------------------------------------------------------------------------------------------------------------------

struct Player
{
    int32_t tileX;
    int32_t tileY;
    int32_t hp;
    int32_t maxHp;
    int32_t attack;
    int32_t defense;
    int32_t level;
    int32_t xp;
    int32_t xpToNextLevel;
};

// Camera stores the screen position of tile (0,0)'s diamond apex.
struct Camera
{
    float x;
    float y;
};

struct GameState
{
    bool    isInitialized;
    bool    isCameraInitialized;
    Player  player;
    Camera  camera;
    Map     currentMap;

    uint64_t tSineIndex;

    int  osdRefreshCounter;
    char osdFpsText[64];
};

// ---------------------------------------------------------------------------------------------------------------------
//  Isometric coordinate conversion
// ---------------------------------------------------------------------------------------------------------------------

// Returns the screen X of the tile's diamond apex (top-center point).
inline int TileToScreenX(int tx, int ty, float camX)
{
    return (int)(camX + (float)(tx - ty) * HALF_TILE_W);
}

// Returns the screen Y of the tile's diamond apex (top-center point).
inline int TileToScreenY(int tx, int ty, float camY)
{
    return (int)(camY + (float)(tx + ty) * HALF_TILE_H);
}

// Converts a screen pixel coordinate to the nearest tile coordinate.
// Used for mouse hover picking.
inline void ScreenToTile(float sx, float sy, float camX, float camY,
                          int* outTX, int* outTY)
{
    float a = (sx - camX) / (float)HALF_TILE_W;
    float b = (sy - camY) / (float)HALF_TILE_H;
    *outTX = (int)floorf((a + b) * 0.5f);
    *outTY = (int)floorf((b - a) * 0.5f);
}

// ---------------------------------------------------------------------------------------------------------------------
//  Core rendering helpers (unchanged from original)
// ---------------------------------------------------------------------------------------------------------------------

void DrawRectangle(GameOffscreenBuffer* buffer, float startX, float startY,
                   int rectWidth, int rectHeight, uint32_t colorARGB)
{
    int minX = (int)startX;
    int minY = (int)startY;
    int maxX = minX + rectWidth;
    int maxY = minY + rectHeight;

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX > buffer->width)  maxX = buffer->width;
    if (maxY > buffer->height) maxY = buffer->height;

    uint8_t* row = (uint8_t*)buffer->memory + (minX * 4) + (minY * buffer->pitch);
    for (int y = minY; y < maxY; ++y)
    {
        uint32_t* pixel = (uint32_t*)row;
        for (int x = minX; x < maxX; ++x)
            *pixel++ = colorARGB;
        row += buffer->pitch;
    }
}

// Blit a rectangular sub-region from a spritesheet with alpha blending.
void DrawBitmapRegion(GameOffscreenBuffer* buffer, LoadedBitmap* bitmap,
                      int srcX, int srcY, int regionWidth, int regionHeight,
                      float startX, float startY)
{
    if (!bitmap->pixels) return;

    int minX = (int)startX;
    int minY = (int)startY;
    int maxX = minX + regionWidth;
    int maxY = minY + regionHeight;

    int sourceOffsetX = srcX;
    int sourceOffsetY = srcY;

    if (minX < 0) { sourceOffsetX += -minX; minX = 0; }
    if (minY < 0) { sourceOffsetY += -minY; minY = 0; }
    if (maxX > buffer->width)  maxX = buffer->width;
    if (maxY > buffer->height) maxY = buffer->height;

    uint32_t* sourceRow = bitmap->pixels + (sourceOffsetY * bitmap->width) + sourceOffsetX;
    uint8_t*  destRow   = (uint8_t*)buffer->memory + (minX * 4) + (minY * buffer->pitch);

    for (int y = minY; y < maxY; ++y)
    {
        uint32_t* destPixelPtr = (uint32_t*)destRow;
        uint32_t* sourcePixel  = sourceRow;

        for (int x = minX; x < maxX; ++x)
        {
            uint32_t srcPixel = *sourcePixel;

            // 1. Extract source alpha and normalise to 0.0 -> 1.0
            float srcA = (float)((srcPixel >> 24) & 0xFF) / 255.0f;

            if (srcA > 0.0f)
            {
                // 2. Extract source RGB channels
                float srcR = (float)((srcPixel >> 16) & 0xFF);
                float srcG = (float)((srcPixel >> 8)  & 0xFF);
                float srcB = (float)((srcPixel >> 0)  & 0xFF);

                // 3. Extract destination RGB channels
                uint32_t destPixel = *destPixelPtr;
                float destR = (float)((destPixel >> 16) & 0xFF);
                float destG = (float)((destPixel >> 8)  & 0xFF);
                float destB = (float)((destPixel >> 0)  & 0xFF);

                // 4. Lerp using alpha
                float invA   = 1.0f - srcA;
                float finalR = (srcR * srcA) + (destR * invA);
                float finalG = (srcG * srcA) + (destG * invA);
                float finalB = (srcB * srcA) + (destB * invA);

                // 5. Pack and write
                *destPixelPtr = (255u << 24) |
                                ((uint32_t)finalR << 16) |
                                ((uint32_t)finalG << 8)  |
                                ((uint32_t)finalB);
            }

            destPixelPtr++;
            sourcePixel++;
        }

        destRow   += buffer->pitch;
        sourceRow += bitmap->width;
    }
}

// Draw a null-terminated ASCII string using the 8x8 bitmap font.
void DrawDebugText(GameOffscreenBuffer* buffer, int startX, int startY,
                   uint32_t colorARGB, const char* text, int scale = 2)
{
    if (!text || scale < 1) return;

    int cursorX = startX;

    for (const char* c = text; *c != '\0'; ++c)
    {
        int ch = (unsigned char)*c;
        if (ch < 32 || ch > 127) ch = '?';

        for (int row = 0; row < 8; ++row)
        {
            uint8_t rowBits = FONT8X8_DATA[ch - 32][row];

            for (int col = 0; col < 8; ++col)
            {
                if (!(rowBits & (1 << col))) continue;

                for (int sy = 0; sy < scale; ++sy)
                {
                    int destY = startY + (row * scale) + sy;
                    if (destY < 0 || destY >= buffer->height) continue;

                    for (int sx = 0; sx < scale; ++sx)
                    {
                        int destX = cursorX + (col * scale) + sx;
                        if (destX < 0 || destX >= buffer->width) continue;

                        uint32_t* pixel = (uint32_t*)((uint8_t*)buffer->memory
                                          + (destY * buffer->pitch) + (destX * 4));
                        *pixel = colorARGB;
                    }
                }
            }
        }

        cursorX += (8 * scale) + scale;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  Isometric tile drawing
// ---------------------------------------------------------------------------------------------------------------------

// Draw the outline only of an isometric diamond — used for the hover highlight.
void DrawIsoOutline(GameOffscreenBuffer* buffer, int cx, int cy, uint32_t color)
{
    for (int r = 0; r < TILE_H; ++r)
    {
        int hw = (r <= HALF_TILE_H)
            ? (r * HALF_TILE_W / HALF_TILE_H)
            : ((TILE_H - r) * HALF_TILE_W / HALF_TILE_H);
        if (hw <= 0) continue;

        // Left edge
        DrawRectangle(buffer, (float)(cx - hw), (float)(cy + r), 2, 1, color);
        // Right edge (only if non-trivial width)
        if (hw > 1)
            DrawRectangle(buffer, (float)(cx + hw - 1), (float)(cy + r), 2, 1, color);
    }
}

// Draws a single TILE_SPRITE x TILE_SPRITE tile from the spritesheet.
// tileCol and tileRow are 0-based indices into the tileset grid.
// (sx, sy) is the screen position of the tile's diamond apex (top-center point).
void DrawIsoTile(GameOffscreenBuffer* buffer, LoadedBitmap* tileset,
                 int tileCol, int tileRow, int sx, int sy)
{
    int srcX = tileCol * TILE_SPRITE;
    int srcY = tileRow * TILE_SPRITE;
    DrawBitmapRegion(buffer, tileset, srcX, srcY, TILE_SPRITE, TILE_SPRITE,
                     (float)(sx - HALF_TILE_W), (float)sy);
}

// Maps a tile ID to its spritesheet location via s_TileDefs and blits it.
// Returns immediately for TILE_VOID so callers need no guard.
// The yOffset from the tile definition is applied here — callers always pass a plain sy.
static void DrawTileById(GameOffscreenBuffer* buffer, LoadedBitmap* tileset,
                         TileID tileId, int sx, int sy)
{
    if (tileId == TILE_VOID) return;
    const TileDef& def = s_TileDefs[tileId];
    DrawIsoTile(buffer, tileset, def.spriteCol, def.spriteRow, sx, sy - def.yOffset);
}

// ---------------------------------------------------------------------------------------------------------------------
//  Tilemap helpers
// ---------------------------------------------------------------------------------------------------------------------

// Short aliases for the static map data below — undefined immediately after.
// Wall names (SE/SW/NE/NW) match the direction the face points outward.
#define _   TILE_VOID
#define F   TILE_FLOOR
#define SE  TILE_WALL_SE   // two faces visible — top edge / corners
#define SW  TILE_WALL_SW   // right edge wall
#define NE  TILE_WALL_NE   // left edge wall
#define NW  TILE_WALL_NW   // front/bottom wall — closest to camera
#define NC  TILE_WALL_CORNER_N // north corner piece
#define EC  TILE_WALL_CORNER_E // east corner piece
#define WC  TILE_WALL_CORNER_W // west corner piece
#define SC  TILE_WALL_CORNER_S // south corner piece

// Floor is solid everywhere; cluster centres (5,5) and (10,10) are void on all layers.
static const uint8_t s_TestFloor[] = {
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  0
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  1
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  2
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  3
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  4
    F,F,F,F, F,_,F,F, F,F,F,F, F,F,F,F,  // row  5  (5,5) hollow centre
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  6
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  7
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  8
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row  9
    F,F,F,F, F,F,F,F, F,F,_,F, F,F,F,F,  // row 10  (10,10) hollow centre
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row 11
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row 12
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row 13
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row 14
    F,F,F,F, F,F,F,F, F,F,F,F, F,F,F,F,  // row 15
};

// Wall variants per cell edge:
//   SE = top edge & corners   SW = right edge
//   NE = left edge            NW = bottom/front edge
static const uint8_t s_TestObject[] = {
    NC,NE,NE,NE, NE,NE,NE,NE, NE,NE,NE,NE, NE,NE,NE,EC,  // row  0  perimeter top
    NW, _, _, _,  _, _, _, _,  _, _, _, _,  _, _, _,SE,  // row  1
    NW, _, _, _,  _, _, _, _,  _, _, _, _,  _, _, _,SE,  // row  2
    NW, _, _, _,  _, _, _, _,  _, _, _, _,  _, _, _,SE,  // row  3
    NW, _, _, _, NC,NE,EC, _,  _, _, _, _,  _, _, _,SE,  // row  4  top-left cluster top
    NW, _, _, _, NW, _,SE, _,  _, _, _, _,  _, _, _,SE,  // row  5  top-left sides + hollow centre
    NW, _, _, _, WC,SW,SC, _,  _, _, _, _,  _, _, _,SE,  // row  6  top-left cluster bottom
    NW, _, _, _,  _, _, _, _,  _, _, _, _,  _, _, _,SE,  // row  7
    NW, _, _, _,  _, _, _, _,  _, _, _, _,  _, _, _,SE,  // row  8
    NW, _, _, _,  _, _, _, _,  _,NC,NE,EC,  _, _, _,SE,  // row  9  bottom-right cluster top
    NW, _, _, _,  _, _, _, _,  _,NW, _,SE,  _, _, _,SE,  // row 10  bottom-right sides + hollow centre
    NW, _, _, _,  _, _, _, _,  _,WC,SW,SC,  _, _, _,SE,  // row 11  bottom-right cluster bottom
    NW, _, _, _,  _, _, _, _,  _, _, _, _,  _, _, _,SE,  // row 12
    NW, _, _, _,  _, _, _, _,  _, _, _, _,  _, _, _,SE,  // row 13
    NW, _, _, _,  _, _, _, _,  _, _, _, _,  _, _, _,SE,  // row 14
    WC,SW,SW,SW, SW,SW,SW,SW, SW,SW,SW,SW, SW,SW,SW,SC,  // row 15  perimeter bottom
};

static const uint8_t s_TestOverlay[] = {
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  0
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  1
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  2
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  3
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  4
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  5
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  6
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  7
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  8
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row  9
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row 10
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row 11
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row 12
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row 13
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row 14
    _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_,  // row 15
};

#undef _
#undef F
#undef SE
#undef SW
#undef NE
#undef NW

static void BuildTestMap(Map* map)
{
    int tileCount = map->width * map->height;
    for (int i = 0; i < tileCount; ++i)
    {
        map->layers[LAYER_FLOOR   * tileCount + i] = s_TestFloor[i];
        map->layers[LAYER_OBJECT  * tileCount + i] = s_TestObject[i];
        map->layers[LAYER_OVERLAY * tileCount + i] = s_TestOverlay[i];
    }
}

static bool IsTileWalkable(const Map* map, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height) return false;
    if ((TileID)MAP_TILE(map, LAYER_FLOOR, tx, ty) == TILE_VOID) return false;
    TileID obj = (TileID)MAP_TILE(map, LAYER_OBJECT, tx, ty);
    return (obj == TILE_VOID) || s_TileDefs[(uint8_t)obj].walkable;
}

// ---------------------------------------------------------------------------------------------------------------------
//  Game entry points
// ---------------------------------------------------------------------------------------------------------------------

extern "C" __declspec(dllexport) GAME_INIT(GameInitialize)
{
    GameState* state = (GameState*)memory->permanentStorage.base;
    memory->permanentStorage.used = sizeof(GameState);

    // Set map dimensions before allocating so the layer block size is correct.
    state->currentMap.width  = 16;
    state->currentMap.height = 16;
    // One contiguous allocation for all three layers; PushArray zero-fills the block.
    state->currentMap.layers = PushArray(&memory->permanentStorage,
                                         MAP_LAYERS * state->currentMap.width * state->currentMap.height,
                                         uint8_t);

    BuildTestMap(&state->currentMap);

    // Place player at a safe interior tile, clear of wall clusters
    state->player.tileX = 7;
    state->player.tileY = 7;

    // Core stat block — data only, no gameplay behaviour yet
    state->player.hp            = 20;
    state->player.maxHp         = 20;
    state->player.attack        =  5;
    state->player.defense       =  2;
    state->player.level         =  1;
    state->player.xp            =  0;
    state->player.xpToNextLevel = 10;

    state->isInitialized       = true;
    state->isCameraInitialized = false;   // deferred — needs buffer dimensions

    LOG_INFO(memory, "Game initialised — player at tile (%d, %d)",
             state->player.tileX, state->player.tileY);
}

extern "C" __declspec(dllexport) GAME_RUN_FRAME(GameUpdateAndRender)
{
    GameState* state = (GameState*)memory->permanentStorage.base;
    if (memory->permanentStorage.used < sizeof(GameState))
        memory->permanentStorage.used = sizeof(GameState);

    Map* map = &state->currentMap;

    // --- ONE-TIME CAMERA INIT ---
    // Cannot run in GameInitialize because buffer dimensions aren't available there.
    if (!state->isCameraInitialized)
    {
        // The map spans (width-1 + height-1) * HALF_TILE_H screen pixels vertically.
        // Offset so the map is vertically centred, with extra room at top for walls.
        float mapScreenH = (float)((map->width - 1 + map->height - 1) * HALF_TILE_H);
        state->camera.x  = (float)buffer->width  * 0.5f;
        state->camera.y  = ((float)buffer->height - mapScreenH) * 0.5f;
        state->isCameraInitialized = true;
    }

    // --- INPUT: tile-step movement ---
    // One tile per keypress — only fires when the button transitions down this frame.
    GameControllerInput* ctrl = &input->controllers[0];

    int newTX = state->player.tileX;
    int newTY = state->player.tileY;

    if (ctrl->moveUp.endedDown    && ctrl->moveUp.halfTransitionCount    > 0) newTY--;
    if (ctrl->moveDown.endedDown  && ctrl->moveDown.halfTransitionCount  > 0) newTY++;
    if (ctrl->moveLeft.endedDown  && ctrl->moveLeft.halfTransitionCount  > 0) newTX--;
    if (ctrl->moveRight.endedDown && ctrl->moveRight.halfTransitionCount > 0) newTX++;

    if ((newTX != state->player.tileX || newTY != state->player.tileY) &&
        IsTileWalkable(map, newTX, newTY))
    {
        state->player.tileX = newTX;
        state->player.tileY = newTY;
    }

    // Snap camera float to integer pixels once — used by both hover picking and rendering.
    // state->camera.{x,y} stay float for smooth accumulation; all draw calls use integers.
    int camX = (int)floorf(state->camera.x + 0.5f);
    int camY = (int)floorf(state->camera.y + 0.5f);

    // --- MOUSE: compute hovered tile ---
    int hoverTX, hoverTY;
    ScreenToTile(input->mouse.x, input->mouse.y,
                 (float)camX, (float)camY,
                 &hoverTX, &hoverTY);
    if (hoverTX < 0)             hoverTX = 0;
    if (hoverTY < 0)             hoverTY = 0;
    if (hoverTX >= map->width)   hoverTX = map->width  - 1;
    if (hoverTY >= map->height)  hoverTY = map->height - 1;

    // --- RENDERING ---

    // 1. Clear to near-black background
    DrawRectangle(buffer, 0.0f, 0.0f, buffer->width, buffer->height, COLOR_BG);

    // 2. Viewport cull — compute the conservative axis-aligned tile rect visible through the screen.
    //    Sample ScreenToTile at all four screen corners and take the bounding box of results.
    //    Add a 1-tile margin so tall objects (walls) at the boundary are never clipped early.
    int rowMin, rowMax, colMin, colMax;
    {
        int tx, ty;
        rowMin = map->height; rowMax = 0; colMin = map->width; colMax = 0;

        // Helper lambda — accumulate tile coords into the bounding box
        auto Acc = [&](float sx, float sy) {
            ScreenToTile(sx, sy, (float)camX, (float)camY, &tx, &ty);
            if (tx < colMin) colMin = tx;
            if (tx > colMax) colMax = tx;
            if (ty < rowMin) rowMin = ty;
            if (ty > rowMax) rowMax = ty;
        };
        Acc(0.0f,               0.0f);
        Acc((float)buffer->width, 0.0f);
        Acc(0.0f,               (float)buffer->height);
        Acc((float)buffer->width, (float)buffer->height);

        rowMin--; rowMax++;
        colMin--; colMax++;
        if (rowMin < 0)             rowMin = 0;
        if (rowMax >= map->height)  rowMax = map->height - 1;
        if (colMin < 0)             colMin = 0;
        if (colMax >= map->width)   colMax = map->width  - 1;
    }

    // 3. Painter's algorithm: row 0 is farthest from camera, bottom-right corner nearest.
    //    Within each row, earlier columns are farther.
    for (int row = rowMin; row <= rowMax; ++row)
    {
        for (int col = colMin; col <= colMax; ++col)
        {
            TileID floorId   = (TileID)MAP_TILE(map, LAYER_FLOOR,   col, row);
            TileID objectId  = (TileID)MAP_TILE(map, LAYER_OBJECT,  col, row);
            TileID overlayId = (TileID)MAP_TILE(map, LAYER_OVERLAY, col, row);

            if (floorId == TILE_VOID && objectId == TILE_VOID && overlayId == TILE_VOID)
                continue;

            int sx = TileToScreenX(col, row, (float)camX);
            int sy = TileToScreenY(col, row, (float)camY);

            // a. Floor layer — always drawn; wall sprites rely on the floor to fill
            //    in below their raised face (the face has transparent pixels at floor level).
            DrawTileById(buffer, &assets->tilesetBitmap, floorId, sx, sy);

            // b. Object layer — yOffset from s_TileDefs applied inside DrawTileById
            DrawTileById(buffer, &assets->tilesetBitmap, objectId, sx, sy);

            // c. Hover highlight (on top of tile geometry)
            if (col == hoverTX && row == hoverTY)
                DrawIsoOutline(buffer, sx, sy, COLOR_HIGHLIGHT);

            // d. Player sprite — drawn in the same depth slot as its tile so
            //    surrounding tiles in later rows correctly appear in front.
            if (state->player.tileX == col && state->player.tileY == row)
            {
                // Centre the 32×32 front-facing hero sprite on this tile.
                // The sprite's visual feet land at the tile's midline (sy + HALF_TILE_H).
                DrawBitmapRegion(buffer, &assets->heroBitmap,
                                 0, 0, 32, 32,
                                 (float)(sx - 16), (float)(sy - 22));
            }

            // e. Overlay layer — drawn on top of units (e.g. roof tiles)
            DrawTileById(buffer, &assets->tilesetBitmap, overlayId, sx, sy);
        }
    }

    // --- OSD ---
    if (input->frameTimeMs > 0.0f)
    {
        // FPS string — refresh every OSD_REFRESH_INTERVAL frames to avoid flicker
        if (state->osdRefreshCounter <= 0)
        {
            float fps = 1000.0f / input->frameTimeMs;
            snprintf(state->osdFpsText, sizeof(state->osdFpsText),
                     "FPS: %.0f  %.2fms", fps, input->frameTimeMs);
            state->osdRefreshCounter = OSD_REFRESH_INTERVAL;
        }
        else
        {
            state->osdRefreshCounter--;
        }

        // Player / hover coords — fresh every frame
        char posText[64];
        snprintf(posText, sizeof(posText), "P:(%d,%d)  Hover:(%d,%d)",
                 state->player.tileX, state->player.tileY, hoverTX, hoverTY);

        // Shadow pass then colour pass for both lines
        DrawDebugText(buffer, 7,  7, 0xFF000000, state->osdFpsText);
        DrawDebugText(buffer, 6,  6, 0xFFFFFFFF, state->osdFpsText);
        DrawDebugText(buffer, 7, 27, 0xFF000000, posText);
        DrawDebugText(buffer, 6, 26, 0xFFFFFFFF, posText);
    }

    // --- AUDIO (muted sine tone — keep the synthesiser alive for later) ---
    int16_t toneVolume = 0;
    int     toneHz     = 256;
    int16_t* sampleOut = audioBuffer->samples;
    for (int i = 0; i < audioBuffer->sampleCount; ++i)
    {
        float t = (float)(state->tSineIndex % (uint64_t)audioBuffer->samplesPerSecond)
                / (float)audioBuffer->samplesPerSecond;
        int16_t v = (int16_t)(sinf(2.0f * 3.14159265f * (float)toneHz * t) * toneVolume);
        *sampleOut++ = v;
        *sampleOut++ = v;
        state->tSineIndex++;
    }
}
