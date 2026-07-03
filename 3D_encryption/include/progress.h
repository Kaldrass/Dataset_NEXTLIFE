#pragma once

/* C++ */
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/* Me */
#include "meshTools.h"
#include "csvGeneric.h"

class Progress
{
public:
    using clock = std::chrono::steady_clock;
    using seconds = std::chrono::duration<double>;

    Progress();

    void step();
    void finish();

    template <typename CoreFn>
    void run_phase_core(const std::string &phaseName, const std::vector<std::string> &meshPaths, std::size_t attemptsPerMesh, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, CoreFn core)
    {
        // --- Init Progress --- //
        this->totalMeshes_ = meshPaths.size();
        this->phaseName_ = phaseName;

        // total steps are known up-front: meshes × attempts each
        this->knownTotalSteps_ = this->totalMeshes_ * attemptsPerMesh;
        this->completedSteps_ = 0;

        // reset clocks for this phase
        this->start_ = clock::now();
        this->lastPrint_ = start_;
        this->lastStepEnd_ = start_;
        this->hasLastStep_ = false;

        // reset EMA
        this->emaStepSec_ = 0.0;
        this->emaStepSecCount_ = 0;

        clear_line();
        std::cout << "[PHASE] " << phaseName_ << std::endl;

        // --- CSV Init --- //
        const std::string filename = outputFolder + "/results_stats_" + phaseName_ + ".csv";
        std::ofstream out(filename, std::ios::binary);
        if (!out)
            throw std::runtime_error("Cannot open " + filename);
        csv::WriterDynamic csvWriter(out, {});

        // --- LOOP FOR EACH MESH --- //
        for (std::size_t i = 0; i < meshPaths.size(); ++i)
        {
            const std::string &meshPath = meshPaths[i];
            const std::string meshName = stem_of(meshPath);

            // Build original mesh ONCE and CONST
            const Mesh meshOrig(meshPath);
            this->curMeshName_ = meshName;
            this->curMeshIndex0_ = i;
            this->curMeshSteps_ = attemptsPerMesh;
            this->curMeshDone_ = 0;
            this->lastStepEnd_ = clock::now();
            print_line();

            // --------- ORIG STATS --------- //
            if (cfg.saveOrig)
                meshOrig.save(outputFolder + meshName + "_orig");

            if (cfg.plotOrig)
            {
                meshOrig.plotHistPoints(gp, outputFolder + meshName + "_orig_histPoints");
                meshOrig.plotHistEdges(gp, outputFolder + meshName + "_orig_histEdges");
                meshOrig.plotPointCorrIndex(gp, outputFolder + meshName + "_orig_histCorrXYZ");
            }

            // --- LOOP FOR EACH ATTEMPT --- //
            for (std::size_t k = 0; k < attemptsPerMesh; ++k)
            {
                // One unit of work (success-only tick AFTER core returns)
                core(meshOrig, meshName, outputFolder, cfg, gp, csvWriter, k);
                step();
            }

            // --- CSV OUT AT EACH CORE MESH CALL (SAVE) --- //
            out.flush();
        }

        // --- PROGRESS FINISH --- //
        finish();
    }

private:
    bool should_print(clock::time_point now) const;
    double dynamic_period() const;
    void update_ema(double observedStepSec);
    void print_line();
    void clear_line() const;
    static std::string format_hms(double secondsTotal);

    static std::string stem_of(const std::string &pathStr);

private:
    clock::time_point start_;
    clock::time_point lastPrint_;
    clock::time_point lastStepEnd_;
    bool hasLastStep_ = false;

    std::size_t totalMeshes_ = 0;
    std::size_t knownTotalSteps_ = 0;
    std::size_t completedSteps_ = 0;

    std::string curMeshName_;
    std::size_t curMeshIndex0_ = 0;
    std::size_t curMeshSteps_ = 0;
    std::size_t curMeshDone_ = 0;

    double emaStepSec_ = 0.0;
    std::size_t emaStepSecCount_ = 0;

    std::string phaseName_;
};
