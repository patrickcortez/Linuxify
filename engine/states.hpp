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
// Responsible for polling input (non-blocking)
class StateReadInput : public Continuation {
    std::unique_ptr<InputHandler> handler;
public:
    StateReadInput(std::unique_ptr<InputHandler> existingHandler = nullptr) 
        : handler(std::move(existingHandler)) {}

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
    
    // Transition to ReadInput with a fresh handler (nullptr)
    return std::make_unique<StateReadInput>();
}

inline std::unique_ptr<Continuation> StateReadInput::run(ShellContext& ctx) {
    // Lazy Initialization of Handler
    if (!handler) {
        handler = std::make_unique<InputHandler>(ctx.currentDir, ctx.commandHistory);
    }

    // POLL (Non-Blocking)
    InputHandler::PollResult result = handler->poll();

    if (result == InputHandler::PollResult::LineReady) {
        // Input Complete
        std::string input = handler->getInputBuffer();
        
        // Trim whitespace
        size_t first = input.find_first_not_of(" \t");
        if (first == std::string::npos) {
            // Empty command
            ctx.previousCommandWasEmpty = true;
            return std::make_unique<StatePrompt>();
        }
        
        size_t last = input.find_last_not_of(" \t");
        input = input.substr(first, (last - first + 1));
        
        // Separation: Print newline immediately after input is done
        // (Handled by InputHandler::poll in LineReady case usually, but ensures consistency)
        
        return std::make_unique<StateExecute>(input);
    } 
    else if (result == InputHandler::PollResult::Cancelled) {
        // Ctrl+C pressed
        ctx.previousCommandWasEmpty = true; // Avoid double newline
        return std::make_unique<StatePrompt>();
    }
    
    // CONTINUE LOOPING (Recursion via trampoline)
    return std::make_unique<StateReadInput>(std::move(handler));
}

inline std::unique_ptr<Continuation> StateExecute::run(ShellContext& ctx) {
    execute_command_logic(ctx, inputLine);

    if (!ctx.running) return nullptr;

    return std::make_unique<StatePrompt>();
}

#endif // LINUXIFY_ENGINE_STATES_HPP
