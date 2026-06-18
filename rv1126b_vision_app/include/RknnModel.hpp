#pragma once

#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rv1126b {

struct RknnTensorInfo {
    uint32_t index{0};
    std::string name;
    std::vector<uint32_t> dims;
    uint32_t size{0};
};

class RknnModel {
public:
    RknnModel();
    ~RknnModel();

    RknnModel(const RknnModel&) = delete;
    RknnModel& operator=(const RknnModel&) = delete;

    bool load(const std::string& model_path);
    bool run(const Frame& input, std::vector<std::vector<float>>& outputs);
    void unload();

    bool loaded() const;
    bool usingRealRknn() const;
    const std::vector<RknnTensorInfo>& inputInfos() const;
    const std::vector<RknnTensorInfo>& outputInfos() const;

private:
    struct Impl;

    bool readModelFile(const std::string& model_path, std::vector<uint8_t>& model_data) const;

    std::unique_ptr<Impl> impl_;
};

}  // namespace rv1126b
