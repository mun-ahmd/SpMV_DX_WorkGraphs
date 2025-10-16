#pragma once 
#include <cmath>
#include <iostream>
#include <vector>

inline static bool compareWithTolerance(
    const std::vector<float>& vec_a,
    const std::vector<float>& reference,
    float atol = 1e-6f,
    float rtol = 1e-5f)
{
    if (vec_a.size() != reference.size()) {
        std::cerr << "Size mismatch: cpuData=" << vec_a.size()
            << ", reference=" << reference.size() << "\n";
        return false;
    }

    for (size_t i = 0; i < vec_a.size(); ++i) {
        float diff = std::fabs(vec_a[i] - reference[i]);
        float allowed = atol + rtol * std::fabs(reference[i]);
        if (diff > allowed) {
            std::cerr << "Mismatch at index " << i
                << ": cpu=" << vec_a[i]
                << ", ref=" << reference[i]
                << ", diff=" << diff
                << ", allowed=" << allowed << "\n";
            return false;
        }
    }
    return true;
}

// Example usage
/*
std::vector<float> cpuData = ...; // loaded from readback
std::vector<float> refData = activeSpMV.mult_result;
if (compareWithTolerance(cpuData, refData)) {
    std::cout << "Check passed!\n";
} else {
    std::cout << "Check failed!\n";
}
*/
