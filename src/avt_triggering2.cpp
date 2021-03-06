/*=========================================================
Created by Hui Xiao - University of Connecticuit - 2019
hui.xiao@uconn.edu
===========================================================*/

#include <iostream>
#include <sstream>
#include <cstring>
#include "VimbaCPP/Include/VimbaCPP.h"
#include "ros/ros.h"
#include "ros/console.h"
#include "string.h"
#include "Common/StreamSystemInfo.h"
#include "Common/ErrorCodeToMessage.h"
#include "avt_camera_streaming/CamParam.h"
#include "avt_camera_streaming/MessagePublisher2.h"
#include "std_msgs/String.h"

/*From Vimba C++ manual: To assure correct continuous image capture, use at least two or three frames. The appropriate number of frames to be queued in your application depends on the frames per second the camera 
delivers and on the speed with which you are able to re-queue frames (also taking into consideration the 
operating system load). The image frames are filled in the same order in which they were queued.*/
#define NUM_OF_FRAMES 1    //orig 3

//define observer that reacts on new frames
class FrameObserver : public AVT::VmbAPI::IFrameObserver
{
public:
    // In contructor call the constructor of the base class
    // and pass a camera object
    FrameObserver( AVT::VmbAPI::CameraPtr pCamera, MessagePublisher& imgPublisher) : IFrameObserver( pCamera ), pImagePublisher(&imgPublisher)
    {
        
    }
    void FrameReceived( const AVT::VmbAPI::FramePtr pFrame )
    {
        VmbUchar_t *pImage = NULL; // frame data will be put here to be converted to cv::Mat
        VmbFrameStatusType eReceiveStatus ;
        if( VmbErrorSuccess == pFrame->GetReceiveStatus( eReceiveStatus ) )
        {
            if ( VmbFrameStatusComplete == eReceiveStatus )
            {
                //  successfully received frame               
                if (VmbErrorSuccess == pFrame->GetImage(pImage))
			    {
                    //own part
                    unsigned long long ts_cam;
                    static unsigned long long ts_cam_previous;
                    static unsigned long long prev_ros_time;

                    ros::Time ros_time = ros::Time::now();
                    pFrame->GetTimestamp(ts_cam);
                    std::cout << "ROS Time: " << ros_time.toNSec() << " Vimba time: " << ts_cam << " -- time diff: " << ros_time.toNSec()-ts_cam << " Vim_time2prev " << ts_cam-ts_cam_previous << " ROS_time2prev " << ros_time.toNSec()-prev_ros_time << std::endl; // check here how ts_cam is written. I suppose ts_cam is in nanoseconds because it is an integer instead of a double
                    prev_ros_time=ros_time.toNSec();
                    ts_cam_previous=ts_cam;

                    VmbUint32_t width=688;	//1600
                    VmbUint32_t height=512;   //1200
                    pFrame->GetHeight(height);
                    pFrame->GetWidth(width);
                    //ROS_INFO("received an image");
                    cv::Mat image = cv::Mat(height,width, CV_8UC1, pImage);
                    cv::cvtColor(image, image, cv::COLOR_BayerBG2RGB);
                    m_pCamera->QueueFrame(pFrame);   // I can queue frame here because image is already transformed.
                    pImagePublisher->PublishImage(image, ts_cam); //without ts_cam
                }
            }
            else
            {
                // unsuccessfully received frame
                ROS_INFO("receiving frame failed.");
            }
        }
        // finished copying the frame , re - queue it
        m_pCamera->QueueFrame( pFrame );
    }
private:
    MessagePublisher *pImagePublisher;  // class pointer, will point to the MessagePublisher when initializing
};

class AVTCamera
{
public:
    AVTCamera() : sys(AVT::VmbAPI::VimbaSystem::GetInstance()), frames(NUM_OF_FRAMES), n("~")
    {
        
        getParams(n, cam_param);
        sub = nn.subscribe("trigger", 1, &AVTCamera::triggerCb, this);
    }

    void StartAcquisition();
    void StopAcquisition();
    void SetCameraFeature();
    //call this function triggers an image
    void TriggerImage(); 
private:
    // camera trigger call back
    void triggerCb(const std_msgs::String::ConstPtr& msg);
    // this function fetch parameters from ROS server
    void getParams(ros::NodeHandle &n, CameraParam &cp);
    void SetCameraImageSize(const VmbInt64_t& width, const VmbInt64_t& height, const VmbInt64_t& offsetX, const VmbInt64_t& offsetY, const VmbInt64_t& binninghorizontal, const VmbInt64_t& binningvertical);
    void SetExposureTime(const VmbInt64_t & time_in_us);
    void SetAcquisitionFramRate(const double & fps); 
    void SetGain(const VmbInt64_t & gain);

    CameraParam cam_param;
    VmbInt64_t nPLS; // Payload size value
    AVT::VmbAPI::FeaturePtr pFeature; // Generic feature pointer
    AVT::VmbAPI::VimbaSystem &sys;
    AVT::VmbAPI::CameraPtr camera;
    AVT::VmbAPI::FramePtrVector frames; // Frame array
    ros::NodeHandle n;   // this will be initialized as n("~") for accessing private parameters
    ros::NodeHandle nn;  // initialized without namespace. 
    ros::Subscriber sub; // subscriber to camera trigger signal
    MessagePublisher image_pub;  // image publisher class. Using image_transport api.
};


void AVTCamera::triggerCb(const std_msgs::String::ConstPtr& msg)
{
    TriggerImage();
}

void AVTCamera::getParams(ros::NodeHandle &n, CameraParam &cam_param)
{
    //Todo remmber own
    int binninghorizontal, binningvertical;

    int height,width,exposure,gain;
    int offsetX, offsetY;
    double fps;
    if(n.getParam("cam_IP", cam_param.cam_IP))
    {
        ROS_INFO("Got camera IP %s", cam_param.cam_IP.c_str());
    }
    else
    {
        cam_param.cam_IP = "169.254.49.41";   // dafault IP
        ROS_ERROR("failed to get param 'cam_IP' ");
    }

    if(n.getParam("image_height", height))
    {
        ROS_INFO("Got image_height %i", height);
    }
    else
    {
        height = 1200;
        ROS_ERROR("failed to get param 'image_height' ");
    }

    if(n.getParam("image_width", width))
    {
        ROS_INFO("Got image_width %i", width);
    }
    else
    {
        width = 1600;
        ROS_ERROR("failed to get param 'image_width' ");
    }

    if(n.getParam("offsetX", offsetX))
    {
        ROS_INFO("Got offsetX %i", offsetX);
    }
    else
    {
        offsetX = 0;
        ROS_ERROR("failed to get param 'offsetX' ");
    }

    if(n.getParam("offsetY", offsetY))
    {
        ROS_INFO("Got offsetY %i", offsetY);
    }
    else
    {
        offsetY = 0;
        ROS_ERROR("failed to get param 'offsetY' ");
    }

    if(n.getParam("exposure_in_us", exposure))
    {
        ROS_INFO("Got exposure_in_us %i", exposure);
    }
    else
    {
        exposure = 10000;
        ROS_ERROR("failed to get param 'exposure_in_us' ");
    }

    if(n.getParam("frame_rate", fps))
    {
        ROS_INFO("Got frame_rate %f", fps);
    }
    else
    {
        fps = 20;
        ROS_ERROR("failed to get param 'frame_rate' ");
    }

    if(n.getParam("trigger_source", cam_param.trigger_source))
    {
        ROS_INFO_STREAM("trigger source is " << cam_param.trigger_source);
    }
    else
    {
        cam_param.trigger_source = "FreeRun";
        ROS_ERROR("failed to get param 'trigger_souorce' ");
    }

    if(n.getParam("exposure_auto", cam_param.exposure_auto))
    {
        ROS_INFO("exposure_auto %s", cam_param.exposure_auto ? "enabled" : "disabled");
    }
    else
    {
        cam_param.exposure_auto = false;
        ROS_ERROR("failed to get param 'exposure_auto' ");
    }

    if(n.getParam("balance_white_auto", cam_param.balance_white_auto))
    {
        ROS_INFO("balance_white_auto %s", cam_param.balance_white_auto ? "enabled" : "disabled");
    }
    else
    {
        cam_param.balance_white_auto = false;
        ROS_ERROR("failed to get param 'balance_white_auto' ");
    }

    if(n.getParam("gain", gain))
    {
        ROS_INFO("gain is %i dB", gain);
    }
    else
    {
        gain = 0;
        ROS_ERROR("failed to get param 'gain' ");
    }

    //Todo remember own
    if(n.getParam("binninghorizontal", binninghorizontal))
    {
        ROS_INFO("Got binninghorizontal %i", binninghorizontal);
    }
    else
    {
        binninghorizontal = 1;
        ROS_ERROR("failed to get param 'binninghorizontal' ");
    }
    if(n.getParam("binningvertical", binningvertical))
    {
        ROS_INFO("Got binningvertical %i", binningvertical);
    }
    else
    {
        binningvertical = 1;
        ROS_ERROR("failed to get param 'binningvertical' ");
    }
    if(n.getParam("ptp_mode", cam_param.ptp_mode))
    {
        ROS_INFO_STREAM("ptp_mode is " << cam_param.ptp_mode);
    }
    else
    {
        cam_param.ptp_mode = "Off";
        ROS_ERROR("failed to get param 'ptp_mode' ");
    }
    cam_param.binninghorizontal = binninghorizontal;
    cam_param.binningvertical = binningvertical;

    cam_param.image_height = height;
    cam_param.image_width = width;
    cam_param.exposure_in_us = exposure;
    cam_param.frame_rate = fps;
    cam_param.gain = gain;
    cam_param.offsetX = offsetX;
    cam_param.offsetY = offsetY;
}

void AVTCamera::StartAcquisition()
{
    sys.Startup();    
    VmbErrorType res = sys.OpenCameraByID( cam_param.cam_IP.c_str(), VmbAccessModeFull, camera );
    if (VmbErrorSuccess != res)
    {
        ROS_ERROR("failed to open the camera");
    }
    else
    {
        SetCameraFeature();
        camera->GetFeatureByName("PayloadSize", pFeature );
        pFeature->GetValue(nPLS );
        
        for( AVT::VmbAPI::FramePtrVector::iterator iter= frames.begin(); frames.end()!= iter; ++iter)
        {
            (*iter).reset(new AVT::VmbAPI::Frame(nPLS ));
            (*iter)->RegisterObserver(AVT::VmbAPI::IFrameObserverPtr(new FrameObserver(camera,image_pub)));
            camera->AnnounceFrame(*iter );
        }
        
        // Start the capture engine (API)
        camera->StartCapture();
        for( AVT::VmbAPI::FramePtrVector::iterator iter= frames.begin(); frames.end()!=iter; ++iter)
        {
            // Put frame into the frame queue
            camera->QueueFrame(*iter );
        }
        // Start the acquisition engine ( camera )
        camera->GetFeatureByName("AcquisitionStart", pFeature );
        pFeature->RunCommand();
    }
}

void AVTCamera::StopAcquisition()
{
    camera->GetFeatureByName("AcquisitionStop", pFeature );
    pFeature->RunCommand();
    // Stop the capture engine (API)
    // Flush the frame queue
    // Revoke all frames from the API
    camera->EndCapture();
    camera->FlushQueue();
    camera->RevokeAllFrames();
    for( AVT::VmbAPI::FramePtrVector::iterator iter=frames.begin(); frames.end()!=iter; ++iter)
    {
        // Unregister the frame observer / callback
        (*iter)-> UnregisterObserver();
    }
    sys.Shutdown();
}

void AVTCamera::SetCameraImageSize(const VmbInt64_t& width, const VmbInt64_t& height, const VmbInt64_t& offsetX, const VmbInt64_t& offsetY, const VmbInt64_t& binninghorizontal, const VmbInt64_t& binningvertical)
{
	// set the offset value to centralize the image.
	//VmbInt64_t offset_x = int((1600 - width) / 2);
	//VmbInt64_t offset_y = int((1200 - height) / 2);
	VmbErrorType err;
	err = camera->GetFeatureByName("Height", pFeature);
	if (err == VmbErrorSuccess)
	{
		err = pFeature->SetValue(height);
		if (VmbErrorSuccess == err)
		{
			bool bIsCommandDone = false;
			do
			{
				if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
				{
					break;
				}
			} while (false == bIsCommandDone);
		}
		else
		{
            ROS_ERROR("failed to set height.");
		}
	}

	err = camera->GetFeatureByName("Width", pFeature);
	if (err == VmbErrorSuccess)
	{
		err = pFeature->SetValue(width);
		if (VmbErrorSuccess == err)
		{
			bool bIsCommandDone = false;
			do
			{
				if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
				{
					break;
				}
			} while (false == bIsCommandDone);
		}
		else
		{
            ROS_ERROR("failed to set width");
		}
	}

	
	err = camera->GetFeatureByName("OffsetX", pFeature);
	if (err == VmbErrorSuccess)
	{
		err = pFeature->SetValue(offsetX);
		if (VmbErrorSuccess == err)
		{
			bool bIsCommandDone = false;
			do
			{
				if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
				{
					break;
				}
			} while (false == bIsCommandDone);
		}
		else
		{
            ROS_ERROR("failed to set OffsetX");
		}
	}

	err = camera->GetFeatureByName("OffsetY", pFeature);
	if (err == VmbErrorSuccess)
	{
		err = pFeature->SetValue(offsetY);
		if (VmbErrorSuccess == err)
		{
			bool bIsCommandDone = false;
			do
			{
				if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
				{
					break;
				}
			} while (false == bIsCommandDone);
		}
		else
		{
            ROS_ERROR("failed to set OffsetY");
		}
	}


    //Todo remmeber own
    //VmbInt64_t BinningHorizontal_loc=2;
    //VmbInt64_t BinningVertical_loc=2;

    err = camera->GetFeatureByName("BinningHorizontal", pFeature);
    if (err == VmbErrorSuccess)
    {
        err = pFeature->SetValue(binninghorizontal);
        if (VmbErrorSuccess == err)
        {
            bool bIsCommandDone = false;
            do
            {
                if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
                {
                    break;
                }
            } while (false == bIsCommandDone);
        }
        else
        {
            ROS_ERROR("failed to set BinningHorizontal");
        }
    }
    err = camera->GetFeatureByName("BinningVertical", pFeature);
    if (err == VmbErrorSuccess)
    {
        err = pFeature->SetValue(binningvertical);
        if (VmbErrorSuccess == err)
        {
            bool bIsCommandDone = false;
            do
            {
                if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
                {
                    break;
                }
            } while (false == bIsCommandDone);
        }
        else
        {
            ROS_ERROR("failed to set BinningVertical");
        }
    }



}

void AVTCamera::SetExposureTime(const VmbInt64_t & time_in_us)
{
	VmbErrorType err;
	err = camera->GetFeatureByName("ExposureTimeAbs", pFeature);
	if (err == VmbErrorSuccess)
	{
		err = pFeature->SetValue((double)time_in_us); 
		if (VmbErrorSuccess == err)
		{
			bool bIsCommandDone = false;
			do
			{
				if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
				{
					break;
				}
			} while (false == bIsCommandDone);
		}
		else
		{
			std::cout << "failed to set ExposureTimeAbs." << std::endl;
			getchar();
		}
	}
}

void AVTCamera::SetAcquisitionFramRate(const double & fps)
{
    VmbErrorType err;
	err = camera->GetFeatureByName("AcquisitionFrameRateAbs", pFeature);
	if (err == VmbErrorSuccess)
	{
		err = pFeature->SetValue(fps); 
		if (VmbErrorSuccess == err)
		{
			bool bIsCommandDone = false;
			do
			{
				if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
				{
					break;
				}
			} while (false == bIsCommandDone);
		}
		else
		{
            ROS_ERROR("failed to set AcquisitionFrameRateAbs feature");
		}
	}
    else
    {
        ROS_ERROR("failed to open AcquisitionFrameRateAbs feature");
    }
}

void AVTCamera::SetGain(const VmbInt64_t & gain)
{
    VmbErrorType err;
	err = camera->GetFeatureByName("Gain", pFeature);
	if (err == VmbErrorSuccess)
	{
		err = pFeature->SetValue((double)gain); 
		if (VmbErrorSuccess == err)
		{
			bool bIsCommandDone = false;
			do
			{
				if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
				{
					break;
				}
			} while (false == bIsCommandDone);
		}
		else
		{
            ROS_ERROR("failed to set camera gain");
		}
	}
    else
    {
        ROS_ERROR("failed to open gain feature");
    }
}

void AVTCamera::TriggerImage()
{
    VmbErrorType err;
	err = camera->GetFeatureByName("TriggerSoftware", pFeature);
	if (err == VmbErrorSuccess)
	{
		err = pFeature->RunCommand();
		if (VmbErrorSuccess == err)
		{
			bool bIsCommandDone = false;
			do
			{
				if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
				{
					break;
				}
			} while (false == bIsCommandDone);
		}
	}
}

// This must be called after opening the camera.
void AVTCamera::SetCameraFeature()
{
    VmbErrorType err;
    SetExposureTime(cam_param.exposure_in_us);
    SetCameraImageSize(cam_param.image_width, cam_param.image_height, cam_param.offsetX, cam_param.offsetY, cam_param.binninghorizontal, cam_param.binningvertical);
    SetAcquisitionFramRate(cam_param.frame_rate);
    SetGain(cam_param.gain);

    // Set acquisition mode
    camera->GetFeatureByName("AcquisitionMode", pFeature);
    err = pFeature->SetValue("Continuous");
    if (VmbErrorSuccess == err)
    {
        bool bIsCommandDone = false;
        do
        {
            if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
            {
                break;
            }
        } while (false == bIsCommandDone);
    }
    else
    {
        ROS_ERROR("Failed to set acquisition mode");
    }

    //Todo remmeber own
    // Set ptp_mode
    camera->GetFeatureByName("PtpMode", pFeature);
    if(cam_param.ptp_mode == "Slave")
    {
        err = pFeature->SetValue("Slave");
    }
    else if(cam_param.ptp_mode == "Master")
    {
        err = pFeature->SetValue("Master");
    }
    else if(cam_param.ptp_mode == "Auto")
    {
        err = pFeature->SetValue("Auto");
    }
    else
    {
        err = pFeature->SetValue("Off");
        if(cam_param.ptp_mode != "Off")
        {
            ROS_ERROR("Invalid ptp_mode. Valid values are from set {Off, Slave, Master, Auto}");
        }
    }




    // Set Trigger source
    camera->GetFeatureByName("TriggerSource", pFeature);
    if(cam_param.trigger_source == "Software")
    {
        err = pFeature->SetValue("Software");
    }
    else if(cam_param.trigger_source == "FixedRate")
    {
        err = pFeature->SetValue("FixedRate");
    }
    else
    {
        err = pFeature->SetValue("Freerun");
        if(cam_param.trigger_source != "FreeRun")
        {
            ROS_ERROR("Invalid trigger source value. Valid values are from set {FixedRate, Software, FreeRun}");
        }
    }
    
    if (VmbErrorSuccess == err)
    {
        bool bIsCommandDone = false;
        do
        {
            if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
            {
                break;
            }
        } while (false == bIsCommandDone);
    }
    else
    {
        ROS_ERROR("Failed to set Trigger Source");
    }

    // Set Exposure Auto
    camera->GetFeatureByName("ExposureAuto", pFeature);
    if(cam_param.exposure_auto)
    {
        err = pFeature->SetValue("Continuous");
    }
    else
    {
        err = pFeature->SetValue("Off");
    }
    if (VmbErrorSuccess == err)
    {
        bool bIsCommandDone = false;
        do
        {
            if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
            {
                break;
            }
        } while (false == bIsCommandDone);
    }
    else
    {
        ROS_ERROR("failed to set ExposureAuto");
    }

    // Set Balance White Auto
    camera->GetFeatureByName("BalanceWhiteAuto", pFeature);
    if(cam_param.balance_white_auto)
    {
        err = pFeature->SetValue("Continuous");
    }
    else
    {
        err = pFeature->SetValue("Off");
    }
    if (VmbErrorSuccess == err)
    {
        bool bIsCommandDone = false;
        do
        {
            if (VmbErrorSuccess != pFeature->IsCommandDone(bIsCommandDone))
            {
                break;
            }
        } while (false == bIsCommandDone);
    }
    else
    {
        ROS_ERROR("failed to set BalanceWhiteAuto");
    }

}

int main( int argc, char* argv[])
{
    ros::init(argc, argv, "triggered_avt_camera2", ros::init_options::AnonymousName);
    AVTCamera avt_cam;
    avt_cam.StartAcquisition();
    ros::Rate rate(300); //todo own remove
    while(ros::ok())
    {
        ros::spinOnce();
        rate.sleep();
    }
    avt_cam.StopAcquisition();
}