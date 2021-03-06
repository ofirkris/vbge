/*============================================================================*/
/* File Description                                                           */
/*============================================================================*/
/**
 * @file        VideoBackgroundEraser_Algo.cpp

 */
/*============================================================================*/

/*============================================================================*/
/* Includes                                                                   */
/*============================================================================*/
#include <limits>
#include <iomanip>
#include <typeinfo>

#include "Utils_Logging.hpp"

#include "VideoBackgroundEraser_Algo.hpp"

/*============================================================================*/
/* Defines                                                                  */
/*============================================================================*/

/*============================================================================*/
/* namespace                                                                  */
/*============================================================================*/
namespace VBGE {

VideoBackgroundEraser_Algo::VideoBackgroundEraser_Algo(const VideoBackgroundEraser_Settings &i_settings)
    : m_settings(i_settings),
      m_deeplabv3_inference(m_settings.deeplabv3_inference),
      m_deepimagematting_inference(m_settings.deepimagematting_inference)
{

    if(false == m_deeplabv3_inference.get_isInitialized()) {
        logging_error("m_deeplabv3_inference was not correctly initialized.");
        return;
    }

    m_optFLow = cv::DISOpticalFlow::create(cv::DISOpticalFlow::PRESET_MEDIUM);

    m_isInitialized = true;
}

VideoBackgroundEraser_Algo::~VideoBackgroundEraser_Algo()
{

}

bool VideoBackgroundEraser_Algo::get_isInitialized() {
    return m_isInitialized;
}

int VideoBackgroundEraser_Algo::run(const cv::Mat& i_image, cv::Mat& o_image_withoutBackground)
{
    if(false == get_isInitialized()) {
        logging_error("This instance was not correctly initialized.");
        return -1;
    }

    cv::Mat imageFloat;
    if(CV_32F != i_image.depth()) {
        switch(i_image.depth()) {
        case CV_8U: i_image.convertTo(imageFloat, CV_32F, 1./255.); break;
        case CV_16U: i_image.convertTo(imageFloat, CV_32F, 1./65535.); break;
        default:
            logging_error("Unsuported input image depth (" << cv::typeToString(i_image.depth()) << "). Supported depths are CV_32F, CV_16U and CV_8U");
            return -1;
        }
    } else {
        imageFloat = i_image;
    }

    CV_Assert(CV_32FC3 == imageFloat.type());

    // Run segmentation with DeepLabV3 to create a mask of the background
    cv::Mat segmentation;
    m_deeplabv3_inference.run(imageFloat, segmentation);

    // Debug display
//    {
//        cv::Mat segmentation_uint8;
//        segmentation.convertTo(segmentation_uint8, CV_8U, 50);
//        cv::Mat segmentationColor;
//        cv::applyColorMap(segmentation_uint8, segmentationColor, cv::COLORMAP_HSV);
//        cv::imshow("segmentationColor", segmentationColor);
//    }
//    {
//        cv::Mat segmentation_uint8;
//        segmentation.convertTo(segmentation_uint8, CV_8U);
//        cv::cvtColor(segmentation_uint8, segmentation_uint8, cv::COLOR_GRAY2BGR);
//        cv::imshow("segmentation_uint8", segmentation_uint8);
//    }

    // Create background mask
    cv::Mat backgroundMask;
    if(m_settings.deeplabv3_inference.background_classId_vector.empty()) {
        logging_error("m_settings.deeplabv3_inference.background_classId_vector is empty.");
        return -1;
    }
    for(auto& background_id : m_settings.deeplabv3_inference.background_classId_vector) {
        if(backgroundMask.empty()) {
            backgroundMask = (segmentation == background_id);
        } else {
            backgroundMask = backgroundMask | (segmentation == background_id);
        }
    }

    // Prepare intermediary data
    cv::Mat foregroundMask;
    cv::Mat image_rgb_uint8;
    imageFloat.convertTo(image_rgb_uint8, CV_8U, 255.);

    // Run temporal processing to try and keep consistency between successive frames
    if(m_settings.enable_temporalManagement) {
        if(0 > temporalManagement(image_rgb_uint8, backgroundMask, foregroundMask)) {
            logging_error("temporalManagement() failed.");
            return -1;
        }
    } else {
        foregroundMask = 0 == backgroundMask;
    }

    // Generate trimap
    cv::Mat trimap;
    {
        // Downscale
        const float scale = m_settings.imageMatting_scale;
        cv::Mat foregroundMask_down, trimap_down;
        cv::resize(foregroundMask, foregroundMask_down, cv::Size(), scale, scale, cv::INTER_NEAREST);
        // Generate trimap
        compute_trimap(foregroundMask_down, trimap_down);
        // Upscale
        cv::resize(trimap_down, trimap, foregroundMask.size(), 0, 0, cv::INTER_NEAREST);
    }

    // Convert image rgb with trimap to make a rgba image
    cv::Mat imageFloat_rgba;
    std::vector<cv::Mat> image_rgba_planar;
    cv::split(imageFloat, image_rgba_planar);
    image_rgba_planar.push_back(cv::Mat());
    trimap.convertTo(image_rgba_planar.back(), CV_32F, 1./255.);
    cv::merge(image_rgba_planar, imageFloat_rgba);


    // Run Deep Image Matting
    cv::Mat alpha_prediction;
    {
        // Downscale
        const float scale = m_settings.imageMatting_scale;
        cv::Mat imageFloat_rgba_down;
        cv::Mat alpha_prediction_down;
        cv::resize(imageFloat_rgba, imageFloat_rgba_down, cv::Size(), scale, scale, cv::INTER_AREA);
        // Run DIM
        m_deepimagematting_inference.run(imageFloat_rgba_down, alpha_prediction_down);
        // Upscale
        cv::resize(alpha_prediction_down, alpha_prediction, imageFloat_rgba.size(), 0, 0, cv::INTER_CUBIC);
    }

    // Post process alpha_prediction
    alpha_prediction.setTo(0, 0 == trimap);
    alpha_prediction.setTo(255, 255 == trimap);

    // Replace alpha in image_rgba_planar with alpha_prediction
    image_rgba_planar[3] = alpha_prediction;
    cv::Mat image_withoutBackground;
    cv::merge(image_rgba_planar, image_withoutBackground);

    // Facultative conversion to the same depth as input
    if(CV_32F != i_image.depth()) {
        switch(i_image.depth()) {
        case CV_8U: image_withoutBackground.convertTo(o_image_withoutBackground, CV_8U, 255.); break;
        case CV_16U: image_withoutBackground.convertTo(o_image_withoutBackground, CV_16U, 65535.); break;
        default:
            logging_error("Unsuported input image depth (" << cv::typeToString(i_image.depth()) << "). Supported depths are CV_32F, CV_16U and CV_8U");
            return -1;
        }
    } else {
        o_image_withoutBackground = image_withoutBackground;
    }

    return 0;
}


int VideoBackgroundEraser_Algo::temporalManagement(const cv::Mat& i_image_rgb_uint8, const cv::Mat& i_backgroundMask, cv::Mat& o_foregroundMask)
{
    cv::Mat image_uint8;
    cv::cvtColor(i_image_rgb_uint8, image_uint8, cv::COLOR_BGR2GRAY);
    cv::Mat foregroundDetection = 0 == i_backgroundMask;

    constexpr int nbPQ = 1;
    constexpr int P[nbPQ] = {2};//, 2};
    constexpr int Q[nbPQ] = {3};//, 8};
    uint sumQ = 0;
    for(int i = 0 ; i < nbPQ ; ++i) {
        sumQ += Q[i];
    }

    if(!m_image_prev.empty()) {

        // Compute optical flow betwen previous and current image
        m_optFLow->calc(image_uint8, m_image_prev, m_flow);

        // Convert flow to coordinates map
        m_mapXY.create(m_flow.size(), m_flow.type());
        for(int y = 0 ; y < m_flow.rows ; ++y) {
            for(int x = 0 ; x < m_flow.cols ; ++x) {
                auto& f = m_flow.at<cv::Vec2f>(y, x);
                m_mapXY.at<cv::Vec2f>(y, x) = {f(0) + x, f(1) + y};
            }
        }

        // Remap past detections onto current referential
        for(auto& detections : m_detections_history) {
            cv::remap(detections, detections, m_mapXY, cv::noArray(), cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));
        }
        cv::remap(m_statusMap, m_statusMap, m_mapXY, cv::noArray(), cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));

        // Populate detections_history with new foregroundDetection
        {
            cv::Mat newDetection = cv::Mat::zeros(foregroundDetection.size(), CV_8U);
            newDetection.setTo(1, foregroundDetection);
            m_detections_history.push_front(newDetection);
            // Ensure we don't have too many detections
            while(m_detections_history.size() > sumQ) {
                m_detections_history.pop_back();
            }
        }

        // Sum detections, the value in sumDetections will be the number of times
        // this pixel was detected as foreground in the past images
        cv::Mat confirmedDetection;
        {
            cv::Mat sumDetections[2] = {cv::Mat::zeros(foregroundDetection.size(), CV_8U),
                                        cv::Mat::zeros(foregroundDetection.size(), CV_8U)};
            int idx = 0;
            int counter = 0;
            for(auto& detections : m_detections_history) {
                sumDetections[idx] = sumDetections[idx] + detections;
                if(++counter >= Q[idx]) {
                    ++idx;
                    counter = 0;
                }
            }

            // A detection is valid if it was seen more than P times in the last Q frames
            confirmedDetection = sumDetections[0] > P[0];
            for(int i = 1 ; i < nbPQ ; ++i) {
                confirmedDetection = confirmedDetection & (sumDetections[i] > P[i]);
            }
        }

        // Update status map
        m_statusMap.setTo(1, confirmedDetection);
        // Increase values of statusMap which were once confirmed but have not been seen recently
        cv::add(m_statusMap, 1, m_statusMap, (0 == confirmedDetection) & (0 != m_statusMap));
        // Remove the old confirmed values which have not been seen for too long
        m_statusMap.setTo(0, m_statusMap > 2);

        // Effective foreground is when statusMap is valid and we also add the current foreground detection
        o_foregroundMask = (0 != m_statusMap);

    } else {
        m_statusMap = cv::Mat::zeros(image_uint8.size(), CV_8U);

        o_foregroundMask.create(foregroundDetection.size(), CV_8U);
        o_foregroundMask.setTo(0);
    }

    image_uint8.copyTo(m_image_prev);

    return 0;
}

void VideoBackgroundEraser_Algo::compute_trimap(const cv::Mat& i_foreground, cv::Mat &o_trimap)
{
    // Generate standard trimap with morpho maths
    constexpr int kSize = 3;
    constexpr int dilate_iterations = 1;
    constexpr int erode_iterations = 15;
    auto kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kSize, kSize));
    cv::Mat dilated;
    cv::dilate(i_foreground, dilated, kernel, cv::Point(-1, -1), dilate_iterations);
    cv::Mat eroded;
    cv::erode(i_foreground, eroded, kernel, cv::Point(-1, -1), erode_iterations);

    o_trimap.create(i_foreground.size(), CV_8U);
    o_trimap.setTo(128);
    o_trimap.setTo(255, eroded >= 255);
    o_trimap.setTo(0, dilated <= 0);
}

} /* namespace VBGE */
