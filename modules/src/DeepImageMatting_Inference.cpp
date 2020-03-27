/*============================================================================*/
/* File Description                                                           */
/*============================================================================*/
/**
 * @file        DeepImageMatting_Inference.cpp

 */
/*============================================================================*/

/*============================================================================*/
/* Includes                                                                   */
/*============================================================================*/
#include <limits>
#include <iomanip>
#include <typeinfo>

#include "Utils_Logging.hpp"

#include "DeepImageMatting_Inference.hpp"

/*============================================================================*/
/* Defines                                                                  */
/*============================================================================*/

/*============================================================================*/
/* namespace                                                                  */
/*============================================================================*/
namespace VBGE {

DeepImageMatting_Inference::DeepImageMatting_Inference(const DeepImageMatting_Inference_Settings& i_settings)
    : m_settings(i_settings)
{
    m_model = torch::jit::load(m_settings.model_path, m_settings.inferenceDeviceType);

    m_isInitialized = true;
}

DeepImageMatting_Inference::~DeepImageMatting_Inference()
{

}

bool DeepImageMatting_Inference::get_isInitialized() {
    return m_isInitialized;
}


int DeepImageMatting_Inference::run(const cv::Mat& i_image_rgba, cv::Mat& o_enhanced_image_rgba)
{
    if(false == get_isInitialized()) {
        logging_error("This instance was not correctly initialized.");
        return -1;
    }

    // We don't want to save the gradients during net.forward()
    torch::NoGradGuard no_grad_guard;

    // Prepare Input
    // Encapsulate i_src in a tensor (no deep copy) with OpenCV format NHWC
    std::vector<int64_t> srcSize = {1, i_image_rgba.rows, i_image_rgba.cols, i_image_rgba.channels()};
    std::vector<int64_t> srcStride = {1, static_cast<int64_t>(i_image_rgba.step1()), i_image_rgba.channels(), 1};
    torch::Tensor srcTensor_NHWC = torch::from_blob(i_image_rgba.data, srcSize, srcStride, torch::kCPU);
    // Permute format NHWC (OpenCV) to NCHW (PyTorch) (no deep copy)
    torch::Tensor inputTensor_NCHW = srcTensor_NHWC.permute({0, 3, 1, 2}).to(m_settings.inferenceDeviceType);

    // Inference
    std::vector<torch::jit::IValue> inputs;
    inputs.push_back(inputTensor_NCHW);
    // /!\ Dynamic alloc
    torch::Tensor neuralNet_outputTensor_NCHW = m_model.forward(inputs).toTensor();

    // Prepare output
    // Permute format NCHW (PyTorch) to NHWC (OpenCV) (no deep copy)
    torch::Tensor neuralNet_outputTensor_NHWC = neuralNet_outputTensor_NCHW.permute({0, 2, 3, 1});
    // Encapsulate o_enhanced_image_rgba in a tensor (no deep copy) with OpenCV format NHWC
    std::vector<int64_t> dstSize = {1, o_enhanced_image_rgba.rows, o_enhanced_image_rgba.cols, o_enhanced_image_rgba.channels()};
    std::vector<int64_t> dstStride = {1, static_cast<int64_t>(o_enhanced_image_rgba.step1()), o_enhanced_image_rgba.channels(), 1};
    torch::Tensor dstTensor_NHWC = torch::from_blob(o_enhanced_image_rgba.data, dstSize, dstStride, torch::kCPU);
    // Copy neuralNet_outputTensor_NHWC to outputTensor_NHWC
    dstTensor_NHWC.copy_(neuralNet_outputTensor_NHWC, true);

    return 0;
}

} /* namespace VBGE */