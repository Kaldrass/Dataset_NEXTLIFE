/* C++ */
#include <iostream>
#include <string>
#include <vector>
#include <array>

/* Me */
#include "coreFunctions.h"
#include "handleJSON.h"
#include "progress.h"

// --- MAIN --- //
int main(int argc, char *argv[])
{
    std::cout << "[C++] : Main Started!" << std::endl;

    // Args verification
    if (argc != 2)
    {
        std::cerr << "[USE] : " << argv[0] << " <params.json>" << std::endl;
        return 1;
    }
    else
    {
        // --- Init JSON paramters --- //
        Configuration cfg;
        readJsonFile(argv[1], cfg);
        cfg.printConfig();

        // --- Init Gnuplot --- //
        Gnuplot gp;
        initGnuplotParamAndStyle(gp);

        // --- Init Directories result --- //
        const std::string outputFolder = cfg.outputFolder + "/";
        std::filesystem::create_directories(outputFolder);

        // ------- ORIG PATH MESH ------- //
        std::vector<std::string> fullPaths = collect_mesh_paths(cfg.meshPath, /*Recursive=*/true);

        // ------- RUN PHASE CORES ------- //
        Progress prog;

        // // Quantization Only
        // prog.run_phase_core("QuantizationOnly", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcQuantizeOnly);

        // // Monotonic - Simple
        // prog.run_phase_core("Monotonic", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcDefAxis<TransformMonotonic>);

        // // Oscillator - Simple
        // prog.run_phase_core("Oscillator", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcDefAxis<TransformOscillator>);

        // // Mantissa - Simple
        prog.run_phase_core("Mantissa", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcEncMantissa);

        // // Lorenz3D - Simple
        // prog.run_phase_core("Lorenz3D", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcEncLorenz3D);

        // // Monotonic - Quantization
        // prog.run_phase_core("MonotonicQuantization", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcDefAxisQuantized<TransformMonotonic>);

        // // Oscillator - Quantization
        // prog.run_phase_core("OscillatorQuantization", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcDefAxisQuantized<TransformOscillator>);

        // // Mantissa - Quantization
        // prog.run_phase_core("MantissaQuantization", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcEncMantissaQuantized);

        // // Lorenz3D - Quantization
        // prog.run_phase_core("Lorenz3DQuantization", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcEncLorenz3DQuantized);

        // // Bianca / Li&Al. Hierarchical - Simple
        // prog.run_phase_core("Hierarchical", fullPaths, cfg.nbAttempts, outputFolder, cfg, gp, &funcEncHierarchical);
    }

    std::cout << "[C++] : Main Finished!" << std::endl;
    return 0;
}