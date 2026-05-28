#pragma once
#include <vector>
#include <string>
#include <memory>
#ifdef CLOSECRAB_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif
#include "HFTokenizer.h"

class EmbeddingEngine {
public:
    EmbeddingEngine(const std::string& modelPath,
        const std::string& tokenizerJsonPath,
        bool useGPU = true);

    std::vector<float> encode(const std::string& text);

    int getDimension() const { return dimension; }

private:
#ifdef CLOSECRAB_HAS_ONNX
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    Ort::SessionOptions sessionOptions;
#endif

    std::unique_ptr<HFTokenizer> tokenizer;
    int dimension = 768;
};