#pragma once

#include "dna/dna_instance.h"
#include "generator/dna_generator.h"
#include "error_introduction.h"
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <stdexcept>

class DNAInstanceBuilder {
public:
    DNAInstanceBuilder() = default;
    
    // Builder methods with validation
    DNAInstanceBuilder& setN(int value);
    
    DNAInstanceBuilder& setK(int value);
    
    DNAInstanceBuilder& setDeltaK(int value);
    
    DNAInstanceBuilder& setLNeg(int value);
    
    DNAInstanceBuilder& setLPoz(int value);
    
    DNAInstanceBuilder& setRepAllowed(bool value);
    
    DNAInstanceBuilder& setProbablePositive(int value);
    
    DNAInstanceBuilder& buildDNA();
    
    DNAInstanceBuilder& buildSpectrum();
    
    DNAInstanceBuilder& applyError(IErrorIntroductionStrategy* strategy);
    
    // Build method that returns a new instance
    DNAInstance build();
    
    // Get a reference to the instance being built
    const DNAInstance& getInstance() const { return m_instance; }
    
private:
    mutable std::mutex m_mutex;
    DNAInstance m_instance;
    DNAGenerator m_generator;
    int m_n = 0;
}; 