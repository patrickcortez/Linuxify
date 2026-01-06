// pathfinder.hpp - A* Pathfinding for LoneShooter
// Include after worldMap is declared
// Usage: 
//   Pathfinder::Init(worldMapPtr, collisionCallback);
//   auto path = Pathfinder::FindPath(startX, startY, targetX, targetY);

#ifndef PATHFINDER_HPP
#define PATHFINDER_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <queue>

namespace Pathfinder {

const int PATH_MAP_WIDTH = 64;
const int PATH_MAP_HEIGHT = 64;
const int MAX_SEARCH_NODES = 500;
const float SPIRE_CENTER_X = 32.0f;
const float SPIRE_CENTER_Y = 32.0f;
const float SPIRE_RADIUS = 3.0f;

struct PathNode {
    int x, y;
    float g, h, f;
    int parentX, parentY;
    
    bool operator>(const PathNode& other) const {
        return f > other.f;
    }
};

static int (*worldMapPtr)[PATH_MAP_HEIGHT] = nullptr;

typedef bool (*ExternalCollisionFunc)(float x, float y);
static ExternalCollisionFunc externalCollisionCheck = nullptr;

inline void Init(int (*wm)[PATH_MAP_HEIGHT], ExternalCollisionFunc extCollision = nullptr) {
    worldMapPtr = wm;
    externalCollisionCheck = extCollision;
}

inline bool IsBlocked(int x, int y) {
    if (x < 0 || x >= PATH_MAP_WIDTH || y < 0 || y >= PATH_MAP_HEIGHT) return true;
    if (worldMapPtr[x][y] != 0) return true;
    
    float cellCenterX = x + 0.5f;
    float cellCenterY = y + 0.5f;
    float dx = cellCenterX - SPIRE_CENTER_X;
    float dy = cellCenterY - SPIRE_CENTER_Y;
    if (dx*dx + dy*dy < SPIRE_RADIUS * SPIRE_RADIUS) return true;
    
    if (externalCollisionCheck && externalCollisionCheck(cellCenterX, cellCenterY)) return true;
    
    return false;
}

inline float Heuristic(int x1, int y1, int x2, int y2) {
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    return sqrtf(dx*dx + dy*dy);
}

inline std::vector<std::pair<int,int>> FindPath(float startX, float startY, float targetX, float targetY) {
    std::vector<std::pair<int,int>> result;
    if (!worldMapPtr) return result;
    
    int sx = (int)startX;
    int sy = (int)startY;
    int tx = (int)targetX;
    int ty = (int)targetY;
    
    if (sx < 0 || sx >= PATH_MAP_WIDTH || sy < 0 || sy >= PATH_MAP_HEIGHT) return result;
    if (tx < 0 || tx >= PATH_MAP_WIDTH || ty < 0 || ty >= PATH_MAP_HEIGHT) return result;
    
    if (IsBlocked(tx, ty)) {
        for (int ddx = -1; ddx <= 1; ddx++) {
            for (int ddy = -1; ddy <= 1; ddy++) {
                if (ddx == 0 && ddy == 0) continue;
                int nx = tx + ddx;
                int ny = ty + ddy;
                if (!IsBlocked(nx, ny)) {
                    tx = nx;
                    ty = ny;
                    goto found_valid_target;
                }
            }
        }
        return result;
    }
    found_valid_target:
    
    if (sx == tx && sy == ty) {
        result.push_back({tx, ty});
        return result;
    }
    
    static bool closedSet[PATH_MAP_WIDTH][PATH_MAP_HEIGHT];
    static float gScore[PATH_MAP_WIDTH][PATH_MAP_HEIGHT];
    static int parentX[PATH_MAP_WIDTH][PATH_MAP_HEIGHT];
    static int parentY[PATH_MAP_WIDTH][PATH_MAP_HEIGHT];
    
    for (int i = 0; i < PATH_MAP_WIDTH; i++) {
        for (int j = 0; j < PATH_MAP_HEIGHT; j++) {
            closedSet[i][j] = false;
            gScore[i][j] = 1e9f;
            parentX[i][j] = -1;
            parentY[i][j] = -1;
        }
    }
    
    std::priority_queue<PathNode, std::vector<PathNode>, std::greater<PathNode>> openSet;
    
    PathNode start;
    start.x = sx;
    start.y = sy;
    start.g = 0;
    start.h = Heuristic(sx, sy, tx, ty);
    start.f = start.g + start.h;
    start.parentX = -1;
    start.parentY = -1;
    
    openSet.push(start);
    gScore[sx][sy] = 0;
    
    int nodesSearched = 0;
    
    const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dy8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    const float cost8[] = {1.414f, 1.0f, 1.414f, 1.0f, 1.0f, 1.414f, 1.0f, 1.414f};
    
    while (!openSet.empty() && nodesSearched < MAX_SEARCH_NODES) {
        PathNode current = openSet.top();
        openSet.pop();
        
        if (closedSet[current.x][current.y]) continue;
        closedSet[current.x][current.y] = true;
        nodesSearched++;
        
        if (current.x == tx && current.y == ty) {
            int cx = tx;
            int cy = ty;
            while (cx != -1 && cy != -1) {
                result.push_back({cx, cy});
                int px = parentX[cx][cy];
                int py = parentY[cx][cy];
                cx = px;
                cy = py;
            }
            std::reverse(result.begin(), result.end());
            return result;
        }
        
        for (int i = 0; i < 8; i++) {
            int nx = current.x + dx8[i];
            int ny = current.y + dy8[i];
            
            if (IsBlocked(nx, ny)) continue;
            if (closedSet[nx][ny]) continue;
            
            if (dx8[i] != 0 && dy8[i] != 0) {
                if (IsBlocked(current.x + dx8[i], current.y) && 
                    IsBlocked(current.x, current.y + dy8[i])) continue;
            }
            
            float tentativeG = gScore[current.x][current.y] + cost8[i];
            
            if (tentativeG < gScore[nx][ny]) {
                gScore[nx][ny] = tentativeG;
                parentX[nx][ny] = current.x;
                parentY[nx][ny] = current.y;
                
                PathNode neighbor;
                neighbor.x = nx;
                neighbor.y = ny;
                neighbor.g = tentativeG;
                neighbor.h = Heuristic(nx, ny, tx, ty);
                neighbor.f = neighbor.g + neighbor.h;
                neighbor.parentX = current.x;
                neighbor.parentY = current.y;
                
                openSet.push(neighbor);
            }
        }
    }
    
    return result;
}

inline bool GetNextPathPoint(float currentX, float currentY, 
                             std::vector<std::pair<int,int>>& path, int& pathIndex,
                             float& outX, float& outY) {
    if (path.empty() || pathIndex >= (int)path.size()) return false;
    
    float targetX = path[pathIndex].first + 0.5f;
    float targetY = path[pathIndex].second + 0.5f;
    
    float dx = targetX - currentX;
    float dy = targetY - currentY;
    float dist = sqrtf(dx*dx + dy*dy);
    
    if (dist < 0.5f) {
        pathIndex++;
        if (pathIndex >= (int)path.size()) return false;
        targetX = path[pathIndex].first + 0.5f;
        targetY = path[pathIndex].second + 0.5f;
    }
    
    outX = targetX;
    outY = targetY;
    return true;
}

}

#endif

