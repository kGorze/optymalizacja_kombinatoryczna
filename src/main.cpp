#include <iostream>
#include <random>
#include <chrono>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <memory>
#include <ctime>

// Pliki projektu
#include "generator/dna_generator.h"
#include "naive/naive_reconstruction.h"
#include "benchmark/naive_benchmark.h"
#include "utils/utility_functions.h"

#include "metaheuristics/crossover.h"
#include "metaheuristics/fitness.h"
#include "metaheuristics/genetic_algorithm.h"
#include "metaheuristics/mutation.h"
#include "metaheuristics/replacement.h"
#include "metaheuristics/selection.h"
#include "metaheuristics/stopping_criteria.h"

#include "benchmark/crossover_benchmark.h"

// Nowy include do konfiguracji AG:
#include "configuration/genetic_algorithm_configuration.h"

#include "metaheuristics/adaptive_crossover.h"
#include "benchmark/adaptive_crossover_benchmark.h"

// Pliki do tuningu:
#include "tuning/parameter_tuning_manager.h"
#include "tuning/parameters_parser.h"
#include "tuning/tuning_structures.h"
#include "tuning/racing.h"
#include "tuning/meta_ea.h"

#include "utils/logging.h"

#include "metaheuristics/population_cache.h"

// Start your main code here, remove the Logger class definition

// Funkcja do wypisywania użycia programu:
void printUsage() {
    std::cout << "Usage: dna_reconstruction <mode> [options]\n\n"
              << "Modes:\n"
              << "  debug             - Run in debug mode with default settings\n"
              << "  generate_instance - Generate test instances\n"
              << "  test_instance     - Solve DNA reconstruction from input file\n\n"
              << "  tuning            - Run parameter tuning\n"
              << "  tuning_hybrid     - Run advanced hybrid (1+lambda) ES + Racing\n\n"
              << "Options for generate_instance:\n"
              << "  -n <value>        - DNA length (300-700)\n"
              << "  -k <value>        - Oligo length (7-10)\n"
              << "  -dk <value>       - Delta K range (0-2)\n"
              << "  -ln <value>       - Negative errors (0 or >=10)\n"
              << "  -lp <value>       - Positive errors (0 or >=10)\n"
              << "  -o <filename>     - Output filename\n\n"
              << "Options for test_instance:\n"
              << "  -i <filename>     - Input instance file\n"
              << "  -o <filename>     - Output results file\n"
              << "  -pid <value>      - Process ID (unique identifier)\n"
              << "  -diff <string>    - Difficulty/test name (e.g. Easy, Medium, Hard)\n\n"
              << "Options for tuning:\n"
              << "  -cfg <file>       - Path to GA config file (key=value style)\n"
              << "  -out <file>       - Output CSV file for tuning results\n\n"
              << "Options for tuning_hybrid:\n"
              << "  (same as tuning, but uses the new HybridOnePlusLambdaEA approach)\n\n"
              << "Additional option for any mode:\n"
              << "  -cfg <file>       - Path to GA config file (key=value style)\n";
}

// Funkcja pomocnicza do generowania instancji
bool generateInstance(int n, int k, int deltaK, int lNeg, int lPoz, const std::string &outputFile)
{
    DNAInstanceBuilder builder;
    builder.setN(n)
           .setK(k)
           .setDeltaK(deltaK)
           .setLNeg(lNeg)
           .setLPoz(lPoz)
           .setRepAllowed(true)
           .setProbablePositive(0)
           .buildDNA()
           .buildSpectrum();

    DNAInstance instance = builder.getInstance();
    
    // Introduce errors AFTER setting start index
    auto startFrag = instance.getDNA().substr(0, k);
    const auto& spectrum = instance.getSpectrum();
    
    int startIdx = -1;
    for (int i = 0; i < (int)spectrum.size(); i++) {
        if (spectrum[i] == startFrag) {
            startIdx = i;
            break;
        }
    }
    
    instance.setStartIndex(startIdx);

    // Now introduce errors
    if (lNeg > 0) {
        NegativeErrorIntroducer negErr(lNeg);
        negErr.introduceErrors(instance);
    }

    if (lPoz > 0) {
        PositiveErrorIntroducer posErr(lPoz);
        posErr.introduceErrors(instance);
    }

    return InstanceIO::saveInstance(instance, outputFile);
}

void updateConfigWithInstanceParams(const DNAInstance& instance) {
    auto& config = GAConfig::getInstance();
    // Update all instance parameters
    config.k = instance.getK();
    config.deltaK = instance.getDeltaK();
    config.lNeg = instance.getLNeg();
    config.lPoz = instance.getLPoz();
    config.repAllowed = instance.isRepAllowed();
    config.probablePositive = instance.getProbablePositive();
    
    LOG_DEBUG("Updated instance parameters: k=" + std::to_string(config.k) + 
              ", deltaK=" + std::to_string(config.deltaK) +
              ", lNeg=" + std::to_string(config.lNeg) +
              ", lPoz=" + std::to_string(config.lPoz));
}

int main(int argc, char* argv[]) {
    LOG_INFO("Starting program");

    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string mode = argv[1];
    // Domyślna ścieżka do pliku config
    std::string configFile = "config.cfg";

    // Parsujemy argumenty w poszukiwaniu -cfg
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "-cfg") == 0) {
            if (i + 1 < argc) {
                configFile = argv[i + 1];
                ++i; // pomijamy wartość
            } else {
                std::cerr << "Error: -cfg requires a file path.\n";
                return 1;
            }
        }
    }

    if (argc > 1 && strcmp(argv[1], "debug") == 0) {
        // Create a debug instance first
        DNAInstanceBuilder builder;
        builder.setN(400)
               .setK(8)
               .setDeltaK(2)
               .setLNeg(0)
               .setLPoz(0)
               .setRepAllowed(true)
               .setProbablePositive(0)
               .buildDNA()
               .buildSpectrum();

        DNAInstance instance = builder.getInstance();

        // Save debug instance
        std::string debugInstanceFile = "debug_instance.txt";
        if (!InstanceIO::saveInstance(instance, debugInstanceFile)) {
            std::cerr << "Failed to save debug instance!\n";
            return 1;
        }

        // Now load config and update with instance params
        auto& config = GAConfig::getInstance();
        if (!config.loadFromFile("config.cfg")) {
            std::cerr << "Failed to load GA configuration\n";
            return 1;
        }
        updateConfigWithInstanceParams(instance);

        // Create and run GA
        auto cache = std::make_shared<CachedPopulation>();
        config.setCache(cache);

        // Run GA with the debug instance
        runGeneticAlgorithm(instance, "debug_output.txt", 0, "DEBUG");

        return 0;

    } else if (mode == "generate_instance") {
        int n = 400, k = 8, deltaK = 1, lNeg = 10, lPoz = 10;
        std::string outputFile = "generated_instance.txt";

        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "-n") == 0) {
                if (i + 1 < argc) {
                    n = std::stoi(argv[i + 1]);
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-k") == 0) {
                if (i + 1 < argc) {
                    k = std::stoi(argv[i + 1]);
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-dk") == 0) {
                if (i + 1 < argc) {
                    deltaK = std::stoi(argv[i + 1]);
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-ln") == 0) {
                if (i + 1 < argc) {
                    lNeg = std::stoi(argv[i + 1]);
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-lp") == 0) {
                if (i + 1 < argc) {
                    lPoz = std::stoi(argv[i + 1]);
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-o") == 0) {
                if (i + 1 < argc) {
                    outputFile = argv[i + 1];
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-cfg") == 0) {
                ++i; // skip
            }
        }

        if (!generateInstance(n, k, deltaK, lNeg, lPoz, outputFile)) {
            std::cerr << "Failed to generate instance!\n";
            return 1;
        }
        std::cout << "Instance generated successfully: " << outputFile << std::endl;

    } else if (mode == "test_instance") {
        std::string inputFile;
        std::string outputFile = "results.txt";
        int processId = 0;
        std::string difficulty = "Unknown";

        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "-i") == 0) {
                if (i + 1 < argc) {
                    inputFile = argv[i + 1];
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-o") == 0) {
                if (i + 1 < argc) {
                    outputFile = argv[i + 1];
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-pid") == 0) {
                if (i + 1 < argc) {
                    processId = std::stoi(argv[i + 1]);
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-diff") == 0) {
                if (i + 1 < argc) {
                    difficulty = argv[i + 1];
                    ++i;
                }
            } else if (std::strcmp(argv[i], "-cfg") == 0) {
                ++i; // skip
            }
        }

        if (inputFile.empty()) {
            std::cerr << "Input file must be specified for test_instance mode!\n";
            printUsage();
            return 1;
        }

        // Load instance first
        DNAInstance instance;
        if (!InstanceIO::loadInstance(inputFile, instance)) {
            std::cerr << "Failed to load instance from " << inputFile << std::endl;
            return 1;
        }

        // Now load config and update with instance params
        auto& config = GAConfig::getInstance();
        if (!config.loadFromFile("config.cfg")) {
            std::cerr << "Failed to load GA configuration\n";
            return 1;
        }
        updateConfigWithInstanceParams(instance);

        // Create and run GA
        auto cache = std::make_shared<CachedPopulation>();
        config.setCache(cache);

        // Run GA with the loaded instance
        runGeneticAlgorithm(instance, outputFile, processId, difficulty);

        return 0;

    } else if (mode == "tuning") {
        std::string tuningOutputFile = "tuning_results.csv";

        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "-out") == 0) {
                if (i + 1 < argc) {
                    tuningOutputFile = argv[i + 1];
                    ++i;
                } else {
                    std::cerr << "Error: -out requires a filename.\n";
                    return 1;
                }
            }
        }

        ParameterTuningManager tuner(tuningOutputFile);

        // Przykładowa lista kandydatów
        std::vector<ParameterSet> candidateParams = ParameterParser::generateGridOfCandidatesWithout();

        Racing::Configuration rc;
        rc.significanceLevel = 0.05;
        rc.maxTrialsPerCandidate = 20;
        rc.minTrialsBeforeElimination = 5;
        rc.useBootstrap = false;
        rc.bootstrapSamples = 2000;

        // Funkcja ewaluacji (korzysta z runGeneticAlgorithm)
        auto evaluateFunc = [&](const ParameterSet &ps) -> TuningResult {
            // Load instance parameters from file or use builder
            DNAInstance instance;
            if (!InstanceIO::loadInstance("test_instance.txt", instance)) {
                // If no test instance file, create one with reasonable parameters
                DNAInstanceBuilder builder;
                builder.setN(300)
                       .setK(8)  // Default k if no instance provided
                       .setDeltaK(1)
                       .setLNeg(0)
                       .setLPoz(0)
                       .setRepAllowed(true)
                       .buildDNA()
                       .buildSpectrum();
                instance = builder.getInstance();
            }
            
            // Update config with instance-specific parameters
            updateConfigWithInstanceParams(instance);
            
            // Update GA parameters from parameter set
            GAConfig &cfg = GAConfig::getInstance();
            for (const auto &[key, value] : ps.params) {
                if (key == "populationSize") {
                    cfg.populationSize = std::stoi(value);
                } else if (key == "mutationRate") {
                    cfg.mutationRate = std::stod(value);
                }
                // Don't override k-mer related parameters here
            }
            
            // 3. Start pomiaru czasu
            auto start = std::chrono::high_resolution_clock::now();

            // 4. Uruchomienie Algorytmu Genetycznego
            runGeneticAlgorithm(instance, "temp_output.txt", 0, "Tuning");

            // 5. Zakończenie pomiaru czasu
            auto end = std::chrono::high_resolution_clock::now();
            double durationSec = std::chrono::duration<double>(end - start).count();

            // 6. Odczytanie finalnego fitness z GAConfig
            double finalFitness = cfg.getGlobalBestFitness();

            // 7. Stworzenie wyniku tuningu
            TuningResult tr; 
            tr.parameterSet = ps;
            tr.fitness = finalFitness;
            tr.executionTime = durationSec;

            return tr;
        };

        // Uruchamiamy Racing
        tuner.runRacingOnly(candidateParams, rc, evaluateFunc);

        std::cout << "Parameter tuning completed. Results saved to " << tuningOutputFile << std::endl;

    } 
    else if (mode == "tuning_hybrid") {
        std::string tuningOutputFile = "tuning_hybrid_results.csv";
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "-out") == 0) {
                if (i + 1 < argc) {
                    tuningOutputFile = argv[i + 1];
                    ++i;
                } else {
                    std::cerr << "Error: -out requires a filename.\n";
                    return 1;
                }
            }
        }

        ParameterTuningManager tuner(tuningOutputFile);

        Racing::Configuration rc;
        rc.significanceLevel = 0.05;
        rc.maxTrialsPerCandidate = 10;
        rc.minTrialsBeforeElimination = 3;
        rc.useBootstrap = true;
        rc.bootstrapSamples = 1000;

        auto evaluateFunc = [&](const ParameterSet &ps) -> TuningResult {
            // Generujemy instancję (dla przykładu)
            DNAInstanceBuilder builder;
            builder.setN(300)
                   .setK(7)
                   .setDeltaK(2)
                   .setLNeg(0)
                   .setLPoz(0)
                   .setRepAllowed(true)
                   .buildDNA()
                   .buildSpectrum();
            DNAInstance instance = builder.getInstance();

            // Ustawiamy parametry GA
            auto &cfg = GAConfig::getInstance();
            for (const auto &[key, value] : ps.params) {
                if (key == "populationSize") {
                    cfg.populationSize = std::stoi(value);
                } else if (key == "mutationRate") {
                    cfg.mutationRate = std::stod(value);
                }
            }

            // Symulacja hybrydy (1+lambda) ES, w tym ponownie AG
            HybridOnePlusLambdaEA hybrid;
            ParameterSet bestSet = hybrid.runHybridOnePlusLambdaEA(ps, instance);

            double finalFitness = cfg.getGlobalBestFitness();

            TuningResult tr;
            tr.parameterSet = ps;
            tr.fitness = finalFitness;
            tr.executionTime = 0.0; // ewentualnie pomiar w HybridOnePlusLambdaEA
            return tr;
        };

        // Paru kandydatów
        std::vector<ParameterSet> candidateParams = ParameterParser::generateGridOfCandidatesWithout();

        // Racing
        tuner.runRacingOnly(candidateParams, rc, evaluateFunc);

        std::cout << "Hybrid (1+lambda) tuning completed. Results: " << tuningOutputFile << "\n";
    }
    else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        printUsage();
        return 1;
    }

    return 0;
}
