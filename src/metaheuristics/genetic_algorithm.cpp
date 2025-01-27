#include "../../include/metaheuristics/genetic_algorithm.h"
#include "../../include/configuration/genetic_algorithm_configuration.h"
#include "../../include/metaheuristics/genetic_algorithm_runner.h"
#include "../../include/utils/logging.h"
#include <iostream>
#include <random>
#include <chrono>
#include <algorithm>
#include <mutex>
#include <string>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <cmath>

namespace {
    // Helper function for safe string conversion
    template<typename T>
    std::string safeToString(const T& value) {
        try {
            return std::to_string(value);
        } catch (const std::exception&) {
            return "error";
        }
    }

    std::mutex s_outputMutex;
}

GeneticAlgorithm::GeneticAlgorithm(
    std::unique_ptr<IRepresentation> representation,
    const GAConfig& config,
    bool debugMode)
    : m_representation(std::move(representation))
    , m_config(config)
    , m_random(std::make_unique<Random>())
    , m_globalBestFit(-std::numeric_limits<double>::infinity())
    , m_bestFitness(0.0)
    , m_theoreticalMaxFitness(1.0)  // Initialize with default value
    , m_debugMode(debugMode)
{
    if (!m_representation) {
        throw std::invalid_argument("Invalid representation pointer");
    }
}

std::string GeneticAlgorithm::run(const DNAInstance& instance) {
    try {
        if (!m_representation) {
            LOG_ERROR("No representation set for genetic algorithm");
            return "";
        }
        
        LOG_DEBUG("Initializing genetic algorithm run");
        int generation = 0;
        calculateTheoreticalMaxFitness(instance);
        m_globalBestFit = -std::numeric_limits<double>::infinity();
        int bestLevenshteinDistance = std::numeric_limits<int>::max();
        std::string bestDNASequence;
        
        LOG_DEBUG("Initializing population");
        initializePopulation(m_config.getPopulationSize(), instance);
        
        auto sharedRepresentation = std::shared_ptr<IRepresentation>(
            m_representation.release(),
            [](IRepresentation* ptr) { delete ptr; }
        );
        
        auto stopping = m_config.getStopping();
        int stagnationCount = 0;
        double previousBestFitness = -std::numeric_limits<double>::infinity();
        
        int crossoverAttempts = 0;
        int successfulCrossovers = 0;
        int mutationAttempts = 0;
        int successfulMutations = 0;
        
        LOG_DEBUG("Starting main evolution loop");
        while (!stopping->shouldStop(generation, m_globalBestFit)) {
            LOG_DEBUG("Generation " + std::to_string(generation) + " starting");
            
            // Selection
            LOG_DEBUG("Performing selection");
            std::vector<std::shared_ptr<Individual>> parents;
            parents.reserve(m_config.getPopulationSize());
            
            auto selection = m_config.getSelection();
            auto fitness = m_config.getFitness();
            
            auto selectedParents = selection->select(m_population, instance, fitness, sharedRepresentation);
            if (selectedParents.empty()) {
                LOG_ERROR("Selection produced no parents - terminating");
                break;
            }
            parents = std::move(selectedParents);
            LOG_DEBUG("Selected " + std::to_string(parents.size()) + " parents");
            
            // Crossover and Mutation
            LOG_DEBUG("Performing crossover and mutation");
            std::vector<std::shared_ptr<Individual>> offspring;
            offspring.reserve(parents.size());
            
            for (size_t i = 0; i < parents.size() - 1; i += 2) {
                std::vector<std::shared_ptr<Individual>> parentPair = {parents[i], parents[i + 1]};
                bool offspringAdded = false;
                
                if (m_random->generateProbability() < m_config.getCrossoverProbability()) {
                    crossoverAttempts++;
                    auto crossover = m_config.getCrossover("");
                    auto children = crossover->crossover(parentPair, instance, sharedRepresentation);
                    
                    if (!children.empty()) {
                        offspring.insert(offspring.end(), children.begin(), children.end());
                        successfulCrossovers++;
                        offspringAdded = true;
                        LOG_DEBUG("Crossover successful - added " + std::to_string(children.size()) + " children");
                    } else {
                        LOG_DEBUG("Crossover failed to produce valid offspring");
                    }
                }
                
                if (!offspringAdded) {
                    offspring.push_back(parents[i]);
                    offspring.push_back(parents[i + 1]);
                    LOG_DEBUG("Using parents as offspring due to failed/skipped crossover");
                }
            }
            
            if (parents.size() % 2 == 1) {
                offspring.push_back(parents.back());
            }
            
            LOG_DEBUG("Performing mutations");
            auto mutation = m_config.getMutation();
            for (auto& individual : offspring) {
                if (m_random->generateProbability() < m_config.getMutationRate()) {
                    mutationAttempts++;
                    
                    auto originalGenes = individual->getGenes();
                    bool mutationSuccessful = false;
                    
                    for (int attempt = 0; attempt < 3 && !mutationSuccessful; attempt++) {
                        try {
                            mutation->mutate(individual, instance, sharedRepresentation);
                            if (sharedRepresentation->isValid(individual, instance)) {
                                mutationSuccessful = true;
                                successfulMutations++;
                                LOG_DEBUG("Mutation successful on attempt " + std::to_string(attempt + 1));
                            } else {
                                individual = std::make_shared<Individual>(originalGenes);
                                LOG_DEBUG("Mutation produced invalid individual - reverting");
                            }
                        } catch (const std::exception& e) {
                            LOG_DEBUG("Mutation attempt " + std::to_string(attempt + 1) + 
                                    " failed: " + std::string(e.what()));
                            individual = std::make_shared<Individual>(originalGenes);
                        }
                    }
                }
            }
            
            LOG_DEBUG("Evaluating offspring population");
            evaluatePopulation(instance, offspring, sharedRepresentation);
            
            LOG_DEBUG("Performing replacement");
            auto replacement = m_config.getReplacement();
            m_population = replacement->replace(m_population, offspring, instance, sharedRepresentation);
            
            if (updateGlobalBest(m_population, instance)) {
                auto bestIndividual = m_population[m_bestIndex];
                std::string currentDNA = sharedRepresentation->toDNA(bestIndividual, instance);
                
                if (!currentDNA.empty()) {
                    int currentDistance = calculateLevenshteinDistance(currentDNA, instance.getOriginalDNA());
                    
                    if (currentDistance < bestLevenshteinDistance || bestDNASequence.empty()) {
                        bestLevenshteinDistance = currentDistance;
                        bestDNASequence = currentDNA;
                        
                        std::stringstream ss;
                        ss << "New best solution - Normalized Fitness: " << std::fixed << std::setprecision(4) 
                           << (m_globalBestFit / m_theoreticalMaxFitness)
                           << ", Levenshtein distance: " << bestLevenshteinDistance
                           << ", DNA length: " << currentDNA.length();
                        LOG_INFO(ss.str());
                    }
                } else {
                    LOG_WARNING("Best individual produced empty DNA sequence");
                }
            }
            
            if (std::abs(m_globalBestFit - previousBestFitness) < 1e-6) {
                stagnationCount++;
                LOG_DEBUG("Stagnation count increased to " + std::to_string(stagnationCount));
            } else {
                stagnationCount = 0;
                previousBestFitness = m_globalBestFit;
                LOG_DEBUG("Fitness improved - resetting stagnation count");
            }
            
            if (m_debugMode) {
                std::stringstream ss;
                ss << "Operation stats - Crossover: " << successfulCrossovers << "/" << crossoverAttempts
                   << " successful, Mutation: " << successfulMutations << "/" << mutationAttempts 
                   << " successful, Stagnation count: " << stagnationCount;
                LOG_DEBUG(ss.str());
            }
            
            generation++;
            LOG_DEBUG("Generation " + std::to_string(generation) + " completed");
        }
        
        std::stringstream ss;
        ss << "\nFinal Results:";
        ss << "\n- Best Normalized Fitness: " << std::fixed << std::setprecision(4) 
           << (m_globalBestFit / m_theoreticalMaxFitness);
        if (!bestDNASequence.empty()) {
            ss << "\n- Best Levenshtein Distance: " << bestLevenshteinDistance;
            ss << "\n- Best DNA Length: " << bestDNASequence.length();
        } else {
            ss << "\n- No valid DNA sequence found";
        }
        ss << "\n- Total Generations: " << generation;
        LOG_INFO(ss.str());
        
        return bestDNASequence;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error in genetic algorithm: " + std::string(e.what()));
        return "";
    } catch (...) {
        LOG_ERROR("Unknown error in genetic algorithm");
        return "";
    }
}

void GeneticAlgorithm::initializePopulation(int popSize, const DNAInstance& instance) {
    if (!m_representation) {
        LOG_ERROR("Null representation in initializePopulation");
        return;
    }
    
    m_population.clear();
    m_population = m_representation->initializePopulation(popSize, instance);
    
    // Create a temporary shared_ptr for evaluation
    auto tempRepresentation = std::shared_ptr<IRepresentation>(
        m_representation.get(),
        [](IRepresentation*){} // Empty deleter since we don't own the pointer
    );
    
    evaluatePopulation(instance, m_population, tempRepresentation);
    updateGlobalBest(m_population, instance);
}

void GeneticAlgorithm::evaluatePopulation(
    const DNAInstance& instance,
    const std::vector<std::shared_ptr<Individual>>& population,
    const std::shared_ptr<IRepresentation>& representation)
{
    if (!representation) {
        LOG_ERROR("Null representation in evaluatePopulation");
        return;
    }
    
    auto fitness = m_config.getFitness();
    if (!fitness) {
        LOG_ERROR("Null fitness in evaluatePopulation");
        return;
    }
    
    double bestFitnessInGeneration = -std::numeric_limits<double>::infinity();
    
    for (auto& individual : population) {
        if (!individual) {
            LOG_ERROR("Null individual in evaluatePopulation");
            continue;
        }
        double fitnessValue = fitness->calculateFitness(individual, instance, representation);
        individual->setFitness(fitnessValue);
        
        // Only log if this is a new best fitness
        if (fitnessValue > bestFitnessInGeneration) {
            bestFitnessInGeneration = fitnessValue;
            if (fitnessValue > m_globalBestFit) {
                LOG_DEBUG("New best fitness: " + std::to_string(fitnessValue));
            }
        }
    }
}

bool GeneticAlgorithm::updateGlobalBest(
    const std::vector<std::shared_ptr<Individual>>& population,
    [[maybe_unused]] const DNAInstance& instance)
{
    bool improved = false;
    m_bestIndex = -1;
    
    for (size_t i = 0; i < population.size(); i++) {
        if (!population[i]) continue;
        
        double fitness = population[i]->getFitness();
        if (fitness > m_globalBestFit) {
            m_globalBestFit = fitness;
            m_bestFitness = fitness;
            m_globalBestGenes = population[i]->getGenes();
            m_bestDNA = vectorToString(population[i]->getGenes());
            m_bestIndex = i;
            improved = true;
        }
    }
    
    return improved;
}

void GeneticAlgorithm::logGenerationStats(
    const std::vector<std::shared_ptr<Individual>>& population,
    [[maybe_unused]] const DNAInstance& instance,
    int generation)
{
    double sumFitness = 0.0;
    double minFitness = std::numeric_limits<double>::infinity();
    double maxFitness = -std::numeric_limits<double>::infinity();
    int validCount = 0;
    
    for (const auto& individual : population) {
        if (!individual) continue;
        
        double fitness = individual->getFitness();
        sumFitness += fitness;
        minFitness = std::min(minFitness, fitness);
        maxFitness = std::max(maxFitness, fitness);
        validCount++;
    }
    
    double avgFitness = validCount > 0 ? sumFitness / validCount : 0.0;
    bool improved = maxFitness > m_globalBestFit;
    
    // Only log if there's improvement or every 10 generations
    if (improved || generation % 10 == 0) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "Gen " << generation << ": ";
        ss << "Best=" << maxFitness;
        if (improved) {
            ss << " (↑ from " << m_globalBestFit << ")";
        }
        ss << " Avg=" << avgFitness;
        if (validCount < static_cast<int>(population.size())) {
            ss << " [" << validCount << "/" << population.size() << " valid]";
        }
        LOG_INFO(ss.str());
    }
}

std::string GeneticAlgorithm::vectorToString(const std::vector<int>& vec) {
    if (vec.empty()) return "";
    
    std::stringstream ss;
    for (size_t i = 0; i < vec.size(); i++) {
        if (i > 0) ss << " ";
        ss << vec[i];
    }
    return ss.str();
}

void GeneticAlgorithm::calculateTheoreticalMaxFitness(const DNAInstance& instance) {
    // Calculate theoretical maximum fitness based on instance parameters
    int k = instance.getK();
    int n = instance.getN();
    int spectrumSize = instance.getSpectrum().size();
    
    if (k <= 0 || n <= 0 || spectrumSize <= 0) {
        LOG_ERROR("Invalid instance parameters for theoretical fitness calculation");
        m_theoreticalMaxFitness = 1.0;  // Default value
        return;
    }
    
    // Maximum possible score components
    double maxConnectivityScore = spectrumSize - 1;  // All k-mers perfectly connected
    double maxSpectrumCoverage = spectrumSize;       // All k-mers used
    double maxLengthScore = 1.0;                     // Perfect length match
    
    // Combine components with weights
    m_theoreticalMaxFitness = maxConnectivityScore + maxSpectrumCoverage + maxLengthScore;
    
    LOG_INFO("Theoretical maximum fitness calculated: " + std::to_string(m_theoreticalMaxFitness));
}

std::vector<std::vector<PreprocessedEdge>> GeneticAlgorithm::buildAdjacencyMatrix(
    const DNAInstance& instance) const {
    const auto& spectrum = instance.getSpectrum();
    const int k = instance.getK();
    const int deltaK = instance.getDeltaK();
    
    std::vector<std::vector<PreprocessedEdge>> adjacencyMatrix(
        spectrum.size(),
        std::vector<PreprocessedEdge>(spectrum.size())
    );
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        for (size_t j = 0; j < spectrum.size(); ++j) {
            if (i != j) {
                int weight = calculateEdgeWeight(spectrum[i], spectrum[j], k);
                adjacencyMatrix[i][j] = PreprocessedEdge(j, weight, weight >= k - deltaK);
            }
        }
    }
    
    return adjacencyMatrix;
}

int GeneticAlgorithm::calculateEdgeWeight(const std::string& from, const std::string& to, int k) const {
    if (from.length() < static_cast<size_t>(k - 1) || to.length() < static_cast<size_t>(k)) return 0;
    
    std::string suffix = from.substr(from.length() - (k - 1));
    std::string prefix = to.substr(0, k - 1);
    
    return (suffix == prefix) ? 1 : 0;
}

// Add helper method for Levenshtein distance calculation
int GeneticAlgorithm::calculateLevenshteinDistance(const std::string& s1, const std::string& s2) {
    const size_t m = s1.length();
    const size_t n = s2.length();
    std::vector<std::vector<int>> d(m + 1, std::vector<int>(n + 1));

    // Initialize first row and column
    for (size_t i = 0; i <= m; i++) d[i][0] = i;
    for (size_t j = 0; j <= n; j++) d[0][j] = j;

    // Fill in the rest of the matrix
    for (size_t i = 1; i <= m; i++) {
        for (size_t j = 1; j <= n; j++) {
            if (s1[i-1] == s2[j-1]) {
                d[i][j] = d[i-1][j-1];
            } else {
                d[i][j] = 1 + std::min({d[i-1][j],      // deletion
                                      d[i][j-1],      // insertion
                                      d[i-1][j-1]});  // substitution
            }
        }
    }

    return d[m][n];
}