/*============================================================================*/
/* File Description                                                           */
/*============================================================================*/
/**
 * @file        DeepLabV3_Inference.hpp

 */
/*============================================================================*/

#ifndef DEEPLABV3_INFERENCE_HPP_
#define DEEPLABV3_INFERENCE_HPP_

/*============================================================================*/
/* Includes                                                                   */
/*============================================================================*/
#include <opencv2/opencv.hpp>
#include <torch/script.h>

#include "DeepLabV3_Inference_Settings.hpp"

/*============================================================================*/
/* define                                                                     */
/*============================================================================*/

/*============================================================================*/
/* namespace                                                                  */
/*============================================================================*/
namespace VBGE {


class DeepLabV3_Inference {
public:

    /*============================================================================*/
    /* Function Description                                                       */
    /*============================================================================*/
    /**
     * @brief         	Run the text detection
     * @param[in] 		i_eastModelPath         : Path to a binary .pb containing the trained network
     * @param[in] 		i_confidenceThreshold	: Confidence threshold
     * @param[in] 		i_nmsThreshold          : Non-maximum suppression threshold
     *
     */
    /*============================================================================*/
    DeepLabV3_Inference(const DeepLabV3_Inference_Settings& i_settings);

    ~DeepLabV3_Inference();

    bool get_isInitialized();


    int run(const cv::Mat& i_image, cv::Mat& o_segmentation);

private:
    // Misc
    bool m_isInitialized = false;

    // Members
    torch::jit::script::Module m_model;

    // Settings
    const DeepLabV3_Inference_Settings m_settings;

    void run_segmentation(const cv::Mat& i_image, cv::Mat& o_segmentation);

    // Utility struct
    typedef struct {
        cv::Mat im;
        cv::Point2i origin_in_source;
    } WindowImage;

    void create_windowList(const cv::Mat                &i_image,
                           const cv::Size                i_windowSize,
                           const cv::Size                i_overlapSize,
                                 std::list<WindowImage> &o_windowList);

    void run_window(const cv::Mat& i_image, cv::Mat& o_noBackgroundMask);

};

} /* namespace VBGE */
#endif /* DEEPLABV3_INFERENCE_HPP_ */