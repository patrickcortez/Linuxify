#ifndef LINUXIFY_ENGINE_STATES_HPP
#define LINUXIFY_ENGINE_STATES_HPP

#include "continuation.hpp"
#include "shell_context.hpp"
#include "../shell_streams.hpp"
#include "../input_handler.hpp"
#include <iostream>

// Forward declarations of helper functions (to be defined in main.cpp or a helper file)
// We will need to expose the execution logic from main.cpp to these states
void execute_command_logic(ShellContext& ctx, const std::string& input);

// --- State: Prompt ---
// Responsible for visual formatting and displaying the prompt
class StatePrompt : public Continuation {
public:
    std::string getName() const override { return "Prompt"; }

    std::unique_ptr<Continuation> run(ShellContext& ctx) override;
};

// --- State: Read Input ---
// Responsible for blocking until user enters a line
class StateReadInput : public Continuation {
public:
    std::string getName() const override { return "ReadInput"; }

    std::unique_ptr<Continuation> run(ShellContext& ctx) override;
};

// --- State: Execute ---
// Responsible for processing the command
class StateExecute : public Continuation {
    std::string inputLine;
public:
    StateExecute(std::string cmd) : inputLine(std::move(cmd)) {}
    
    std::string getName() const override { return "Execute"; }

    std::unique_ptr<Continuation> run(ShellContext& ctx) override;
};


// --- Implementation ---

inline std::unique_ptr<Continuation> StatePrompt::run(ShellContext& ctx) {
    if (!ctx.running) return nullptr;

    // SPACING LOGIC: 
    // If the previous command produced output (was not empty),
    // we give it breathing room before showing the new prompt.
    if (!ctx.previousCommandWasEmpty) {
        ShellIO::sout << ShellIO::endl;
    }
    
    // Reset for the next cycle
    ctx.previousCommandWasEmpty = false; 
    return std::make_unique<StateReadInput>();
}

inline std::unique_ptr<Continuation> StateReadInput::run(ShellContext& ctx) {
    // This call manages the prompt printing + input reading + history
    std::string input = InputHandler::read(ctx.currentDir, ctx.commandHistory);

    // Trim whitespace
    size_t first = input.find_first_not_of(" \t");
    if (first == std::string::npos) {
        // Empty command
        ctx.previousCommandWasEmpty = true;
        return std::make_unique<StatePrompt>();
    }
    
    size_t last = input.find_last_not_of(" \t");
    input = input.substr(first, (last - first + 1));

    // Separation: Print newline immediately after input (before output)
    ShellIO::sout << ShellIO::endl;

    return std::make_unique<StateExecute>(input);
}

inline std::unique_ptr<Continuation> StateExecute::run(ShellContext& ctx) {
    execute_command_logic(ctx, inputLine);

    if (!ctx.running) return nullptr;

    return std::make_unique<StatePrompt>();
}

#endif // LINUXIFY_ENGINE_STATES_HPP
