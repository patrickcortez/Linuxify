// sesh.hpp - Session Management for Linuxify
#ifndef LINUXIFY_SESH_HPP
#define LINUXIFY_SESH_HPP

#include <string>
#include <vector>
#include "../engine/shell_context.hpp"

namespace Sesh {

    // Initialize session system (ensure directory exists)
    void init();

    void saveSession(ShellContext& ctx, const std::string& sessionName);
    void loadSession(ShellContext& ctx, const std::string& sessionName);
    void listSessions();

} // namespace Sesh

#endif // LINUXIFY_SESH_HPP
