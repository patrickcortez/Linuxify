// g++ -O3 -o ../cmds/cowsay.exe cowsay.cpp
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <windows.h>

struct CowFile {
    const char* name;
    const char* art;
};

static const CowFile COW_FILES[] = {
    {"default",
     "        \\   ^__^\n"
     "         \\  (oo)\\_______\n"
     "            (__)\\       )\\/\\\n"
     "                ||----w |\n"
     "                ||     ||\n"},
    {"tux",
     "   \\\n"
     "    \\\n"
     "        .--.\n"
     "       |o_o |\n"
     "       |:_/ |\n"
     "      //   \\ \\\n"
     "     (|     | )\n"
     "    /'\\_   _/`\\\n"
     "    \\___)=(___/\n"},
    {"dragon",
     "      \\                    / \\  //\\\n"
     "       \\    |\\___/|      /   \\//  \\\\\n"
     "            /0  0  \\__  /    //  | \\ \\\n"
     "           /     /  \\/_/    //   |  \\  \\\n"
     "           @_^_@'/   \\/_   //    |   \\   \\\n"
     "           //_^_/     \\/_ //     |    \\    \\\n"
     "        ( //) |        \\///      |     \\     \\\n"
     "      ( / /) _|_ /   )  //       |      \\     _\\\n"
     "    ( // /) '/,_ _ _/  ( ; -.    |    _ _\\.-~        .-~~~^-.\n"
     "  (( / / )) ,-{        _      `-.|.-~-.           .~         `.\n"
     " (( // / ))  '/\\      /                 ~-. _ .-~      .-~^-.  \\\n"
     " (( /// ))      `.   {            }                   /      \\  \\\n"
     "  (( / ))     .----~-.\\        \\-'                 .~         \\  `. \\^-.\n"
     "             ///.----..>        \\             _ -~             `.  ^-`  ^-_\n"
     "               ///-._ _ _ _ _ _ _}^ - - - - ~                     ~-- ,.-~\n"
     "                                                                  /.-~\n"},
    {"stegosaurus",
     "         \\                             .       .\n"
     "          \\                           / `.   .' \"\n"
     "           \\                  .---.  <    > <    >  .---.\n"
     "            \\                 |    \\  \\ - ~ ~ - /  /    |\n"
     "          _____          ..-~             ~-..-~\n"
     "         |     |   \\~~~\\.'                    `./~~~/\n"
     "        ---------   \\__/                        \\__/\n"
     "       .'  O    \\     /               /       \\  \"\n"
     "      (_____,    `._.'               |         }  \\/~~~/\n"
     "       `----.          /       }     |        /    \\__/\n"
     "             `-.      |       /      |       /      `. ,~~|\n"
     "                 ~-.__|      /_ - ~ ^|      /- _      `..-'\n"
     "                      |     /        |     /     ~-.     `-. _  _  _\n"
     "                      |_____|        |_____|         ~ - . _ _ _ _ _>\n"},
    {"bunny",
     "  \\\n"
     "   \\\n"
     "   (\\(\\\n"
     "   ( -.-)o\n"
     "   o_(\")_(\")\n"},
    {"cat",
     "  \\\n"
     "   \\\n"
     "    /\\_/\\\n"
     "   ( o.o )\n"
     "    > ^ <\n"
     "   /|   |\\\n"
     "  (_|   |_)\n"},
    {"ghost",
     "   \\\n"
     "    \\\n"
     "     .----.\n"
     "    / o  o \\\n"
     "   |   __   |\n"
     "   |  (__) |\n"
     "   |       |\n"
     "    \\ --- /\n"
     "     )   (\n"
     "    / ' ' \\\n"
     "   / ' ' ' \\\n"
     "   `\"\"'\"\"'\"\"'\n"},
    {"skull",
     "   \\\n"
     "    \\\n"
     "      _____\n"
     "     /     \\\n"
     "    | () () |\n"
     "     \\  ^  /\n"
     "      ||||||\n"
     "      ||||||\n"},
};

static const int NUM_COWS = sizeof(COW_FILES) / sizeof(COW_FILES[0]);

static std::vector<std::string> wordWrap(const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string word;
    std::string currentLine;

    while (stream >> word) {
        if (currentLine.empty()) {
            currentLine = word;
        } else if ((int)(currentLine.length() + 1 + word.length()) <= maxWidth) {
            currentLine += " " + word;
        } else {
            lines.push_back(currentLine);
            currentLine = word;
        }
    }
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

static void drawBubble(const std::vector<std::string>& lines, bool think) {
    int maxLen = 0;
    for (auto& l : lines) {
        if ((int)l.length() > maxLen) maxLen = (int)l.length();
    }

    std::string topBorder(maxLen + 2, '_');
    std::string botBorder(maxLen + 2, '-');
    std::cout << " " << topBorder << "\n";

    if (lines.size() == 1) {
        char l = think ? '(' : '<';
        char r = think ? ')' : '>';
        std::string padded = lines[0];
        padded.append(maxLen - padded.length(), ' ');
        std::cout << l << " " << padded << " " << r << "\n";
    } else {
        for (size_t i = 0; i < lines.size(); i++) {
            std::string padded = lines[i];
            padded.append(maxLen - padded.length(), ' ');
            char l, r;
            if (think) {
                l = '('; r = ')';
            } else if (i == 0) {
                l = '/'; r = '\\';
            } else if (i == lines.size() - 1) {
                l = '\\'; r = '/';
            } else {
                l = '|'; r = '|';
            }
            std::cout << l << " " << padded << " " << r << "\n";
        }
    }
    std::cout << " " << botBorder << "\n";
}

static std::string readStdin() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    bool isPipe = !GetConsoleMode(hIn, &mode);

    if (!isPipe) return "";

    std::string result;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hIn, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
        result.append(buf, bytesRead);
    }

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

int main(int argc, char* argv[]) {
    std::string message;
    int cowIdx = 0;
    bool think = false;
    bool listMode = false;
    int wrapWidth = 40;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: cowsay [-f cow] [-W width] [-l] [message]\n";
            std::cout << "Generate an ASCII cow with a speech bubble.\n\n";
            std::cout << "Options:\n";
            std::cout << "  -f <cow>   Select cow file: default, tux, dragon,\n";
            std::cout << "             stegosaurus, bunny, cat, ghost, skull\n";
            std::cout << "  -W <width> Max bubble width (default: 40)\n";
            std::cout << "  -l         List available cows\n";
            std::cout << "  -h, --help Show this help\n\n";
            std::cout << "Also available as 'cowthink' (thought bubble).\n";
            std::cout << "Supports piped input: echo \"hello\" | cowsay\n";
            return 0;
        } else if (arg == "-f" && i + 1 < argc) {
            std::string name = argv[++i];
            bool found = false;
            for (int j = 0; j < NUM_COWS; j++) {
                if (name == COW_FILES[j].name) {
                    cowIdx = j;
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << "cowsay: unknown cow '" << name << "'. Use -l to list.\n";
                return 1;
            }
        } else if (arg == "-W" && i + 1 < argc) {
            wrapWidth = atoi(argv[++i]);
            if (wrapWidth < 10) wrapWidth = 10;
        } else if (arg == "-l") {
            listMode = true;
        } else {
            if (!message.empty()) message += " ";
            message += arg;
        }
    }

    if (listMode) {
        std::cout << "Available cow files:\n";
        for (int j = 0; j < NUM_COWS; j++) {
            std::cout << "  " << COW_FILES[j].name << "\n";
        }
        return 0;
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeName = exePath;
    if (exeName.find("cowthink") != std::string::npos) {
        think = true;
    }

    if (message.empty()) {
        message = readStdin();
    }

    if (message.empty()) {
        message = "Moo!";
    }

    std::vector<std::string> lines = wordWrap(message, wrapWidth);
    drawBubble(lines, think);
    std::cout << COW_FILES[cowIdx].art;

    return 0;
}
