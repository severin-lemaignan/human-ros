// Copyright (C) 2018-2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

/**
* \brief The entry point for the Inference Engine Human Pose Estimation demo application
* \file human_pose_estimation_demo/main.cpp
* \example human_pose_estimation_demo/main.cpp
*/

#include <vector>
#include <chrono>
#include <iomanip>

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

#include <inference_engine.hpp>

#include "human_pose_estimator.hpp"
#include "render_human_pose.hpp"

using namespace InferenceEngine;
using namespace human_pose_estimation;

int main(int argc, char* argv[]) {
    try {
        std::cout << "InferenceEngine: " << GetInferenceEngineVersion() << std::endl;

        // ------------------------------ Parsing and validation of input args ---------------------------------

        HumanPoseEstimator estimator("/home/severin/openvino_models/intel/human-pose-estimation-0001/FP16/human-pose-estimation-0001.xml", "CPU", false);
        cv::VideoCapture cap;
        cap.open(0);

        int delay = 33;

        // read input (video) frame
        cv::Mat curr_frame; cap >> curr_frame;
        cv::Mat next_frame;
        if (!cap.grab()) {
            throw std::logic_error("Failed to get frame from cv::VideoCapture");
        }

        estimator.reshape(curr_frame);  // Do not measure network reshape, if it happened

        std::cout << "To close the application, press 'CTRL+C' here";
        std::cout << " or switch to the output window and press ESC key" << std::endl;
        std::cout << "To pause execution, switch to the output window and press 'p' key" << std::endl;
    std::cout << std::endl;

        cv::Size graphSize{static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH) / 4), 60};
        std::vector<HumanPose> poses;
        bool isLastFrame = false;
        bool isAsyncMode = false; // execution is always started in SYNC mode
        bool isModeChanged = false; // set to true when execution mode is changed (SYNC<->ASYNC)
        bool blackBackground = false;

        typedef std::chrono::duration<double, std::ratio<1, 1000>> ms;
        auto total_t0 = std::chrono::high_resolution_clock::now();
        auto wallclock = std::chrono::high_resolution_clock::now();
        double render_time = 0;

        while (true) {
            auto t0 = std::chrono::high_resolution_clock::now();
            //here is the first asynchronus point:
            //in the async mode we capture frame to populate the NEXT infer request
            //in the regular mode we capture frame to the current infer request

            if (!cap.read(next_frame)) {
                if (next_frame.empty()) {
                    isLastFrame = true; //end of video file
                } else {
                    throw std::logic_error("Failed to get frame from cv::VideoCapture");
                }
            }
            if (isAsyncMode) {
                if (isModeChanged) {
                    estimator.frameToBlobCurr(curr_frame);
                }
                if (!isLastFrame) {
                    estimator.frameToBlobNext(next_frame);
                }
            } else if (!isModeChanged) {
                estimator.frameToBlobCurr(curr_frame);
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            double decode_time = std::chrono::duration_cast<ms>(t1 - t0).count();

            t0 = std::chrono::high_resolution_clock::now();
            // Main sync point:
            // in the trully Async mode we start the NEXT infer request, while waiting for the CURRENT to complete
            // in the regular mode we start the CURRENT request and immediately wait for it's completion
            if (isAsyncMode) {
                if (isModeChanged) {
                    estimator.startCurr();
                }
                if (!isLastFrame) {
                    estimator.startNext();
                }
            } else if (!isModeChanged) {
                estimator.startCurr();
            }

            if (estimator.readyCurr()) {
                t1 = std::chrono::high_resolution_clock::now();
                ms detection = std::chrono::duration_cast<ms>(t1 - t0);
                t0 = std::chrono::high_resolution_clock::now();
                ms wall = std::chrono::duration_cast<ms>(t0 - wallclock);
                wallclock = t0;

                t0 = std::chrono::high_resolution_clock::now();

                if (blackBackground) {
                    curr_frame = cv::Mat::zeros(curr_frame.size(), curr_frame.type());
                }
                std::ostringstream out;
                out << "OpenCV cap/render time: " << std::fixed << std::setprecision(2)
                    << (decode_time + render_time) << " ms";

                cv::putText(curr_frame, out.str(), cv::Point2f(0, 25),
                            cv::FONT_HERSHEY_TRIPLEX, 0.6, cv::Scalar(0, 255, 0));
                out.str("");
                out << "Wallclock time " << (isAsyncMode ? "(TRUE ASYNC):      " : "(SYNC, press Tab): ");
                out << std::fixed << std::setprecision(2) << wall.count()
                    << " ms (" << 1000.f / wall.count() << " fps)";
                cv::putText(curr_frame, out.str(), cv::Point2f(0, 50),
                            cv::FONT_HERSHEY_TRIPLEX, 0.6, cv::Scalar(0, 0, 255));
                if (!isAsyncMode) {  // In the true async mode, there is no way to measure detection time directly
                    out.str("");
                    out << "Detection time  : " << std::fixed << std::setprecision(2) << detection.count()
                    << " ms ("
                    << 1000.f / detection.count() << " fps)";
                    cv::putText(curr_frame, out.str(), cv::Point2f(0, 75), cv::FONT_HERSHEY_TRIPLEX, 0.6,
                        cv::Scalar(255, 0, 0));
                }

                poses = estimator.postprocessCurr();
//
//                if (FLAGS_r) {
//                    if (!poses.empty()) {
//                        std::time_t result = std::time(nullptr);
//                        char timeString[sizeof("2020-01-01 00:00:00: ")];
//                        std::strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S: ", std::localtime(&result));
//                        std::cout << timeString;
//                     }
//
//                    for (HumanPose const& pose : poses) {
//                        std::stringstream rawPose;
//                        rawPose << std::fixed << std::setprecision(0);
//                        for (auto const& keypoint : pose.keypoints) {
//                            rawPose << keypoint.x << "," << keypoint.y << " ";
//                        }
//                        rawPose << pose.score;
//                        std::cout << rawPose.str() << std::endl;
//                    }
//                }
//
                    renderHumanPose(poses, curr_frame);
                    cv::imshow("Human Pose Estimation", curr_frame);
                    t1 = std::chrono::high_resolution_clock::now();
                    render_time = std::chrono::duration_cast<ms>(t1 - t0).count();
                }

            if (isLastFrame) {
                break;
            }

            if (isModeChanged) {
                isModeChanged = false;
            }

            // Final point:
            // in the truly Async mode we swap the NEXT and CURRENT requests for the next iteration
            curr_frame = next_frame;
            next_frame = cv::Mat();
            if (isAsyncMode) {
                estimator.swapRequest();
            }

            const int key = cv::waitKey(delay) & 255;
            if (key == 'p') {
                delay = (delay == 0) ? 33 : 0;
            } else if (27 == key) { // Esc
                break;
            } else if (9 == key) { // Tab
                isAsyncMode ^= true;
                isModeChanged = true;
            } else if (32 == key) { // Space
                blackBackground ^= true;
            }
        }

        auto total_t1 = std::chrono::high_resolution_clock::now();
        ms total = std::chrono::duration_cast<ms>(total_t1 - total_t0);
        std::cout << "Total Inference time: " << total.count() << std::endl;
    }
    catch (const std::exception& error) {
        std::cerr << "[ ERROR ] " << error.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (...) {
        std::cerr << "[ ERROR ] Unknown/internal exception happened." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Execution successful" << std::endl;
    return EXIT_SUCCESS;
}
