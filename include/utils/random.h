#pragma once

#include <random>
#include <chrono>

class Random {
public:
    Random() {
        std::random_device rd;
        m_generator.seed(rd() ^ 
            static_cast<unsigned long long>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count()
            )
        );
    }

    std::mt19937& getGenerator() {
        return m_generator;
    }

    double generateProbability() {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(m_generator);
    }

private:
    std::mt19937 m_generator;
}; 