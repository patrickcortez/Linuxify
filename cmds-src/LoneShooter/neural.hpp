// neural.hpp - Neuroevolution AI for LoneShooter
// Include after Enemy struct is declared
// Enemies learn to counter player over generations

#ifndef NEURAL_HPP
#define NEURAL_HPP

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace NeuralAI {

const int INPUT_COUNT = 8;
const int HIDDEN_COUNT = 6;
const int OUTPUT_COUNT = 4;
const float MUTATION_RATE = 0.15f;
const float MUTATION_STRENGTH = 0.3f;

struct NeuralNet {
    float weightsIH[INPUT_COUNT][HIDDEN_COUNT];
    float weightsHO[HIDDEN_COUNT][OUTPUT_COUNT];
    float biasH[HIDDEN_COUNT];
    float biasO[OUTPUT_COUNT];
    float fitness;
    float survivalTime;
    float damageDealt;
    
    void Randomize() {
        for (int i = 0; i < INPUT_COUNT; i++) {
            for (int h = 0; h < HIDDEN_COUNT; h++) {
                weightsIH[i][h] = ((rand() % 2000) - 1000) / 1000.0f;
            }
        }
        for (int h = 0; h < HIDDEN_COUNT; h++) {
            for (int o = 0; o < OUTPUT_COUNT; o++) {
                weightsHO[h][o] = ((rand() % 2000) - 1000) / 1000.0f;
            }
            biasH[h] = ((rand() % 1000) - 500) / 1000.0f;
        }
        for (int o = 0; o < OUTPUT_COUNT; o++) {
            biasO[o] = ((rand() % 1000) - 500) / 1000.0f;
        }
        fitness = 0;
        survivalTime = 0;
        damageDealt = 0;
    }
    
    void CopyFrom(const NeuralNet& other) {
        memcpy(weightsIH, other.weightsIH, sizeof(weightsIH));
        memcpy(weightsHO, other.weightsHO, sizeof(weightsHO));
        memcpy(biasH, other.biasH, sizeof(biasH));
        memcpy(biasO, other.biasO, sizeof(biasO));
        fitness = 0;
        survivalTime = 0;
        damageDealt = 0;
    }
    
    void Mutate() {
        for (int i = 0; i < INPUT_COUNT; i++) {
            for (int h = 0; h < HIDDEN_COUNT; h++) {
                if ((rand() % 100) < (int)(MUTATION_RATE * 100)) {
                    weightsIH[i][h] += ((rand() % 2000) - 1000) / 1000.0f * MUTATION_STRENGTH;
                    if (weightsIH[i][h] > 2.0f) weightsIH[i][h] = 2.0f;
                    if (weightsIH[i][h] < -2.0f) weightsIH[i][h] = -2.0f;
                }
            }
        }
        for (int h = 0; h < HIDDEN_COUNT; h++) {
            for (int o = 0; o < OUTPUT_COUNT; o++) {
                if ((rand() % 100) < (int)(MUTATION_RATE * 100)) {
                    weightsHO[h][o] += ((rand() % 2000) - 1000) / 1000.0f * MUTATION_STRENGTH;
                    if (weightsHO[h][o] > 2.0f) weightsHO[h][o] = 2.0f;
                    if (weightsHO[h][o] < -2.0f) weightsHO[h][o] = -2.0f;
                }
            }
            if ((rand() % 100) < (int)(MUTATION_RATE * 100)) {
                biasH[h] += ((rand() % 1000) - 500) / 1000.0f * MUTATION_STRENGTH;
            }
        }
        for (int o = 0; o < OUTPUT_COUNT; o++) {
            if ((rand() % 100) < (int)(MUTATION_RATE * 100)) {
                biasO[o] += ((rand() % 1000) - 500) / 1000.0f * MUTATION_STRENGTH;
            }
        }
    }
    
    void Evaluate(float inputs[INPUT_COUNT], float outputs[OUTPUT_COUNT]) {
        float hidden[HIDDEN_COUNT];
        
        for (int h = 0; h < HIDDEN_COUNT; h++) {
            float sum = biasH[h];
            for (int i = 0; i < INPUT_COUNT; i++) {
                sum += inputs[i] * weightsIH[i][h];
            }
            hidden[h] = tanhf(sum);
        }
        
        for (int o = 0; o < OUTPUT_COUNT; o++) {
            float sum = biasO[o];
            for (int h = 0; h < HIDDEN_COUNT; h++) {
                sum += hidden[h] * weightsHO[h][o];
            }
            outputs[o] = tanhf(sum);
        }
    }
    
    void UpdateFitness() {
        fitness = survivalTime + damageDealt * 10.0f;
    }
};

static NeuralNet globalBestBrain;
static float globalBestFitness = 0;
static int generation = 1;
static bool brainInitialized = false;

inline void InitGlobalBrain() {
    if (!brainInitialized) {
        globalBestBrain.Randomize();
        brainInitialized = true;
    }
}

inline void UpdateGlobalBest(NeuralNet& brain) {
    brain.UpdateFitness();
    if (brain.fitness > globalBestFitness) {
        globalBestFitness = brain.fitness;
        globalBestBrain.CopyFrom(brain);
        globalBestBrain.fitness = brain.fitness;
    }
}

inline void InheritBrain(NeuralNet& brain) {
    InitGlobalBrain();
    if (globalBestFitness > 5.0f) {
        brain.CopyFrom(globalBestBrain);
        brain.Mutate();
    } else {
        brain.Randomize();
    }
}

inline void NextGeneration() {
    generation++;
}

inline int GetGeneration() {
    return generation;
}

inline float GetBestFitness() {
    return globalBestFitness;
}

}

#endif
