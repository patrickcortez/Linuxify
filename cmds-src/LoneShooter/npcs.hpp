// Compile: (Included in loneshooter.cpp compilation)
// Run: N/A - Header only

#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <cmath>

namespace NPCSystem {

struct NPC {
    float x, y;
    bool active;
    bool isTalking;
    DWORD* spriteIdle;
    int spriteIdleW, spriteIdleH;
    DWORD* spriteTalking;
    int spriteTalkingW, spriteTalkingH;
    std::wstring dialoguePath;
    std::wstring name;
};

inline std::vector<NPC> npcs;

inline void SpawnNPC(float x, float y, const std::wstring& name, DWORD* idle, int idleW, int idleH, DWORD* talking, int talkW, int talkH, const std::wstring& dialoguePath) {
    NPC npc;
    npc.x = x;
    npc.y = y;
    npc.active = true;
    npc.isTalking = false;
    npc.spriteIdle = idle;
    npc.spriteIdleW = idleW;
    npc.spriteIdleH = idleH;
    npc.spriteTalking = talking;
    npc.spriteTalkingW = talkW;
    npc.spriteTalkingH = talkH;
    npc.dialoguePath = dialoguePath;
    npc.name = name;
    npcs.push_back(npc);
}

inline void ClearNPCs() {
    npcs.clear();
}

inline NPC* GetNearestInteractableNPC(float playerX, float playerY, float maxDist) {
    NPC* nearest = nullptr;
    float nearestDist = maxDist;
    
    for (auto& npc : npcs) {
        if (!npc.active) continue;
        float dx = npc.x - playerX;
        float dy = npc.y - playerY;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = &npc;
        }
    }
    
    return nearest;
}

inline void SetAllNPCsNotTalking() {
    for (auto& npc : npcs) {
        npc.isTalking = false;
    }
}

}
