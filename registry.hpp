// Linuxify Registry - External Package Management Header
// This module discovers and manages user-installed packages,
// allowing them to be executed directly from Linuxify shell.

#ifndef LINUXIFY_REGISTRY_HPP
#define LINUXIFY_REGISTRY_HPP

#include <string>
#include <map>
#include <vector>

class LinuxifyRegistry {
private:
    std::map<std::string, std::string> commandRegistry;  // command -> full path
    std::string registryFilePath;
    std::string linuxdbPath;
    bool isLoaded = false;
    
    // Common Linux command names to look for
    std::vector<std::string> commonCommands;
    
    // Get the path to the linuxdb directory
    std::string getLinuxdbPath();
    
    // Get the path to the registry.lin file
    std::string getRegistryFilePath();
    
    // Search for executable in PATH
    std::string findInPath(const std::string& command);
    
    // Check common installation directories
    std::string findInCommonDirs(const std::string& command);
    
public:
    LinuxifyRegistry();
    
    // Load registry from file
    void loadRegistry();
    
    // Save registry to file
    void saveRegistry();
    
    // Refresh registry by scanning system
    int refreshRegistry();
    
    // Check if command is registered
    bool isRegistered(const std::string& command);
    
    // Get executable path for command
    std::string getExecutablePath(const std::string& command);
    
    // Execute a registered command
    bool executeRegisteredCommand(const std::string& command, const std::vector<std::string>& args, const std::string& currentDir);
    
    // Get all registered commands
    const std::map<std::string, std::string>& getAllCommands();
    
    // Add a custom command to registry
    void addCommand(const std::string& command, const std::string& path);
    
    // Remove a command from registry
    void removeCommand(const std::string& command);
    
    // Get the linuxdb path (for external access)
    std::string getDbPath();
};

// Global registry instance
extern LinuxifyRegistry g_registry;

#endif // LINUXIFY_REGISTRY_HPP
