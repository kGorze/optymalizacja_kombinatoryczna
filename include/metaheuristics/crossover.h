//
// Created by konrad_guest on 07/01/2025.
// SMART

#ifndef CROSSOVER_H
#define CROSSOVER_H

#include <vector>
#include <memory>
#include <algorithm>
#include <iostream>
#include "metaheuristics/representation.h"
#include "generator/dna_generator.h"

/**
 * Każdy osobnik to std::shared_ptr<std::vector<int>>.
 */
class ICrossover {
public:
    virtual ~ICrossover() = default;

    virtual std::vector<std::shared_ptr<std::vector<int>>>  
    crossover(const std::vector<std::shared_ptr<std::vector<int>>>& parents,
              const DNAInstance &instance,
              std::shared_ptr<IRepresentation> representation) = 0;
};

// Różne krzyżowania
class OnePointCrossover : public ICrossover {
private:
    double m_crossoverRate;
    
    std::pair<std::shared_ptr<std::vector<int>>, std::shared_ptr<std::vector<int>>> 
    performCrossover(const std::shared_ptr<std::vector<int>>& parent1,
                    const std::shared_ptr<std::vector<int>>& parent2,
                    const DNAInstance& instance);

public:
    explicit OnePointCrossover(double crossoverRate = 0.8) : m_crossoverRate(crossoverRate) {}
    
    std::vector<std::shared_ptr<std::vector<int>>> crossover(
        const std::vector<std::shared_ptr<std::vector<int>>>& parents,
        const DNAInstance& instance,
        std::shared_ptr<IRepresentation> representation) override;
};

class OrderCrossover : public ICrossover {
public:
    std::vector<std::shared_ptr<std::vector<int>>> crossover(
        const std::vector<std::shared_ptr<std::vector<int>>>& parents,
        const DNAInstance& instance,
        std::shared_ptr<IRepresentation> representation) override;

private:


    std::vector<int> performOrderCrossover(
        const std::vector<int>& parent1,
        const std::vector<int>& parent2,
        size_t size);
};

class EdgeRecombination : public ICrossover {
public:
    std::vector<std::shared_ptr<std::vector<int>>> 
    crossover(const std::vector<std::shared_ptr<std::vector<int>>>& parents,
              const DNAInstance &instance,
              std::shared_ptr<IRepresentation> representation) override;
};

class PMXCrossover : public ICrossover {
public:
    std::vector<std::shared_ptr<std::vector<int>>> 
    crossover(const std::vector<std::shared_ptr<std::vector<int>>>& parents,
              const DNAInstance &instance,
              std::shared_ptr<IRepresentation> representation) override;
};

class DistancePreservingCrossover : public ICrossover {
public:
    std::vector<std::shared_ptr<std::vector<int>>> 
    crossover(const std::vector<std::shared_ptr<std::vector<int>>>& parents,
              const DNAInstance &instance,
              std::shared_ptr<IRepresentation> representation) override;
private:
    struct DistanceMatrix {
        std::vector<int> distances; // Zmienione z std::vector<std::vector<int>>
        explicit DistanceMatrix(const std::vector<int>& perm);
        int getDistance(int from, int to) const;
    };
};

bool isValidPermutation(const std::vector<int>& perm, int size);

#endif //CROSSOVER_H
