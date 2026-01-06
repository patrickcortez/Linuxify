// Compile: (Included in loneshooter.cpp compilation)
// Run: N/A - Header only

#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace DialogueSystem {

enum DialogueState {
    DIALOGUE_INACTIVE,
    DIALOGUE_ACTIVE,
    DIALOGUE_OPTION_SELECT,
    DIALOGUE_FINISHED
};

struct DialogueLine {
    std::wstring text;
    bool hasOptions;
    std::wstring option1;
    std::wstring option2;
};

struct Dialogue {
    std::wstring name;
    std::vector<DialogueLine> lines;
};

inline Dialogue LoadDialogueFromJSON(const wchar_t* path, bool selectRandomLine = false) {
    Dialogue dialogue;
    dialogue.name = L"Unknown";
    
    FILE* f = _wfopen(path, L"rb");
    if (!f) return dialogue;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = new char[size + 1];
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    std::string json(buffer);
    delete[] buffer;
    
    auto findValue = [&json](const std::string& key) -> std::string {
        std::string lowerJson = json;
        std::string lowerKey = key;
        for (auto& c : lowerJson) c = (char)tolower(c);
        for (auto& c : lowerKey) c = (char)tolower(c);
        
        size_t pos = lowerJson.find("\"" + lowerKey + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find(":", pos);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
        if (pos >= json.size()) return "";
        if (json[pos] == '"') {
            size_t start = pos + 1;
            size_t end = json.find("\"", start);
            if (end == std::string::npos) return "";
            return json.substr(start, end - start);
        }
        return "";
    };
    
    auto toWide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
        wchar_t* wstr = new wchar_t[len];
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wstr, len);
        std::wstring result(wstr);
        delete[] wstr;
        return result;
    };
    
    std::string name = findValue("Name");
    if (name.empty()) name = findValue("name");
    if (!name.empty()) {
        dialogue.name = toWide(name);
    }
    
    std::vector<std::string> allLines;
    for (int i = 1; i <= 20; i++) {
        char lineKey[16];
        sprintf(lineKey, "line%d", i);
        std::string lineVal = findValue(lineKey);
        if (!lineVal.empty()) {
            allLines.push_back(lineVal);
        }
    }
    
    std::string opt1 = findValue("Option1");
    std::string opt2 = findValue("Option2");
    std::string lineWithOptions = findValue("Line");
    
    if (selectRandomLine && !allLines.empty()) {
        int idx = rand() % (int)allLines.size();
        DialogueLine dl;
        dl.text = toWide(allLines[idx]);
        dl.hasOptions = false;
        dialogue.lines.push_back(dl);
    } else {
        for (const auto& line : allLines) {
            DialogueLine dl;
            dl.text = toWide(line);
            dl.hasOptions = false;
            dialogue.lines.push_back(dl);
        }
    }
    
    if (!lineWithOptions.empty()) {
        DialogueLine dl;
        dl.text = toWide(lineWithOptions);
        dl.hasOptions = !opt1.empty();
        dl.option1 = toWide(opt1);
        dl.option2 = toWide(opt2);
        dialogue.lines.push_back(dl);
    }
    
    return dialogue;
}

inline void RenderDialogueBox(HDC hdc, int screenW, int screenH, const std::wstring& name, const std::wstring& text, bool showOptions, const std::wstring& opt1, const std::wstring& opt2, int selectedOption) {
    int boxH = 120;
    int boxY = screenH - boxH - 20;
    int boxX = 50;
    int boxW = screenW - 100;
    
    HBRUSH bgBrush = CreateSolidBrush(RGB(20, 20, 30));
    RECT bgRect = {boxX, boxY, boxX + boxW, boxY + boxH};
    FillRect(hdc, &bgRect, bgBrush);
    DeleteObject(bgBrush);
    
    HPEN borderPen = CreatePen(PS_SOLID, 3, RGB(200, 180, 100));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH hollowBrush = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hollowBrush);
    Rectangle(hdc, boxX, boxY, boxX + boxW, boxY + boxH);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
    
    SetBkMode(hdc, TRANSPARENT);
    
    HFONT nameFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    HFONT textFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    
    HFONT oldFont = (HFONT)SelectObject(hdc, nameFont);
    SetTextColor(hdc, RGB(200, 180, 100));
    TextOutW(hdc, boxX + 15, boxY + 10, name.c_str(), (int)name.length());
    
    SelectObject(hdc, textFont);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT textRect = {boxX + 15, boxY + 40, boxX + boxW - 15, boxY + 80};
    DrawTextW(hdc, text.c_str(), -1, &textRect, DT_LEFT | DT_WORDBREAK);
    
    if (showOptions) {
        int optY = boxY + boxH - 35;
        
        SetTextColor(hdc, selectedOption == 0 ? RGB(255, 255, 0) : RGB(180, 180, 180));
        std::wstring opt1Text = L"[1] " + opt1;
        TextOutW(hdc, boxX + 50, optY, opt1Text.c_str(), (int)opt1Text.length());
        
        SetTextColor(hdc, selectedOption == 1 ? RGB(255, 255, 0) : RGB(180, 180, 180));
        std::wstring opt2Text = L"[2] " + opt2;
        TextOutW(hdc, boxX + 250, optY, opt2Text.c_str(), (int)opt2Text.length());
    } else {
        SetTextColor(hdc, RGB(150, 150, 150));
        TextOutA(hdc, boxX + boxW - 150, boxY + boxH - 25, "[E] Continue", 12);
    }
    
    SelectObject(hdc, oldFont);
    DeleteObject(nameFont);
    DeleteObject(textFont);
}

}
