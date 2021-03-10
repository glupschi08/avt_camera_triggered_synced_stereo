/*============================================================
    Created by Hui Xiao - University of Connecticuit - 2018
    hui.xiao@uconn.edu
==============================================================*/

#include "avt_camera_streaming/MessagePublisher2.h"


void MessagePublisher::PublishImage(cv::Mat &image, unsigned long long ts_cam)
{
    msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", image).toImageMsg();

    msg->header.stamp = ros::Time().fromNSec(ts_cam); //old
    //msg->header.stamp = ros::Time::now(); //orig
    img_pub.publish(msg);
}