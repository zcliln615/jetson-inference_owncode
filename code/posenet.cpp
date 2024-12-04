
/**
 * @file posenet.cpp
 * @brief 基于Jetson Inference的姿势估计demo posenet.cpp更改的简单例子
 * 这个例子使用Jetson Inference库中的poseNet类来运行姿势估计网络。
 * 增加了一个功能，将检测到的关键点写入CSV文件。
 *
 * @author Fourierlin
 * @date 2024-12-4
 */

/*
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 *包含所需的头文件，分别用于视频输入、视频输出、姿势估计网络以及信号处理。
 */
#include <jetson-utils/videoSource.h>
#include <jetson-utils/videoOutput.h>
#include <jetson-inference/poseNet.h>
#include <signal.h>

#include <fstream> // 添加头文件以处理文件操作

bool signal_recieved = false; // 定义一个全局布尔变量，用于标记是否接收到终止信号。

/*
 *定义一个信号处理函数，当接收到SIGINT信号时（通常由Ctrl+C触发），会设置signal_recieved为true。
 */
void sig_handler(int signo)
{
    if (signo == SIGINT)
    {
        LogVerbose("received SIGINT\n");
        signal_recieved = true;
    }
}

/*
 *定义一个用法函数，打印程序的使用说明和参数说明，并返回0表示成功执行。
 */
int usage()
{
    printf("usage: posenet [--help] [--network=NETWORK] ...\n");
    printf("                input_URI [output_URI]\n\n");
    printf("Run pose estimation DNN on a video/image stream.\n");
    printf("See below for additional arguments that may not be shown above.\n\n");
    printf("positional arguments:\n");
    printf("    input_URI       resource URI of input stream  (see videoSource below)\n");
    printf("    output_URI      resource URI of output stream (see videoOutput below)\n\n");

    printf("%s", poseNet::Usage());
    printf("%s", videoSource::Usage());
    printf("%s", videoOutput::Usage());
    printf("%s", Log::Usage());

    return 0;
}

int main(int argc, char **argv)
{
    /*
     * 主函数开始，解析命令行参数。如果传入了--help标志，则调用usage()函数并返回。
     */
    commandLine cmdLine(argc, argv);
    if (cmdLine.GetFlag("help"))
        return usage();

    /*
     *设置信号处理函数，如果设置失败，记录错误日志。
     */
    if (signal(SIGINT, sig_handler) == SIG_ERR)
        LogError("can't catch SIGINT\n");

    /*
     * 创建输入流对象input，如果创建失败，记录错误日志并返回1表示错误。
     */
    videoSource *input = videoSource::Create(cmdLine, ARG_POSITION(0));

    if (!input)
    {
        LogError("posenet: failed to create input stream\n");
        return 1;
    }

    /*
     * 创建输出流对象output，如果创建失败，记录错误日志并返回1表示错误。
     */
    videoOutput *output = videoOutput::Create(cmdLine, ARG_POSITION(1));

    if (!output)
    {
        LogError("posenet: failed to create output stream\n");
        return 1;
    }

    /*
     * 创建姿势估计网络对象net，如果创建失败，记录错误日志并返回1表示错误。
     */
    poseNet *net = poseNet::Create(cmdLine);

    if (!net)
    {
        LogError("posenet: failed to initialize poseNet\n");
        return 1;
    }

    // 解析叠加标志，控制在输出图像上绘制的姿势信息。
    const uint32_t overlayFlags = poseNet::OverlayFlagsFromStr(cmdLine.GetString("overlay", "links,keypoints"));

    std::ofstream keypointsFile("keypoints.csv");                                                                      // 打开一个文件用于保存关键点
    keypointsFile << "@video_source: " << "width: " << input->GetWidth() << " height: " << input->GetHeight() << "\n"; // 写入视频源的宽度和高度
    keypointsFile << "frame,person_id,keypoint_id,x,y\n";                                                              // 写入CSV文件头

    /*
     * 进入处理循环，循环条件是没有接收到终止信号。
     * 捕获下一帧图像，如果捕获失败且状态是超时，继续等待下一帧；否则，跳出循环。
     */

    size_t frame = 0; // 定义帧编号

    while (!signal_recieved)
    {
        // capture next image
        uchar3 *image = NULL;
        int status = 0;

        if (!input->Capture(&image, &status))
        {
            if (status == videoSource::TIMEOUT)
                continue;

            break; // EOS
        }

        // 运行姿势估计，如果处理失败，记录错误日志并继续处理下一帧。记录检测到的姿势数量和类别。
        std::vector<poseNet::ObjectPose> poses;
        if (!net->Process(image, input->GetWidth(), input->GetHeight(), poses, overlayFlags))
        {
            LogError("posenet: failed to process frame\n");
            continue;
        }

        // 将检测到的关键点写入CSV文件
        for (const auto &pose : poses)
        {
            for (const auto &keypoint : pose.Keypoints)
            {
                keypointsFile << frame << "," << pose.ID << "," << keypoint.ID << "," << keypoint.x << "," << keypoint.y << "\n";
            }
        }

        frame++; // 增加帧编号

        LogInfo("posenet: detected %zu %s(s)\n", poses.size(), net->GetCategory());

        // 渲染输出图像，更新状态栏显示TensorRT版本、网络精度和FPS。如果用户停止了输出流，跳出循环。打印网络处理时间的性能信息。
        if (output != NULL)
        {
            output->Render(image, input->GetWidth(), input->GetHeight());

            // update status bar
            char str[256];
            sprintf(str, "TensorRT %i.%i.%i | %s | Network %.0f FPS", NV_TENSORRT_MAJOR, NV_TENSORRT_MINOR, NV_TENSORRT_PATCH, precisionTypeToStr(net->GetPrecision()), net->GetNetworkFPS());
            output->SetStatus(str);

            // check if the user quit
            if (!output->IsStreaming())
                break;
        }

        // print out timing info
        net->PrintProfilerTimes();
    }

    keypointsFile.close(); // 关闭文件

    /*
     * destroy resources
     */
    LogVerbose("posenet: shutting down...\n");

    SAFE_DELETE(input);
    SAFE_DELETE(output);
    SAFE_DELETE(net);

    LogVerbose("posenet: shutdown complete.\n");
    return 0;
}