#pragma once

/* C++ */
#include <string>
#include <vector>

/* Me */
#include "meshTools.h"
#include "csvGeneric.h"

void funcQuantizeOnly(const Mesh &meshOrig, const std::string &meshName, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, csv::WriterDynamic &csvWriter, size_t attempt = 0)
{
    // --------- QUANTIZE-DEQUANTIZE --------- //
    Mesh meshQuantized = meshOrig.clone();
    meshQuantized.quantizeDequantizeInPlace(cfg.qp);

    // --- CSV DATA --- //
    std::array<double, 4> autoCorrQuantized = meshQuantized.autoCorrelationLag1Index();
    std::array<double, 4> compCorrQuantized = meshOrig.pearsonCorrelation(meshQuantized);

    csvWriter.writeRow(
        {{"Name", meshName},
         {"QP", std::to_string(cfg.qp)},
         {"Attempt", std::to_string(attempt)},

         {"RMSE_Quantized", csv::format_float(meshOrig.rmse(meshQuantized))},
         {"HAUS_Quantized", csv::format_float(meshOrig.hausdorff(meshQuantized))},
         {"NPCR_Quantized", csv::format_float(meshOrig.npcr(meshQuantized))},
         {"UACI_Quantized", csv::format_float(meshOrig.uaci(meshQuantized))},
         {"Entropy_Quantized", csv::format_float(meshQuantized.entropy())},
         {"AutoCorrX_Quantized", csv::format_float(autoCorrQuantized[0])},
         {"AutoCorrY_Quantized", csv::format_float(autoCorrQuantized[1])},
         {"AutoCorrZ_Quantized", csv::format_float(autoCorrQuantized[2])},
         {"AutoCorrAVG_Quantized", csv::format_float(autoCorrQuantized[3])},
         {"CompCorrX_Quantized", csv::format_float(compCorrQuantized[0])},
         {"CompCorrY_Quantized", csv::format_float(compCorrQuantized[1])},
         {"CompCorrZ_Quantized", csv::format_float(compCorrQuantized[2])},
         {"CompCorrAVG_Quantized", csv::format_float(compCorrQuantized[3])}});

    // --------- PLT --------- //
    if (cfg.plotQP)
    {
        meshQuantized.plotHistPoints(gp, outputFolder + meshName + "_quantized" + std::to_string(cfg.qp) + "_histPoints");
        meshQuantized.plotHistEdges(gp, outputFolder + meshName + "_quantized" + std::to_string(cfg.qp) + "_histEdges");
        meshQuantized.plotPointCorrIndex(gp, outputFolder + meshName + "_quantized" + std::to_string(cfg.qp) + "_histCorrXYZ");
    }

    // --------- SAVE --------- //
    if (cfg.saveQP)
        meshQuantized.save(outputFolder + meshName + "_quantized_" + std::to_string(cfg.qp));
}

void funcEncMantissa(const Mesh &meshOrig, const std::string &meshName, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, csv::WriterDynamic &csvWriter, size_t attempt = 0)
{
    // --- Gen Keys and PRNG --- //
    CryptoPP::AutoSeededRandomPool rnd;
    CryptoPP::SecByteBlock key(cfg.nbBytesKey), iv(cfg.nbBytesIV);
    rnd.GenerateBlock(key, key.size());
    rnd.GenerateBlock(iv, iv.size());

    // --------- NORMALIZE --------- //
    Mesh meshOrigOrder = meshOrig.clone();
    const UnitXform xf_orig = meshOrigOrder.computeUnitXform(); // WARNING : XF IS CONSIDERED GLOBAL FOR ENCODER / DECODER !
    meshOrigOrder.applyUnitInPlace(xf_orig);

    // --------- ENC --------- //
    Mesh meshEnc = meshOrigOrder.clone();
    meshEnc.selectEncrAESCFB(cfg.nbBitsEnc, cfg.nbClearLSB, /*ENCRYPT=*/true, key, iv);

    // --------- DEC --------- //
    Mesh meshDec = meshEnc.clone();
    meshDec.selectEncrAESCFB(cfg.nbBitsEnc, cfg.nbClearLSB, /*ENCRYPT=*/false, key, iv);

    // // --- LAPLACIAN SMOOTHING --- //
    // Mesh meshEncSmoothed = meshEnc.clone();
    // const int iteration = 10;
    // meshEncSmoothed.taubinSmoothCotanInPlace(iteration);
    // meshEncSmoothed.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB) + "_LaplacianSmoothed");

    // --- CSV DATA --- //
    std::array<double, 4> autoCorrEnc = meshEnc.autoCorrelationLag1Index();
    std::array<double, 4> compCorrEnc = meshOrigOrder.pearsonCorrelation(meshEnc);
    std::array<double, 4> autoCorrDec = meshDec.autoCorrelationLag1Index();
    std::array<double, 4> compCorrDec = meshOrigOrder.pearsonCorrelation(meshDec);
    csvWriter.writeRow(
        {{"Name", meshName},
         {"Attempt", std::to_string(attempt)},
         {"Bit_Encr", std::to_string(cfg.nbBitsEnc)},
         {"Bit_OffsetLSB", std::to_string(cfg.nbClearLSB)},
         {"RMSE_Enc", csv::format_float(meshOrigOrder.rmse(meshEnc))},
         {"HAUS_Enc", csv::format_float(meshOrigOrder.hausdorff(meshEnc))},
         {"NPCR_Enc", csv::format_float(meshOrigOrder.npcr(meshEnc))},
         {"UACI_Enc", csv::format_float(meshOrigOrder.uaci(meshEnc))},
         {"Entropy_Enc", csv::format_float(meshEnc.entropy())},
         {"AutoCorrX_Enc", csv::format_float(autoCorrEnc[0])},
         {"AutoCorrY_Enc", csv::format_float(autoCorrEnc[1])},
         {"AutoCorrZ_Enc", csv::format_float(autoCorrEnc[2])},
         {"AutoCorrAVG_Enc", csv::format_float(autoCorrEnc[3])},
         {"CompCorrX_Enc", csv::format_float(compCorrEnc[0])},
         {"CompCorrY_Enc", csv::format_float(compCorrEnc[1])},
         {"CompCorrZ_Enc", csv::format_float(compCorrEnc[2])},
         {"CompCorrAVG_Enc", csv::format_float(compCorrEnc[3])},

         {"RMSE_Dec", csv::format_float(meshOrigOrder.rmse(meshDec))},
         {"HAUS_Dec", csv::format_float(meshOrigOrder.hausdorff(meshDec))},
         {"NPCR_Dec", csv::format_float(meshOrigOrder.npcr(meshDec))},
         {"UACI_Dec", csv::format_float(meshOrigOrder.uaci(meshDec))},
         {"Entropy_Dec", csv::format_float(meshDec.entropy())},
         {"AutoCorrX_Dec", csv::format_float(autoCorrDec[0])},
         {"AutoCorrY_Dec", csv::format_float(autoCorrDec[1])},
         {"AutoCorrZ_Dec", csv::format_float(autoCorrDec[2])},
         {"AutoCorrAVG_Dec", csv::format_float(autoCorrDec[3])},
         {"CompCorrX_Dec", csv::format_float(compCorrDec[0])},
         {"CompCorrY_Dec", csv::format_float(compCorrDec[1])},
         {"CompCorrZ_Dec", csv::format_float(compCorrDec[2])},
         {"CompCorrAVG_Dec", csv::format_float(compCorrDec[3])},

         //  {"RMSE_Lap", csv::format_float(meshOrigOrder.rmse(meshEncSmoothed))},
         //  {"HAUS_Lap", csv::format_float(meshOrigOrder.hausdorff(meshEncSmoothed))},

         {"Key", encodeHexStringSecByteBlock(key)},
         {"IV", encodeHexStringSecByteBlock(iv)}});

    // --------- PLT --------- //
    if (cfg.plotEnc)
    {
        meshEnc.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histPoints" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshEnc.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histEdges" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshEnc.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histCorrXYZ" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
    }
    if (cfg.plotDec)
    {
        meshDec.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_dec_histPoints" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshDec.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_dec_histEdges" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshDec.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_dec_histCorrXYZ" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
    }

    // --------- DENORMALIZATION --------- //
    meshOrigOrder.applyInverseUnitInPlace(xf_orig);
    meshEnc.applyInverseUnitInPlace(xf_orig);
    meshDec.applyInverseUnitInPlace(xf_orig);

    // --------- SAVE --------- //
    if (cfg.saveEnc)
        meshEnc.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
    if (cfg.saveDec)
        meshDec.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_dec_" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
}

template <typename TF>
void funcDefAxis(const Mesh &meshOrig, const std::string &meshName, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, csv::WriterDynamic &csvWriter, size_t attempt = 0)
{
    // --------- Gen Keys and PRNG --------- //
    CryptoPP::AutoSeededRandomPool rnd;
    CryptoPP::SecByteBlock key(cfg.nbBytesKey), iv(cfg.nbBytesIV);
    rnd.GenerateBlock(key, key.size());
    rnd.GenerateBlock(iv, iv.size());
    SimpleCryptoPRNG prngGeodesic(key), prngEncDec(key);

    // --------- REORDER SEQUENCE 3D EUCLIDEAN RADIUS (once) --------- //
    Mesh meshOrigOrder = meshOrig.clone();
    meshOrigOrder.reorderByEuclideanRadius(prngGeodesic);

    // --------- NORMALIZE --------- //
    const UnitXform xf_orig = meshOrigOrder.computeUnitXform(); // WARNING : XF IS CONSIDERED GLOBAL FOR ENCODER / DECODER !
    meshOrigOrder.applyUnitInPlace(xf_orig);

    // --------- ORIG REORDER STATS (BEFORE DENORM) --------- //
    if (cfg.plotOrig)
    {
        meshOrigOrder.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_orig_histPoints");
        meshOrigOrder.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_orig_histEdges");
        meshOrigOrder.plotPointCorrSeed(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_orig_seedDistCorrEuclidean", /*seedIdx=*/0); // reorderByEuclideanRadius -> Seed is 0.
    }

    // --------- PRNG  --------- //
    std::array<std::pair<float, float>, 3> scalesEncDec = randFloatLinUniform(prngEncDec, cfg.minRange, cfg.maxRange, cfg.idBand);
    TF T0_EncDec = TF::fromConfig(prngEncDec, cfg);
    TF T1_EncDec = TF::fromConfig(prngEncDec, cfg);
    TF T2_EncDec = TF::fromConfig(prngEncDec, cfg);

    // --------- ENCODER --------- //
    Mesh meshDef = meshOrigOrder.clone();
    meshDef.deform(scalesEncDec, T0_EncDec, T1_EncDec, T2_EncDec, /*DECODE=*/false);

    // ------- DECODER ------- //
    Mesh meshRec = meshDef.clone();
    meshRec.deform(scalesEncDec, T0_EncDec, T1_EncDec, T2_EncDec, /*DECODE=*/true);

    // // --- LAPLACIAN SMOOTHING --- //
    // Mesh meshDefSmoothed = meshDef.clone();
    // const int iteration = 10;
    // meshDefSmoothed.taubinSmoothCotanInPlace(iteration);
    // meshDefSmoothed.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_LaplacianSmoothed");

    // --------- CSV DATA --------- //
    std::array<double, 4> compCorrDef = meshOrigOrder.pearsonCorrelation(meshDef);
    std::array<double, 4> compCorrRec = meshOrigOrder.pearsonCorrelation(meshRec);

    std::vector<std::pair<std::string, std::string>> cfgPairs = TF::cfgParamPairs(cfg);
    auto xPairs = T0_EncDec.instanceParamPairs("TFX_");
    auto yPairs = T1_EncDec.instanceParamPairs("TFY_");
    auto zPairs = T2_EncDec.instanceParamPairs("TFZ_");

    std::vector<std::pair<std::string, std::string>> row = {
        {"Name", meshName},
        {"Attempt", std::to_string(attempt)},
        {"Min_Range", csv::format_float(cfg.minRange)},
        {"Max_Range", csv::format_float(cfg.maxRange)},
        {"Id_Band", csv::format_float(cfg.idBand)},

        {"RMSE_Def", csv::format_float(meshOrigOrder.rmse(meshDef))},
        {"HAUS_Def", csv::format_float(meshOrigOrder.hausdorff(meshDef))},
        {"NPCR_Def", csv::format_float(meshOrigOrder.npcr(meshDef))},
        {"UACI_Def", csv::format_float(meshOrigOrder.uaci(meshDef))},
        {"Entropy_Def", csv::format_float(meshDef.entropy())},
        {"AutoCorrSeed_Def", csv::format_float(meshDef.autoCorrelationLag1Seed(/*seedIdx=*/0))},
        {"CompCorrX_Def", csv::format_float(compCorrDef[0])},
        {"CompCorrY_Def", csv::format_float(compCorrDef[1])},
        {"CompCorrZ_Def", csv::format_float(compCorrDef[2])},
        {"CompCorrAVG_Def", csv::format_float(compCorrDef[3])},

        {"RMSE_Rec", csv::format_float(meshOrigOrder.rmse(meshRec))},
        {"HAUS_Rec", csv::format_float(meshOrigOrder.hausdorff(meshRec))},
        {"NPCR_Rec", csv::format_float(meshOrigOrder.npcr(meshRec))},
        {"UACI_Rec", csv::format_float(meshOrigOrder.uaci(meshRec))},
        {"Entropy_Rec", csv::format_float(meshRec.entropy())},
        {"AutoCorrSeed_Rec", csv::format_float(meshRec.autoCorrelationLag1Seed(/*seedIdx=*/0))},
        {"CompCorrX_Rec", csv::format_float(compCorrRec[0])},
        {"CompCorrY_Rec", csv::format_float(compCorrRec[1])},
        {"CompCorrZ_Rec", csv::format_float(compCorrRec[2])},
        {"CompCorrAVG_Rec", csv::format_float(compCorrRec[3])},

        // {"RMSE_Lap", csv::format_float(meshOrigOrder.rmse(meshDefSmoothed))},
        // {"HAUS_Lap", csv::format_float(meshOrigOrder.hausdorff(meshDefSmoothed))},

        {"ScaleX_start", csv::format_float(scalesEncDec[0].first)},
        {"ScaleX_end", csv::format_float(scalesEncDec[0].second)},
        {"ScaleY_start", csv::format_float(scalesEncDec[1].first)},
        {"ScaleY_end", csv::format_float(scalesEncDec[1].second)},
        {"ScaleZ_start", csv::format_float(scalesEncDec[2].first)},
        {"ScaleZ_end", csv::format_float(scalesEncDec[2].second)},
        {"TFName", TF::name()},
        {"Key", encodeHexStringSecByteBlock(key)},
        {"IV", encodeHexStringSecByteBlock(iv)}};

    row.insert(row.end(), cfgPairs.begin(), cfgPairs.end());
    row.insert(row.end(), xPairs.begin(), xPairs.end());
    row.insert(row.end(), yPairs.begin(), yPairs.end());
    row.insert(row.end(), zPairs.begin(), zPairs.end());
    csvWriter.writeRow(row);

    // --------- PLOTS (BEFORE DENORM) --------- //
    if (cfg.plotEnc)
    {
        meshDef.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_def_histPoints");
        meshDef.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_def_histEdges");
        meshDef.plotPointCorrSeed(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_def_seedDistCorrEuclidean", /*seedIdx=*/0);
    }
    if (cfg.plotDec)
    {
        meshRec.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_rec_histPoints");
        meshRec.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_rec_histEdges");
        meshRec.plotPointCorrSeed(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_rec_seedDistCorrEuclidean", /*seedIdx=*/0);
    }

    // --------- DENORMALIZATION --------- //
    meshOrigOrder.applyInverseUnitInPlace(xf_orig);
    meshDef.applyInverseUnitInPlace(xf_orig);
    meshRec.applyInverseUnitInPlace(xf_orig);

    // --------- SAVE --------- //
    if (cfg.saveEnc)
        meshDef.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_def");
    if (cfg.saveDec)
        meshRec.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_rec");
}

void funcEncMantissaQuantized(const Mesh &meshOrig, const std::string &meshName, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, csv::WriterDynamic &csvWriter, size_t attempt = 0)
{
    // --- Gen Keys and PRNG --- //
    CryptoPP::AutoSeededRandomPool rnd;
    CryptoPP::SecByteBlock key(cfg.nbBytesKey), iv(cfg.nbBytesIV);
    rnd.GenerateBlock(key, key.size());
    rnd.GenerateBlock(iv, iv.size());

    // --------- ENC --------- //
    Mesh meshEnc = meshOrig.clone();
    meshEnc.selectEncrAESCFB(cfg.nbBitsEnc, cfg.nbClearLSB, /*ENCRYPT=*/true, key, iv);

    // --------- QUANTIZE --------- //
    Mesh meshEncQP = meshEnc.clone();
    meshEncQP.quantizeDequantizeInPlace(cfg.qp);

    // --------- DEC --------- //
    Mesh meshDec = meshEncQP.clone();
    meshDec.selectEncrAESCFB(cfg.nbBitsEnc, cfg.nbClearLSB, /*ENCRYPT=*/false, key, iv);

    // --- CSV DATA --- //
    std::array<double, 4> autoCorrEnc = meshEnc.autoCorrelationLag1Index();
    std::array<double, 4> compCorrEnc = meshOrig.pearsonCorrelation(meshEnc);
    std::array<double, 4> autoCorrEnc2 = meshEncQP.autoCorrelationLag1Index();
    std::array<double, 4> compCorrEnc2 = meshOrig.pearsonCorrelation(meshEncQP);
    std::array<double, 4> autoCorrDec = meshDec.autoCorrelationLag1Index();
    std::array<double, 4> compCorrDec = meshOrig.pearsonCorrelation(meshDec);
    csvWriter.writeRow(
        {{"Name", meshName},
         {"Attempt", std::to_string(attempt)},
         {"Bit_Encr", std::to_string(cfg.nbBitsEnc)},
         {"Bit_OffsetLSB", std::to_string(cfg.nbClearLSB)},

         {"RMSE_Enc", csv::format_float(meshOrig.rmse(meshEnc))},
         {"HAUS_Enc", csv::format_float(meshOrig.hausdorff(meshEnc))},
         {"NPCR_Enc", csv::format_float(meshOrig.npcr(meshEnc))},
         {"UACI_Enc", csv::format_float(meshOrig.uaci(meshEnc))},
         {"Entropy_Enc", csv::format_float(meshEnc.entropy())},
         {"AutoCorrX_Enc", csv::format_float(autoCorrEnc[0])},
         {"AutoCorrY_Enc", csv::format_float(autoCorrEnc[1])},
         {"AutoCorrZ_Enc", csv::format_float(autoCorrEnc[2])},
         {"AutoCorrAVG_Enc", csv::format_float(autoCorrEnc[3])},
         {"CompCorrX_Enc", csv::format_float(compCorrEnc[0])},
         {"CompCorrY_Enc", csv::format_float(compCorrEnc[1])},
         {"CompCorrZ_Enc", csv::format_float(compCorrEnc[2])},
         {"CompCorrAVG_Enc", csv::format_float(compCorrEnc[3])},

         {"RMSE_EncQP", csv::format_float(meshOrig.rmse(meshEncQP))},
         {"HAUS_EncQP", csv::format_float(meshOrig.hausdorff(meshEncQP))},
         {"NPCR_EncQP", csv::format_float(meshOrig.npcr(meshEncQP))},
         {"UACI_EncQP", csv::format_float(meshOrig.uaci(meshEncQP))},
         {"Entropy_EncQP", csv::format_float(meshEncQP.entropy())},
         {"AutoCorrX_EncQP", csv::format_float(autoCorrEnc2[0])},
         {"AutoCorrY_EncQP", csv::format_float(autoCorrEnc2[1])},
         {"AutoCorrZ_EncQP", csv::format_float(autoCorrEnc2[2])},
         {"AutoCorrAVG_EncQP", csv::format_float(autoCorrEnc2[3])},
         {"CompCorrX_EncQP", csv::format_float(compCorrEnc2[0])},
         {"CompCorrY_EncQP", csv::format_float(compCorrEnc2[1])},
         {"CompCorrZ_EncQP", csv::format_float(compCorrEnc2[2])},
         {"CompCorrAVG_EncQP", csv::format_float(compCorrEnc2[3])},

         {"RMSE_Dec", csv::format_float(meshOrig.rmse(meshDec))},
         {"HAUS_Dec", csv::format_float(meshOrig.hausdorff(meshDec))},
         {"NPCR_Dec", csv::format_float(meshOrig.npcr(meshDec))},
         {"UACI_Dec", csv::format_float(meshOrig.uaci(meshDec))},
         {"Entropy_Dec", csv::format_float(meshDec.entropy())},
         {"AutoCorrX_Dec", csv::format_float(autoCorrDec[0])},
         {"AutoCorrY_Dec", csv::format_float(autoCorrDec[1])},
         {"AutoCorrZ_Dec", csv::format_float(autoCorrDec[2])},
         {"AutoCorrAVG_Dec", csv::format_float(autoCorrDec[3])},
         {"CompCorrX_Dec", csv::format_float(compCorrDec[0])},
         {"CompCorrY_Dec", csv::format_float(compCorrDec[1])},
         {"CompCorrZ_Dec", csv::format_float(compCorrDec[2])},
         {"CompCorrAVG_Dec", csv::format_float(compCorrDec[3])},

         {"Key", encodeHexStringSecByteBlock(key)},
         {"IV", encodeHexStringSecByteBlock(iv)}});

    // --------- PLT --------- //
    if (cfg.plotEnc)
    {
        meshEnc.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_enc_histPoints" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshEnc.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_enc_histEdges" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshEnc.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_enc_histCorrXYZ" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
    }
    if (cfg.plotQP)
    {
        meshEncQP.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_encQP_histPoints" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshEncQP.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_encQP_histEdges" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshEncQP.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_encQP_histCorrXYZ" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
    }
    if (cfg.plotDec)
    {
        meshDec.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_dec_histPoints" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshDec.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_dec_histEdges" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
        meshDec.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_dec_histCorrXYZ" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
    }

    // --------- SAVE --------- //
    if (cfg.saveEnc)
        meshEnc.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_enc_" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
    if (cfg.saveQP)
        meshEncQP.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_encQP_" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
    if (cfg.saveDec)
        meshDec.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_dec_" + std::to_string(cfg.nbBitsEnc) + "_" + std::to_string(cfg.nbClearLSB));
}

template <typename TF>
void funcDefAxisQuantized(const Mesh &meshOrig, const std::string &meshName, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, csv::WriterDynamic &csvWriter, size_t attempt = 0)
{
    // --------- Gen Keys and PRNG --------- //
    CryptoPP::AutoSeededRandomPool rnd;
    CryptoPP::SecByteBlock key(cfg.nbBytesKey), iv(cfg.nbBytesIV);
    rnd.GenerateBlock(key, key.size());
    rnd.GenerateBlock(iv, iv.size());
    SimpleCryptoPRNG prngGeodesic(key), prngEncDec(key);

    // --------- REORDER SEQUENCE 3D EUCLIDEAN RADIUS (once) --------- //
    Mesh meshOrigOrder = meshOrig.clone();
    meshOrigOrder.reorderByEuclideanRadius(prngGeodesic);

    // --------- NORMALIZE --------- //
    const UnitXform xf_orig = meshOrigOrder.computeUnitXform(); // WARNING : XF IS CONSIDERED GLOBAL FOR ENCODER / DECODER !
    meshOrigOrder.applyUnitInPlace(xf_orig);

    // --------- ORIG REORDER STATS (BEFORE DENORM) --------- //
    if (cfg.plotOrig)
    {
        meshOrigOrder.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_orig_histPoints");
        meshOrigOrder.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_orig_histEdges");
        meshOrigOrder.plotPointCorrSeed(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_orig_seedDistCorrEuclidean", /*seedIdx=*/0); // reorderByEuclideanRadius -> Seed is 0.
    }

    // --------- PRNG  --------- //
    std::array<std::pair<float, float>, 3> scalesEncDec = randFloatLinUniform(prngEncDec, cfg.minRange, cfg.maxRange, cfg.idBand);
    TF T0_EncDec = TF::fromConfig(prngEncDec, cfg);
    TF T1_EncDec = TF::fromConfig(prngEncDec, cfg);
    TF T2_EncDec = TF::fromConfig(prngEncDec, cfg);

    // --------- ENCODER --------- //
    Mesh meshDef = meshOrigOrder.clone();
    meshDef.deform(scalesEncDec, T0_EncDec, T1_EncDec, T2_EncDec, /*DECODE=*/false);

    // --------- QUANTIZE --------- //
    Mesh meshDefQP = meshDef.clone();
    meshDefQP.quantizeDequantizeInPlace(cfg.qp);

    // ------- DECODER ------- //
    Mesh meshRec = meshDefQP.clone();
    meshRec.deform(scalesEncDec, T0_EncDec, T1_EncDec, T2_EncDec, /*DECODE=*/true);

    // --------- CSV DATA --------- //
    std::array<double, 4> compCorrDef = meshOrigOrder.pearsonCorrelation(meshDef);
    std::array<double, 4> compCorrDefQP = meshOrigOrder.pearsonCorrelation(meshDefQP);
    std::array<double, 4> compCorrRec = meshOrigOrder.pearsonCorrelation(meshRec);

    std::vector<std::pair<std::string, std::string>> cfgPairs = TF::cfgParamPairs(cfg);
    auto xPairs = T0_EncDec.instanceParamPairs("TFX_");
    auto yPairs = T1_EncDec.instanceParamPairs("TFY_");
    auto zPairs = T2_EncDec.instanceParamPairs("TFZ_");

    std::vector<std::pair<std::string, std::string>> row = {
        {"Name", meshName},
        {"Attempt", std::to_string(attempt)},
        {"Min_Range", csv::format_float(cfg.minRange)},
        {"Max_Range", csv::format_float(cfg.maxRange)},
        {"Id_Band", csv::format_float(cfg.idBand)},

        {"RMSE_Def", csv::format_float(meshOrigOrder.rmse(meshDef))},
        {"HAUS_Def", csv::format_float(meshOrigOrder.hausdorff(meshDef))},
        {"NPCR_Def", csv::format_float(meshOrigOrder.npcr(meshDef))},
        {"UACI_Def", csv::format_float(meshOrigOrder.uaci(meshDef))},
        {"Entropy_Def", csv::format_float(meshDef.entropy())},
        {"AutoCorrSeed_Def", csv::format_float(meshDef.autoCorrelationLag1Seed(/*seedIdx=*/0))},
        {"CompCorrX_Def", csv::format_float(compCorrDef[0])},
        {"CompCorrY_Def", csv::format_float(compCorrDef[1])},
        {"CompCorrZ_Def", csv::format_float(compCorrDef[2])},
        {"CompCorrAVG_Def", csv::format_float(compCorrDef[3])},

        {"RMSE_DefQP", csv::format_float(meshOrigOrder.rmse(meshDefQP))},
        {"HAUS_DefQP", csv::format_float(meshOrigOrder.hausdorff(meshDefQP))},
        {"NPCR_DefQP", csv::format_float(meshOrigOrder.npcr(meshDefQP))},
        {"UACI_DefQP", csv::format_float(meshOrigOrder.uaci(meshDefQP))},
        {"Entropy_DefQP", csv::format_float(meshDefQP.entropy())},
        {"AutoCorrSeed_DefQP", csv::format_float(meshDefQP.autoCorrelationLag1Seed(/*seedIdx=*/0))},
        {"CompCorrX_DefQP", csv::format_float(compCorrDefQP[0])},
        {"CompCorrY_DefQP", csv::format_float(compCorrDefQP[1])},
        {"CompCorrZ_DefQP", csv::format_float(compCorrDefQP[2])},
        {"CompCorrAVG_DefQP", csv::format_float(compCorrDefQP[3])},

        {"RMSE_Rec", csv::format_float(meshOrigOrder.rmse(meshRec))},
        {"HAUS_Rec", csv::format_float(meshOrigOrder.hausdorff(meshRec))},
        {"NPCR_Rec", csv::format_float(meshOrigOrder.npcr(meshRec))},
        {"UACI_Rec", csv::format_float(meshOrigOrder.uaci(meshRec))},
        {"Entropy_Rec", csv::format_float(meshRec.entropy())},
        {"AutoCorrSeed_Rec", csv::format_float(meshRec.autoCorrelationLag1Seed(/*seedIdx=*/0))},
        {"CompCorrX_Rec", csv::format_float(compCorrRec[0])},
        {"CompCorrY_Rec", csv::format_float(compCorrRec[1])},
        {"CompCorrZ_Rec", csv::format_float(compCorrRec[2])},
        {"CompCorrAVG_Rec", csv::format_float(compCorrRec[3])},

        {"ScaleX_start", csv::format_float(scalesEncDec[0].first)},
        {"ScaleX_end", csv::format_float(scalesEncDec[0].second)},
        {"ScaleY_start", csv::format_float(scalesEncDec[1].first)},
        {"ScaleY_end", csv::format_float(scalesEncDec[1].second)},
        {"ScaleZ_start", csv::format_float(scalesEncDec[2].first)},
        {"ScaleZ_end", csv::format_float(scalesEncDec[2].second)},
        {"TFName", TF::name()},
        {"Key", encodeHexStringSecByteBlock(key)},
        {"IV", encodeHexStringSecByteBlock(iv)}};

    row.insert(row.end(), cfgPairs.begin(), cfgPairs.end());
    row.insert(row.end(), xPairs.begin(), xPairs.end());
    row.insert(row.end(), yPairs.begin(), yPairs.end());
    row.insert(row.end(), zPairs.begin(), zPairs.end());
    csvWriter.writeRow(row);

    // --------- PLOTS (BEFORE DENORM) --------- //
    if (cfg.plotEnc)
    {
        meshDef.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_def" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_histPoints");
        meshDef.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_def" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_histEdges");
        meshDef.plotPointCorrSeed(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_def" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_seedDistCorrEuclidean", /*seedIdx=*/0);
    }
    if (cfg.plotQP)
    {
        meshDefQP.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_defQP" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_histPoints");
        meshDefQP.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_defQP" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_histEdges");
        meshDefQP.plotPointCorrSeed(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_defQP" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_seedDistCorrEuclidean", /*seedIdx=*/0);
    }
    if (cfg.plotDec)
    {
        meshRec.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_rec" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_histPoints");
        meshRec.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_rec" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_histEdges");
        meshRec.plotPointCorrSeed(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_rec" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange) + "_seedDistCorrEuclidean", /*seedIdx=*/0);
    }

    // --------- DENORMALIZATION --------- //
    meshOrigOrder.applyInverseUnitInPlace(xf_orig);
    meshDef.applyInverseUnitInPlace(xf_orig);
    meshDefQP.applyInverseUnitInPlace(xf_orig);
    meshRec.applyInverseUnitInPlace(xf_orig);

    // --------- SAVE --------- //
    if (cfg.saveEnc)
        meshDef.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_def" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange));
    if (cfg.saveQP)
        meshDefQP.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_defQP" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange));
    if (cfg.saveDec)
        meshRec.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_rec" + std::to_string(cfg.minRange) + "_" + std::to_string(cfg.maxRange));
}

void funcEncLorenz3D(const Mesh &meshOrig, const std::string &meshName, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, csv::WriterDynamic &csvWriter, size_t attempt = 0)
{
    // --- Gen Keys and PRNG --- //
    CryptoPP::AutoSeededRandomPool rnd;
    CryptoPP::SecByteBlock key(cfg.nbBytesKey), iv(cfg.nbBytesIV);
    rnd.GenerateBlock(key, key.size());
    rnd.GenerateBlock(iv, iv.size());

    // --------- ENC --------- //
    Mesh meshEnc = meshOrig.clone();
    meshEnc.fullEncrLorenz3D(/*ENCRYPT=*/true, key, iv);

    // --------- DEC --------- //
    Mesh meshDec = meshEnc.clone();
    meshDec.fullEncrLorenz3D(/*ENCRYPT=*/false, key, iv);

    // --- CSV DATA --- //
    std::array<double, 4> autoCorrEnc = meshEnc.autoCorrelationLag1Index();
    std::array<double, 4> compCorrEnc = meshOrig.pearsonCorrelation(meshEnc);
    std::array<double, 4> autoCorrDec = meshDec.autoCorrelationLag1Index();
    std::array<double, 4> compCorrDec = meshOrig.pearsonCorrelation(meshDec);
    csvWriter.writeRow(
        {{"Name", meshName},
         {"Attempt", std::to_string(attempt)},
         {"RMSE_Enc", csv::format_float(meshOrig.rmse(meshEnc))},
         {"HAUS_Enc", csv::format_float(meshOrig.hausdorff(meshEnc))},
         {"NPCR_Enc", csv::format_float(meshOrig.npcr(meshEnc))},
         {"UACI_Enc", csv::format_float(meshOrig.uaci(meshEnc))},
         {"Entropy_Enc", csv::format_float(meshEnc.entropy())},
         {"AutoCorrX_Enc", csv::format_float(autoCorrEnc[0])},
         {"AutoCorrY_Enc", csv::format_float(autoCorrEnc[1])},
         {"AutoCorrZ_Enc", csv::format_float(autoCorrEnc[2])},
         {"AutoCorrAVG_Enc", csv::format_float(autoCorrEnc[3])},
         {"CompCorrX_Enc", csv::format_float(compCorrEnc[0])},
         {"CompCorrY_Enc", csv::format_float(compCorrEnc[1])},
         {"CompCorrZ_Enc", csv::format_float(compCorrEnc[2])},
         {"CompCorrAVG_Enc", csv::format_float(compCorrEnc[3])},

         {"RMSE_Dec", csv::format_float(meshOrig.rmse(meshDec))},
         {"HAUS_Dec", csv::format_float(meshOrig.hausdorff(meshDec))},
         {"NPCR_Dec", csv::format_float(meshOrig.npcr(meshDec))},
         {"UACI_Dec", csv::format_float(meshOrig.uaci(meshDec))},
         {"Entropy_Dec", csv::format_float(meshDec.entropy())},
         {"AutoCorrX_Dec", csv::format_float(autoCorrDec[0])},
         {"AutoCorrY_Dec", csv::format_float(autoCorrDec[1])},
         {"AutoCorrZ_Dec", csv::format_float(autoCorrDec[2])},
         {"AutoCorrAVG_Dec", csv::format_float(autoCorrDec[3])},
         {"CompCorrX_Dec", csv::format_float(compCorrDec[0])},
         {"CompCorrY_Dec", csv::format_float(compCorrDec[1])},
         {"CompCorrZ_Dec", csv::format_float(compCorrDec[2])},
         {"CompCorrAVG_Dec", csv::format_float(compCorrDec[3])},

         {"Key", encodeHexStringSecByteBlock(key)},
         {"IV", encodeHexStringSecByteBlock(iv)}});

    // --------- PLT --------- //
    if (cfg.plotEnc)
    {
        meshEnc.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histPoints");
        meshEnc.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histEdges");
        meshEnc.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histCorrXYZ");
    }
    if (cfg.plotDec)
    {
        meshDec.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_dec_histPoints");
        meshDec.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_dec_histEdges");
        meshDec.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_dec_histCorrXYZ");
    }

    // --------- SAVE --------- //
    if (cfg.saveEnc)
        meshEnc.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc");
    if (cfg.saveDec)
        meshDec.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_dec");
}

void funcEncLorenz3DQuantized(const Mesh &meshOrig, const std::string &meshName, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, csv::WriterDynamic &csvWriter, size_t attempt = 0)
{
    // --- Gen Keys and PRNG --- //
    CryptoPP::AutoSeededRandomPool rnd;
    CryptoPP::SecByteBlock key(cfg.nbBytesKey), iv(cfg.nbBytesIV);
    rnd.GenerateBlock(key, key.size());
    rnd.GenerateBlock(iv, iv.size());

    // --------- ENC --------- //
    Mesh meshEnc = meshOrig.clone();
    meshEnc.fullEncrLorenz3D(/*ENCRYPT=*/true, key, iv);

    // --------- QUANTIZE --------- //
    Mesh meshEncQP = meshEnc.clone();
    meshEncQP.quantizeDequantizeInPlace(cfg.qp);

    // --------- DEC --------- //
    Mesh meshDec = meshEncQP.clone();
    meshDec.fullEncrLorenz3D(/*ENCRYPT=*/false, key, iv);

    // --- CSV DATA --- //
    std::array<double, 4> autoCorrEnc = meshEnc.autoCorrelationLag1Index();
    std::array<double, 4> compCorrEnc = meshOrig.pearsonCorrelation(meshEnc);
    std::array<double, 4> autoCorrEnc2 = meshEncQP.autoCorrelationLag1Index();
    std::array<double, 4> compCorrEnc2 = meshOrig.pearsonCorrelation(meshEncQP);
    std::array<double, 4> autoCorrDec = meshDec.autoCorrelationLag1Index();
    std::array<double, 4> compCorrDec = meshOrig.pearsonCorrelation(meshDec);
    csvWriter.writeRow(
        {{"Name", meshName},
         {"Attempt", std::to_string(attempt)},

         {"RMSE_Enc", csv::format_float(meshOrig.rmse(meshEnc))},
         {"HAUS_Enc", csv::format_float(meshOrig.hausdorff(meshEnc))},
         {"NPCR_Enc", csv::format_float(meshOrig.npcr(meshEnc))},
         {"UACI_Enc", csv::format_float(meshOrig.uaci(meshEnc))},
         {"Entropy_Enc", csv::format_float(meshEnc.entropy())},
         {"AutoCorrX_Enc", csv::format_float(autoCorrEnc[0])},
         {"AutoCorrY_Enc", csv::format_float(autoCorrEnc[1])},
         {"AutoCorrZ_Enc", csv::format_float(autoCorrEnc[2])},
         {"AutoCorrAVG_Enc", csv::format_float(autoCorrEnc[3])},
         {"CompCorrX_Enc", csv::format_float(compCorrEnc[0])},
         {"CompCorrY_Enc", csv::format_float(compCorrEnc[1])},
         {"CompCorrZ_Enc", csv::format_float(compCorrEnc[2])},
         {"CompCorrAVG_Enc", csv::format_float(compCorrEnc[3])},

         {"RMSE_EncQP", csv::format_float(meshOrig.rmse(meshEncQP))},
         {"HAUS_EncQP", csv::format_float(meshOrig.hausdorff(meshEncQP))},
         {"NPCR_EncQP", csv::format_float(meshOrig.npcr(meshEncQP))},
         {"UACI_EncQP", csv::format_float(meshOrig.uaci(meshEncQP))},
         {"Entropy_EncQP", csv::format_float(meshEncQP.entropy())},
         {"AutoCorrX_EncQP", csv::format_float(autoCorrEnc2[0])},
         {"AutoCorrY_EncQP", csv::format_float(autoCorrEnc2[1])},
         {"AutoCorrZ_EncQP", csv::format_float(autoCorrEnc2[2])},
         {"AutoCorrAVG_EncQP", csv::format_float(autoCorrEnc2[3])},
         {"CompCorrX_EncQP", csv::format_float(compCorrEnc2[0])},
         {"CompCorrY_EncQP", csv::format_float(compCorrEnc2[1])},
         {"CompCorrZ_EncQP", csv::format_float(compCorrEnc2[2])},
         {"CompCorrAVG_EncQP", csv::format_float(compCorrEnc2[3])},

         {"RMSE_Dec", csv::format_float(meshOrig.rmse(meshDec))},
         {"HAUS_Dec", csv::format_float(meshOrig.hausdorff(meshDec))},
         {"NPCR_Dec", csv::format_float(meshOrig.npcr(meshDec))},
         {"UACI_Dec", csv::format_float(meshOrig.uaci(meshDec))},
         {"Entropy_Dec", csv::format_float(meshDec.entropy())},
         {"AutoCorrX_Dec", csv::format_float(autoCorrDec[0])},
         {"AutoCorrY_Dec", csv::format_float(autoCorrDec[1])},
         {"AutoCorrZ_Dec", csv::format_float(autoCorrDec[2])},
         {"AutoCorrAVG_Dec", csv::format_float(autoCorrDec[3])},
         {"CompCorrX_Dec", csv::format_float(compCorrDec[0])},
         {"CompCorrY_Dec", csv::format_float(compCorrDec[1])},
         {"CompCorrZ_Dec", csv::format_float(compCorrDec[2])},
         {"CompCorrAVG_Dec", csv::format_float(compCorrDec[3])},

         {"Key", encodeHexStringSecByteBlock(key)},
         {"IV", encodeHexStringSecByteBlock(iv)}});

    // --------- PLT --------- //
    if (cfg.plotEnc)
    {
        meshEnc.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_enc_histPoints");
        meshEnc.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_enc_histEdges");
        meshEnc.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_enc_histCorrXYZ");
    }
    if (cfg.plotQP)
    {
        meshEncQP.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_encQP_histPoints");
        meshEncQP.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_encQP_histEdges");
        meshEncQP.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_encQP_histCorrXYZ");
    }
    if (cfg.plotDec)
    {
        meshDec.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_dec_histPoints");
        meshDec.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_dec_histEdges");
        meshDec.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_dec_histCorrXYZ");
    }

    // --------- SAVE --------- //
    if (cfg.saveEnc)
        meshEnc.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_enc");
    if (cfg.saveQP)
        meshEncQP.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_encQP");
    if (cfg.saveDec)
        meshDec.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_qp" + std::to_string(cfg.qp) + "_dec");
}

void funcEncHierarchical(const Mesh &meshOrig, const std::string &meshName, const std::string &outputFolder, const Configuration &cfg, Gnuplot &gp, csv::WriterDynamic &csvWriter, size_t attempt = 0)
{
    // ---------------------------------------------------------------------
    // 2. Choose P/Q/R layout on the MSB side of each float
    //    float is 23 bits mantissa -> B = 23
    // ---------------------------------------------------------------------

    const size_t p = 2;  // could be parameters
    const size_t q = 2;  // could be parameters
    const size_t r = 10; // could be parameters
    if (p + q + r > 23)
    {
        std::cerr << "ERROR: p+q+r > bitwidth of float.\n";
        std::exit(EXIT_FAILURE);
    }
    const size_t N = meshOrig.getSizeVertices();
    std::vector<uint8_t> streamP(((N * p) + 7) / 8, 0u);
    std::vector<uint8_t> streamQ(((N * q) + 7) / 8, 0u);
    std::vector<uint8_t> streamR(((N * r) + 7) / 8, 0u);

    // ---------------------------------------------------------------------
    // 3. Extract P/Q/R bitfields from vertices into streams
    // ---------------------------------------------------------------------

    Mesh meshEnc = meshOrig.clone();
    meshEnc.processBits(streamP.data(), streamQ.data(), streamR.data(), p, q, r, true, true, true, true);

    // ---------------------------------------------------------------------
    // 4. Generate keys and IVs from the streams
    //
    // IMPORTANT: keyGen_PQR must return in this order:
    //   result[0] = K_r, result[1] = IV_r,
    //   result[2] = K_q, result[3] = IV_q,
    //   result[4] = K_p, result[5] = IV_p.
    //
    // If your implementation returns them in a different order, adjust the
    // indices below accordingly.
    // ---------------------------------------------------------------------

    std::array<CryptoPP::SecByteBlock, 6> keyIV = keyGen_PQR(streamP, streamQ, streamR);
    CryptoPP::SecByteBlock &K_r = keyIV[0];
    CryptoPP::SecByteBlock &IV_r = keyIV[1];
    CryptoPP::SecByteBlock &K_q = keyIV[2];
    CryptoPP::SecByteBlock &IV_q = keyIV[3];
    CryptoPP::SecByteBlock &K_p = keyIV[4];
    CryptoPP::SecByteBlock &IV_p = keyIV[5];

    // ---------------------------------------------------------------------
    // 5. ENCRYPT the P/Q/R streams in-place (CFB-like, no padding)
    //    Mapping (by construction):
    //      R-stream uses (K_r, IV_r)
    //      Q-stream uses (K_q, IV_q)
    //      P-stream uses (K_p, IV_p)
    // ---------------------------------------------------------------------

    encryptAESCFB_128b(streamR.data(), streamR.size(), K_r, IV_r, true);
    encryptAESCFB_128b(streamQ.data(), streamQ.size(), K_q, IV_q, true);
    encryptAESCFB_128b(streamP.data(), streamP.size(), K_p, IV_p, true);

    // ---------------------------------------------------------------------
    // 6. Insert the encrypted P/Q/R bits back into the vertices
    // ---------------------------------------------------------------------

    meshEnc.processBits(streamP.data(), streamQ.data(), streamR.data(), p, q, r, false, true, true, true);

    // ---------------------------------------------------------------------
    // 7. DECRYPT: extract P/Q/R from encrypted vertices, decrypt streams,
    //             then reinsert and compare to original.
    // ---------------------------------------------------------------------

    Mesh meshDecMid = meshEnc.clone();
    Mesh meshDecLow = meshEnc.clone();
    Mesh meshDecClear = meshEnc.clone();

    // Rebuild fresh streams for extraction
    std::vector<uint8_t> streamP_dec(((N * p) + 7) / 8, 0u);
    std::vector<uint8_t> streamQ_dec(((N * q) + 7) / 8, 0u);
    std::vector<uint8_t> streamR_dec(((N * r) + 7) / 8, 0u);

    // Extract from encrypted data
    meshDecClear.processBits(streamP_dec.data(), streamQ_dec.data(), streamR_dec.data(), p, q, r, true, true, true, true);

    // Decrypt streams using the SAME keys+IVs
    encryptAESCFB_128b(streamR_dec.data(), streamR_dec.size(), K_r, IV_r, false);
    encryptAESCFB_128b(streamQ_dec.data(), streamQ_dec.size(), K_q, IV_q, false);
    encryptAESCFB_128b(streamP_dec.data(), streamP_dec.size(), K_p, IV_p, false);

    // Reinsert decrypted bits
    meshDecMid.processBits(streamP_dec.data(), streamQ_dec.data(), streamR_dec.data(), p, q, r, false, true, false, false);
    meshDecLow.processBits(streamP_dec.data(), streamQ_dec.data(), streamR_dec.data(), p, q, r, false, true, true, false);
    meshDecClear.processBits(streamP_dec.data(), streamQ_dec.data(), streamR_dec.data(), p, q, r, false, true, true, true);

    // ---------------------------------------------------------------------
    // 8. STATS & SAVE
    // ---------------------------------------------------------------------

    // --- CSV DATA --- //
    std::array<double, 4> autoCorrEnc = meshEnc.autoCorrelationLag1Index();
    std::array<double, 4> compCorrEnc = meshOrig.pearsonCorrelation(meshEnc);
    std::array<double, 4> autoCorrDecMid = meshDecMid.autoCorrelationLag1Index();
    std::array<double, 4> compCorrDecMid = meshOrig.pearsonCorrelation(meshDecMid);
    std::array<double, 4> autoCorrDecLow = meshDecLow.autoCorrelationLag1Index();
    std::array<double, 4> compCorrDecLow = meshOrig.pearsonCorrelation(meshDecLow);
    std::array<double, 4> autoCorrDecClear = meshDecClear.autoCorrelationLag1Index();
    std::array<double, 4> compCorrDecClear = meshOrig.pearsonCorrelation(meshDecClear);

    csvWriter.writeRow(
        {{"Name", meshName},
         {"p", std::to_string(p)},
         {"q", std::to_string(q)},
         {"r", std::to_string(r)},
         {"Attempt", std::to_string(attempt)},
         {"RMSE_Enc", csv::format_float(meshOrig.rmse(meshEnc))},
         {"HAUS_Enc", csv::format_float(meshOrig.hausdorff(meshEnc))},
         {"NPCR_Enc", csv::format_float(meshOrig.npcr(meshEnc))},
         {"UACI_Enc", csv::format_float(meshOrig.uaci(meshEnc))},
         {"Entropy_Enc", csv::format_float(meshEnc.entropy())},
         {"AutoCorrX_Enc", csv::format_float(autoCorrEnc[0])},
         {"AutoCorrY_Enc", csv::format_float(autoCorrEnc[1])},
         {"AutoCorrZ_Enc", csv::format_float(autoCorrEnc[2])},
         {"AutoCorrAVG_Enc", csv::format_float(autoCorrEnc[3])},
         {"CompCorrX_Enc", csv::format_float(compCorrEnc[0])},
         {"CompCorrY_Enc", csv::format_float(compCorrEnc[1])},
         {"CompCorrZ_Enc", csv::format_float(compCorrEnc[2])},
         {"CompCorrAVG_Enc", csv::format_float(compCorrEnc[3])},

         {"RMSE_DecMid", csv::format_float(meshOrig.rmse(meshDecMid))},
         {"HAUS_DecMid", csv::format_float(meshOrig.hausdorff(meshDecMid))},
         {"NPCR_DecMid", csv::format_float(meshOrig.npcr(meshDecMid))},
         {"UACI_DecMid", csv::format_float(meshOrig.uaci(meshDecMid))},
         {"Entropy_DecMid", csv::format_float(meshDecMid.entropy())},
         {"AutoCorrX_DecMid", csv::format_float(autoCorrDecMid[0])},
         {"AutoCorrY_DecMid", csv::format_float(autoCorrDecMid[1])},
         {"AutoCorrZ_DecMid", csv::format_float(autoCorrDecMid[2])},
         {"AutoCorrAVG_DecMid", csv::format_float(autoCorrDecMid[3])},
         {"CompCorrX_DecMid", csv::format_float(compCorrDecMid[0])},
         {"CompCorrY_DecMid", csv::format_float(compCorrDecMid[1])},
         {"CompCorrZ_DecMid", csv::format_float(compCorrDecMid[2])},
         {"CompCorrAVG_DecMid", csv::format_float(compCorrDecMid[3])},

         {"RMSE_DecLow", csv::format_float(meshOrig.rmse(meshDecLow))},
         {"HAUS_DecLow", csv::format_float(meshOrig.hausdorff(meshDecLow))},
         {"NPCR_DecLow", csv::format_float(meshOrig.npcr(meshDecLow))},
         {"UACI_DecLow", csv::format_float(meshOrig.uaci(meshDecLow))},
         {"Entropy_DecLow", csv::format_float(meshDecLow.entropy())},
         {"AutoCorrX_DecLow", csv::format_float(autoCorrDecLow[0])},
         {"AutoCorrY_DecLow", csv::format_float(autoCorrDecLow[1])},
         {"AutoCorrZ_DecLow", csv::format_float(autoCorrDecLow[2])},
         {"AutoCorrAVG_DecLow", csv::format_float(autoCorrDecLow[3])},
         {"CompCorrX_DecLow", csv::format_float(compCorrDecLow[0])},
         {"CompCorrY_DecLow", csv::format_float(compCorrDecLow[1])},
         {"CompCorrZ_DecLow", csv::format_float(compCorrDecLow[2])},
         {"CompCorrAVG_DecLow", csv::format_float(compCorrDecLow[3])},

         {"RMSE_DecClear", csv::format_float(meshOrig.rmse(meshDecClear))},
         {"HAUS_DecClear", csv::format_float(meshOrig.hausdorff(meshDecClear))},
         {"NPCR_DecClear", csv::format_float(meshOrig.npcr(meshDecClear))},
         {"UACI_DecClear", csv::format_float(meshOrig.uaci(meshDecClear))},
         {"Entropy_DecClear", csv::format_float(meshDecClear.entropy())},
         {"AutoCorrX_DecClear", csv::format_float(autoCorrDecClear[0])},
         {"AutoCorrY_DecClear", csv::format_float(autoCorrDecClear[1])},
         {"AutoCorrZ_DecClear", csv::format_float(autoCorrDecClear[2])},
         {"AutoCorrAVG_DecClear", csv::format_float(autoCorrDecClear[3])},
         {"CompCorrX_DecClear", csv::format_float(compCorrDecClear[0])},
         {"CompCorrY_DecClear", csv::format_float(compCorrDecClear[1])},
         {"CompCorrZ_DecClear", csv::format_float(compCorrDecClear[2])},
         {"CompCorrAVG_DecClear", csv::format_float(compCorrDecClear[3])},

         {"K_r", encodeHexStringSecByteBlock(K_r)},
         {"IV_r", encodeHexStringSecByteBlock(IV_r)},
         {"K_q", encodeHexStringSecByteBlock(K_q)},
         {"IV_q", encodeHexStringSecByteBlock(IV_q)},
         {"K_p", encodeHexStringSecByteBlock(K_p)},
         {"IV_p", encodeHexStringSecByteBlock(IV_p)}});

    // --------- PLT --------- //
    if (cfg.plotEnc)
    {
        meshEnc.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histPoints");
        meshEnc.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histEdges");
        meshEnc.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc_histCorrXYZ");
    }
    if (cfg.plotDec)
    {
        meshDecMid.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decMid_histPoints");
        meshDecLow.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decLow_histPoints");
        meshDecClear.plotHistPoints(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decClear_histPoints");

        meshDecMid.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decMid_histEdges");
        meshDecLow.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decLow_histEdges");
        meshDecClear.plotHistEdges(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decClear_histEdges");

        meshDecMid.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decMid_histCorrXYZ");
        meshDecLow.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decLow_histCorrXYZ");
        meshDecClear.plotPointCorrIndex(gp, outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decClear_histCorrXYZ");
    }

    // --------- SAVE --------- //
    if (cfg.saveEnc)
        meshEnc.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_enc");
    if (cfg.saveDec)
    {
        meshDecMid.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decMid");
        meshDecLow.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decLow");
        meshDecClear.save(outputFolder + meshName + "_Attempt" + std::to_string(attempt) + "_decClear");
    }
}
