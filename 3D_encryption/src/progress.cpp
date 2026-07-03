// progress.cpp

#include "progress.h"

/* C++ */
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

Progress::Progress() : start_(clock::now()), lastPrint_(start_), lastStepEnd_(start_)
{
    std::cout << "\n"; // Print an initial blank so the first update can overwrite it
}

void Progress::step()
{
    auto now = clock::now();
    ++completedSteps_;
    ++curMeshDone_;

    if (hasLastStep_)
    {
        double dt = seconds(now - lastStepEnd_).count();
        update_ema(dt);
    }
    else
    {
        hasLastStep_ = true;
    }
    lastStepEnd_ = now;

    if (should_print(now))
    {
        print_line();
        lastPrint_ = now;
    }
}

void Progress::finish()
{
    clear_line();
    double elapsed = seconds(clock::now() - start_).count();
    std::ostringstream oss;
    oss << "[DONE] " << completedSteps_ << "/" << knownTotalSteps_
        << " steps in " << format_hms(elapsed);
    std::cout << oss.str() << std::endl;
}

bool Progress::should_print(clock::time_point now) const
{
    if (completedSteps_ == 0)
        return true; // show first tick promptly
    double since = seconds(now - lastPrint_).count();
    double period = dynamic_period();
    return since >= period;
}

double Progress::dynamic_period() const
{
    // If EMA unknown, use a reasonable default
    double ema = (emaStepSecCount_ > 0) ? emaStepSec_ : 0.5;
    double p = 0.2 * ema; // print ~every 20% of a step
    if (p < 0.1)
        p = 0.1;
    if (p > 1.0)
        p = 1.0;
    return p;
}

void Progress::update_ema(double observedStepSec)
{
    // Reactive early, steadier later
    constexpr double alphaWarm = 0.35;
    constexpr double alphaSteady = 0.15;
    double alpha = (emaStepSecCount_ < 10) ? alphaWarm : alphaSteady;

    if (emaStepSecCount_ == 0)
        emaStepSec_ = observedStepSec;
    else
        emaStepSec_ = alpha * observedStepSec + (1.0 - alpha) * emaStepSec_;
    ++emaStepSecCount_;
}

void Progress::print_line()
{
    auto now = clock::now();
    double elapsed = seconds(now - start_).count();

    double avgStep = (emaStepSecCount_ > 0) ? emaStepSec_ : (completedSteps_ > 0 ? (elapsed / double(completedSteps_)) : 0.5);

    std::size_t remaining = (knownTotalSteps_ > completedSteps_) ? (knownTotalSteps_ - completedSteps_) : 0;

    double etaSec = avgStep * double(remaining);
    double pct = (knownTotalSteps_ > 0) ? std::min(100.0, 100.0 * double(completedSteps_) / double(knownTotalSteps_)) : 100.0;

    std::size_t meshLeft = (curMeshIndex0_ + 1 <= totalMeshes_) ? (totalMeshes_ - (curMeshIndex0_ + 1)) : 0;
    std::size_t leftInMesh = (curMeshDone_ <= curMeshSteps_) ? (curMeshSteps_ - curMeshDone_) : 0;

    std::ostringstream oss;
    oss << "[PROGRESS] "
        << (curMeshIndex0_ + 1) << "/" << (totalMeshes_ ? totalMeshes_ : 1) << " "
        << "(" << curMeshName_ << ") "
        << "Iter " << curMeshDone_ << "/" << (curMeshSteps_ ? curMeshSteps_ : 1)
        << "  |  " << completedSteps_ << "/" << knownTotalSteps_
        << " (" << std::fixed << std::setprecision(1) << pct << "%)"
        << "  |  left: M=" << meshLeft
        << ", I_cur=" << leftInMesh
        << ", I_tot=" << remaining
        << "  |  ETA≈ " << format_hms(etaSec)
        << "  |  avg≈ " << std::fixed << std::setprecision(2) << avgStep << "s";

    clear_line();
    std::cout << oss.str() << std::flush;
}

void Progress::clear_line() const
{
    std::cout << "\33[2K\r"; // ANSI clear whole line + CR
}

std::string Progress::format_hms(double secondsTotal)
{
    if (secondsTotal < 0)
        secondsTotal = 0;
    int s = int(secondsTotal + 0.5);
    int h = s / 3600;
    s %= 3600;
    int m = s / 60;
    s %= 60;
    std::ostringstream oss;
    if (h > 0)
        oss << h << "h ";
    if (h > 0 || m > 0)
        oss << m << "m ";
    oss << s << "s";
    return oss.str();
}

std::string Progress::stem_of(const std::string &pathStr)
{
    std::filesystem::path p(pathStr);
    return p.stem().string();
}
