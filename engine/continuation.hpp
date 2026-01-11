#ifndef LINUXIFY_ENGINE_CONTINUATION_HPP
#define LINUXIFY_ENGINE_CONTINUATION_HPP

#include <memory>
#include <string>

// Forward declaration of the shared context
struct ShellContext;

/**
 * @brief The Unit of Execution (The Node)
 * 
 * Represents a single atomic state of the shell.
 * The 'run' method executes the logic for this state and
 * returns the NEXT state to transition to.
 */
struct Continuation {
    virtual ~Continuation() = default;

    /**
     * @brief Execute the state's logic.
     * 
     * @param context The shared/persistent shell data (Env, History, etc.)
     * @return std::unique_ptr<Continuation> The next state, or nullptr to terminate.
     */
    virtual std::unique_ptr<Continuation> run(ShellContext& context) = 0;
    
    // For debugging visualization
    virtual std::string getName() const { return "Continuation"; }
};

/**
 * @brief The Dispatcher (The Trampoline)
 * 
 * Runs the state machine. It knows HOW to run, but not WHAT to run.
 * This loop is stackless (iterative), preventing stack overflow
 * regardless of session length.
 */
class ShellEngine {
public:
    void execute(std::unique_ptr<Continuation> startNode, ShellContext& context) {
        auto current = std::move(startNode);
        while (current) {
            // "Trampoline" bounce: Execute current, get next
            current = current->run(context);
        }
    }
};

#endif // LINUXIFY_ENGINE_CONTINUATION_HPP
