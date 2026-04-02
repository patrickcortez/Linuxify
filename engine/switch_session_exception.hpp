#ifndef LINUXIFY_ENGINE_SWITCH_SESSION_EXCEPTION_HPP
#define LINUXIFY_ENGINE_SWITCH_SESSION_EXCEPTION_HPP

#include <exception>
#include <string>

// Thrown to unwind the current ShellEngine loop and jump to a different session.
class SwitchSessionException : public std::exception {
public:
    int targetSessionId;
    std::string targetSessionName;

    SwitchSessionException(int targetId, const std::string& targetName = "")
        : targetSessionId(targetId), targetSessionName(targetName) {}

    const char* what() const noexcept override {
        return "Internal session switch exception";
    }
};

#endif // LINUXIFY_ENGINE_SWITCH_SESSION_EXCEPTION_HPP
