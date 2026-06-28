#include "RknnModel.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <utility>

#if defined(RV1126B_HAS_RKNN)
#include <rknn_api.h>
#endif

namespace rv1126b {

namespace {

std::string dimsToString(const std::vector<uint32_t>& dims) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << dims[i];
    }
    oss << "]";
    return oss.str();
}

}  // namespace

struct RknnModel::Impl {
#if defined(RV1126B_HAS_RKNN)
    rknn_context ctx{0};
    rknn_input_output_num io_num{};
#endif
    std::vector<RknnTensorInfo> input_infos;
    std::vector<RknnTensorInfo> output_infos;
    bool loaded{false};
    bool real_rknn{false};
};

RknnModel::RknnModel() : impl_(std::make_unique<Impl>()) {}

RknnModel::~RknnModel() {
    unload();
}

bool RknnModel::load(const std::string& model_path) {
    unload();

    std::vector<uint8_t> model_data;
    if (!readModelFile(model_path, model_data)) {
        std::cerr << "[RKNN] read model failed: " << model_path << "\n";
        return false;
    }

#if defined(RV1126B_HAS_RKNN)
    // RKNN 生命周期：
    // 1. 读取 .rknn 模型文件到内存。
    // 2. rknn_init 创建 context。
    // 3. 查询输入/输出 tensor 数量和属性。
    // 4. 推理时 set input -> run -> get output -> release output。
    // 5. 程序退出或模型切换时 rknn_destroy。
    int ret = rknn_init(&impl_->ctx, model_data.data(), static_cast<uint32_t>(model_data.size()), 0, nullptr);
    if (ret != RKNN_SUCC) {
        std::cerr << "[RKNN] rknn_init failed, ret=" << ret << "\n";
        return false;
    }

    ret = rknn_query(impl_->ctx, RKNN_QUERY_IN_OUT_NUM, &impl_->io_num, sizeof(impl_->io_num));
    if (ret != RKNN_SUCC) {
        std::cerr << "[RKNN] query io num failed, ret=" << ret << "\n";
        unload();
        return false;
    }

    impl_->input_infos.clear();
    impl_->output_infos.clear();

    for (uint32_t i = 0; i < impl_->io_num.n_input; ++i) {
        rknn_tensor_attr attr{};
        attr.index = i;
        ret = rknn_query(impl_->ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));
        if (ret != RKNN_SUCC) {
            std::cerr << "[RKNN] query input attr failed, index=" << i << ", ret=" << ret << "\n";
            unload();
            return false;
        }

        RknnTensorInfo info;
        info.index = i;
        info.name = attr.name;
        info.n_dims = attr.n_dims;
        info.n_elems = attr.n_elems;
        info.size = attr.size;
        info.fmt = static_cast<int>(attr.fmt);
        info.type = static_cast<int>(attr.type);
        info.qnt_type = static_cast<int>(attr.qnt_type);
        info.zp = attr.zp;
        info.scale = attr.scale;
        for (uint32_t d = 0; d < attr.n_dims; ++d) {
            info.dims.push_back(static_cast<uint32_t>(attr.dims[d]));
        }
        impl_->input_infos.push_back(std::move(info));
    }

    for (uint32_t i = 0; i < impl_->io_num.n_output; ++i) {
        rknn_tensor_attr attr{};
        attr.index = i;
        ret = rknn_query(impl_->ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
        if (ret != RKNN_SUCC) {
            std::cerr << "[RKNN] query output attr failed, index=" << i << ", ret=" << ret << "\n";
            unload();
            return false;
        }

        RknnTensorInfo info;
        info.index = i;
        info.name = attr.name;
        info.n_dims = attr.n_dims;
        info.n_elems = attr.n_elems;
        info.size = attr.size;
        info.fmt = static_cast<int>(attr.fmt);
        info.type = static_cast<int>(attr.type);
        info.qnt_type = static_cast<int>(attr.qnt_type);
        info.zp = attr.zp;
        info.scale = attr.scale;
        for (uint32_t d = 0; d < attr.n_dims; ++d) {
            info.dims.push_back(static_cast<uint32_t>(attr.dims[d]));
        }
        impl_->output_infos.push_back(std::move(info));
    }

    impl_->loaded = true;
    impl_->real_rknn = true;
    std::cout << "[RKNN] model loaded: " << model_path
              << ", inputs=" << impl_->input_infos.size()
              << ", outputs=" << impl_->output_infos.size() << "\n";
    std::cout << "[RKNN][OutputAttr] model=" << model_path
              << ", outputs=" << impl_->output_infos.size() << "\n";
    for (const auto& info : impl_->output_infos) {
        std::cout << "[RKNN][OutputAttr] idx=" << info.index
                  << ", name=" << info.name
                  << ", n_dims=" << info.n_dims
                  << ", dims=" << dimsToString(info.dims)
                  << ", n_elems=" << info.n_elems
                  << ", size=" << info.size
                  << ", fmt=" << info.fmt
                  << ", type=" << info.type
                  << ", qnt_type=" << info.qnt_type
                  << ", zp=" << info.zp
                  << ", scale=" << info.scale << "\n";
    }
    return true;
#else
    (void)model_data;
    // 普通开发环境没有 rknn_api.h/librknnrt 时，返回 false 让业务模型走 fallback。
    // 这样不会伪装成真实推理成功，避免调试时误判。
    std::cerr << "[RKNN] SDK not available, fallback mode required for: " << model_path << "\n";
    return false;
#endif
}

bool RknnModel::run(const Frame& input, std::vector<std::vector<float>>& outputs) {
    outputs.clear();

#if defined(RV1126B_HAS_RKNN)
    if (!impl_->loaded || !impl_->real_rknn || impl_->input_infos.empty()) {
        return false;
    }
    if (input.data.empty()) {
        return false;
    }

    rknn_input rknn_input_data{};
    rknn_input_data.index = 0;
    rknn_input_data.type = RKNN_TENSOR_UINT8;
    rknn_input_data.fmt = RKNN_TENSOR_NHWC;
    rknn_input_data.size = static_cast<uint32_t>(input.data.size());
    rknn_input_data.buf = const_cast<uint8_t*>(input.data.data());
    rknn_input_data.pass_through = 0;

    int ret = rknn_inputs_set(impl_->ctx, 1, &rknn_input_data);
    if (ret != RKNN_SUCC) {
        std::cerr << "[RKNN] inputs_set failed, ret=" << ret << "\n";
        return false;
    }

    ret = rknn_run(impl_->ctx, nullptr);
    if (ret != RKNN_SUCC) {
        std::cerr << "[RKNN] run failed, ret=" << ret << "\n";
        return false;
    }

    std::vector<rknn_output> rknn_outputs(impl_->output_infos.size());
    for (uint32_t i = 0; i < rknn_outputs.size(); ++i) {
        rknn_outputs[i].index = i;
        // want_float=1 让 RKNN runtime 反量化为 float，解析层不再关心量化参数。
        rknn_outputs[i].want_float = 1;
        rknn_outputs[i].is_prealloc = 0;
    }

    ret = rknn_outputs_get(impl_->ctx, static_cast<uint32_t>(rknn_outputs.size()), rknn_outputs.data(), nullptr);
    if (ret != RKNN_SUCC) {
        std::cerr << "[RKNN] outputs_get failed, ret=" << ret << "\n";
        return false;
    }

    outputs.reserve(rknn_outputs.size());
    for (const auto& output : rknn_outputs) {
        const auto* values = static_cast<const float*>(output.buf);
        const std::size_t count = output.size / sizeof(float);
        outputs.emplace_back(values, values + count);
    }

    rknn_outputs_release(impl_->ctx, static_cast<uint32_t>(rknn_outputs.size()), rknn_outputs.data());
    return true;
#else
    (void)input;
    return false;
#endif
}

void RknnModel::unload() {
#if defined(RV1126B_HAS_RKNN)
    if (impl_ && impl_->ctx != 0) {
        rknn_destroy(impl_->ctx);
        impl_->ctx = 0;
    }
#endif
    if (impl_) {
        impl_->input_infos.clear();
        impl_->output_infos.clear();
        impl_->loaded = false;
        impl_->real_rknn = false;
    }
}

bool RknnModel::loaded() const {
    return impl_->loaded;
}

bool RknnModel::usingRealRknn() const {
    return impl_->real_rknn;
}

const std::vector<RknnTensorInfo>& RknnModel::inputInfos() const {
    return impl_->input_infos;
}

const std::vector<RknnTensorInfo>& RknnModel::outputInfos() const {
    return impl_->output_infos;
}

bool RknnModel::readModelFile(const std::string& model_path, std::vector<uint8_t>& model_data) const {
    if (model_path.empty()) {
        return false;
    }

    std::ifstream file(model_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    model_data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !model_data.empty();
}

}  // namespace rv1126b
