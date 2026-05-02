#include <rknn_api.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static std::vector<unsigned char> ReadFile(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    throw std::runtime_error("failed to open model: " + path);
  }
  ifs.seekg(0, std::ios::end);
  std::streamoff size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  std::vector<unsigned char> data(static_cast<size_t>(size));
  ifs.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

static const char* TensorTypeName(rknn_tensor_type t) {
  switch (t) {
    case RKNN_TENSOR_FLOAT32: return "float32";
    case RKNN_TENSOR_FLOAT16: return "float16";
    case RKNN_TENSOR_INT8: return "int8";
    case RKNN_TENSOR_UINT8: return "uint8";
    case RKNN_TENSOR_INT16: return "int16";
    case RKNN_TENSOR_UINT16: return "uint16";
    case RKNN_TENSOR_INT32: return "int32";
    case RKNN_TENSOR_UINT32: return "uint32";
    default: return "unknown";
  }
}

static const char* TensorFmtName(rknn_tensor_format f) {
  switch (f) {
    case RKNN_TENSOR_NCHW: return "NCHW";
    case RKNN_TENSOR_NHWC: return "NHWC";
    case RKNN_TENSOR_NC1HWC2: return "NC1HWC2";
    case RKNN_TENSOR_UNDEFINED: return "UNDEFINED";
    default: return "unknown";
  }
}

static void PrintAttr(const char* prefix, const rknn_tensor_attr& attr) {
  std::cout << prefix << "[" << attr.index << "]"
            << " name=" << attr.name
            << " dims=";
  for (uint32_t i = 0; i < attr.n_dims; ++i) {
    std::cout << attr.dims[i] << (i + 1 == attr.n_dims ? "" : "x");
  }
  std::cout << " fmt=" << TensorFmtName(attr.fmt)
            << " type=" << TensorTypeName(attr.type)
            << " size=" << attr.size
            << " size_with_stride=" << attr.size_with_stride
            << " n_elems=" << attr.n_elems
            << " qnt_type=" << attr.qnt_type
            << " zp=" << attr.zp
            << " scale=" << attr.scale
            << std::endl;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <model.rknn>" << std::endl;
    return 2;
  }
  std::string model_path = argv[1];
  std::cout << "MODEL " << model_path << std::endl;

  std::vector<unsigned char> model;
  try {
    model = ReadFile(model_path);
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 2;
  }
  std::cout << "MODEL_BYTES " << model.size() << std::endl;

  rknn_context ctx = 0;
  int ret = rknn_init(&ctx, model.data(), model.size(), 0, nullptr);
  std::cout << "INIT ret=" << ret << " ctx=" << ctx << std::endl;
  if (ret != RKNN_SUCC) return 1;

  rknn_input_output_num io = {};
  ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io));
  std::cout << "IO ret=" << ret << " inputs=" << io.n_input
            << " outputs=" << io.n_output << std::endl;
  if (ret != RKNN_SUCC) {
    rknn_destroy(ctx);
    return 1;
  }

  std::vector<rknn_tensor_attr> input_attrs(io.n_input);
  for (uint32_t i = 0; i < io.n_input; ++i) {
    input_attrs[i].index = i;
    ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(input_attrs[i]));
    std::cout << "QUERY_INPUT_ATTR index=" << i << " ret=" << ret << std::endl;
    if (ret == RKNN_SUCC) PrintAttr("INPUT", input_attrs[i]);
  }
  for (uint32_t i = 0; i < io.n_output; ++i) {
    rknn_tensor_attr attr = {};
    attr.index = i;
    ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
    std::cout << "QUERY_OUTPUT_ATTR index=" << i << " ret=" << ret << std::endl;
    if (ret == RKNN_SUCC) PrintAttr("OUTPUT", attr);
  }

  std::vector<std::vector<unsigned char>> buffers(io.n_input);
  std::vector<rknn_input> inputs(io.n_input);
  for (uint32_t i = 0; i < io.n_input; ++i) {
    const auto& attr = input_attrs[i];
    size_t size = attr.size > 0 ? attr.size : attr.size_with_stride;
    buffers[i].assign(size, 0);
    inputs[i] = {};
    inputs[i].index = i;
    inputs[i].buf = buffers[i].data();
    inputs[i].size = static_cast<uint32_t>(buffers[i].size());
    inputs[i].fmt = attr.fmt;
    inputs[i].type = attr.type;
    std::cout << "SET_INPUT index=" << i << " size=" << inputs[i].size
              << " fmt=" << TensorFmtName(inputs[i].fmt)
              << " type=" << TensorTypeName(inputs[i].type) << std::endl;
  }

  ret = rknn_inputs_set(ctx, io.n_input, inputs.data());
  std::cout << "INPUTS_SET ret=" << ret << std::endl;
  if (ret != RKNN_SUCC) {
    rknn_destroy(ctx);
    return 1;
  }

  std::cout << "RUN_BEGIN" << std::endl;
  ret = rknn_run(ctx, nullptr);
  std::cout << "RUN ret=" << ret << std::endl;
  if (ret != RKNN_SUCC) {
    rknn_destroy(ctx);
    return 1;
  }

  std::vector<rknn_output> outputs(io.n_output);
  for (auto& out : outputs) {
    out.want_float = 1;
    out.is_prealloc = 0;
  }
  ret = rknn_outputs_get(ctx, io.n_output, outputs.data(), nullptr);
  std::cout << "OUTPUTS_GET ret=" << ret << std::endl;
  if (ret == RKNN_SUCC) {
    for (uint32_t i = 0; i < io.n_output; ++i) {
      std::cout << "OUTPUT[" << i << "] size=" << outputs[i].size
                << " buf=" << outputs[i].buf << std::endl;
    }
    rknn_outputs_release(ctx, io.n_output, outputs.data());
  }

  rknn_destroy(ctx);
  std::cout << "DONE" << std::endl;
  return ret == RKNN_SUCC ? 0 : 1;
}
