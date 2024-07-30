// Copyright (c) 2024，D-Robotics.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "include/hobot_falldown_detection/hobot_falldown_detection.h"
#include <string>
#include <vector>
#include <memory>
#include <cmath>

hobot_falldown_detection::hobot_falldown_detection(
    const NodeOptions &options, std::string node_name)
    : Node(node_name, options)
{
    this->declare_parameter<int>("paramSensivity", paramSensivity);
    this->declare_parameter<std::string>("body_kps_topic_name",
                                            body_kps_topic_name);
    this->declare_parameter<std::string>("pub_smart_topic_name",
                                            msg_falldown_topic_name);

    this->get_parameter<int>("paramSensivity", paramSensivity);
    this->get_parameter<std::string>("body_kps_topic_name",
                                        body_kps_topic_name);
    this->get_parameter<std::string>("pub_smart_topic_name",
                                            msg_falldown_topic_name);

    if (paramSensivity == Sensivity::High)
    {
        upper_body_low_ = 48.0f;
        upper_body_high_ = 70.0f;
        lower_body_low_ = 50.0f;
        lower_body_high_ = 78.0f;
        differ_ = 33.0f;
    } else if (paramSensivity == Sensivity::Middle) {
        upper_body_low_ = 35.0f;
        upper_body_high_ = 66.0f;
        lower_body_low_ = 45.0f;
        lower_body_high_ = 75.0f;
        differ_ = 25.0f;
    } else if (paramSensivity == Sensivity::Low) {
        upper_body_low_ = 35.0f;
        upper_body_high_ = 60.0f;
        lower_body_low_ = 40.0f;
        lower_body_high_ = 70.0f;
        differ_ = 25.0f;
    } else if (paramSensivity == Sensivity::ExLow) {
        upper_body_low_ = 30.0f;
        upper_body_high_ = 60.0f;
        lower_body_low_ = 40.0f;
        lower_body_high_ = 70.0f;
        differ_ = 20.0f;
    }

    std::stringstream ss;
    ss << "Parameter:" << "\n paramSensivity: "
    << paramSensivity << "\n topic name: "
    << body_kps_topic_name << "\n pub_smart_topic_name: "
    << msg_falldown_topic_name;
    RCLCPP_WARN(rclcpp::get_logger("example"), "%s", ss.str().c_str());

    RCLCPP_WARN(rclcpp::get_logger("body_kps_Subscriber"),
        "Create subscription with topic_name: %s",
        body_kps_topic_name.data());

    subscription_ = this->create_subscription<ai_msgs::msg::PerceptionTargets>(
        body_kps_topic_name, 10,
        std::bind(&hobot_falldown_detection::topic_callback,
        this, std::placeholders::_1));

    RCLCPP_WARN(rclcpp::get_logger("fall_down_publisher"),
                "msg_falldown_topic_name: %s",
                msg_falldown_topic_name.data());

    falldown_publisher_ =
        this->create_publisher<ai_msgs::msg::PerceptionTargets>(
            msg_falldown_topic_name, 10);
}

hobot_falldown_detection::~hobot_falldown_detection() {}

void hobot_falldown_detection::topic_callback(
    const ai_msgs::msg::PerceptionTargets::ConstSharedPtr msg)
{
    struct timespec time_start = {0, 0};
    clock_gettime(CLOCK_REALTIME, &time_start);
    ai_msgs::msg::Perf perf;
    perf.set__type("PostProcess");
    perf.stamp_start.sec = time_start.tv_sec;
    perf.stamp_start.nanosec = time_start.tv_nsec;

    auto targetList = msg->targets;
    ai_msgs::msg::PerceptionTargets::UniquePtr
            publish_data(new ai_msgs::msg::PerceptionTargets());

    for (auto &target : targetList)
    {
        auto targetType = target.type;
        auto pointList = target.points;
        auto track_id = target.track_id;
        bool isfalldown = false;
        bool has_body_kps = false;
        for (auto &pointNode : pointList)
        {
            auto pointType = pointNode.type;
            std::stringstream ss1;
            ss1 << "targetType: " << targetType << "pointType: " << pointType;
            RCLCPP_INFO(rclcpp::get_logger("body_kps_Subscriber"),
                            "receive %s", ss1.str().c_str());
            if (PointTypeBody_kps != pointType)
            {
                isfalldown = false;
            } else {
                has_body_kps = true;
                auto point32List = pointNode.point;
                isfalldown = IsFallDown(point32List);
            }
        }
        ai_msgs::msg::Target pub_target;
        pub_target.set__type(targetType);
        pub_target.set__track_id(track_id);

        if (has_body_kps) {
            ai_msgs::msg::Attribute attribute;
            attribute.set__type("falldown");
            if (isfalldown) {
                attribute.set__value(1);
                std::stringstream ss;
                ss << "track_id: " << track_id << " is fall down";
                RCLCPP_INFO(rclcpp::get_logger("fall_down_publisher"),
                        "%s", ss.str().c_str());
            } else {
                attribute.set__value(0);
                std::stringstream ss;
                ss << "track_id: " << track_id << " is not fall down";
                RCLCPP_INFO(rclcpp::get_logger("fall_down_publisher"),
                        "%s", ss.str().c_str());
            }
            pub_target.attributes.emplace_back(std::move(attribute));
        }

        pub_target.rois = target.rois;
        pub_target.points = pointList;
        publish_data->targets.emplace_back(std::move(pub_target));
    }
    PublishFallDownEvent(msg, std::move(publish_data), perf);
}

void hobot_falldown_detection::PublishFallDownEvent(
        const ai_msgs::msg::PerceptionTargets::ConstSharedPtr msg,
        ai_msgs::msg::PerceptionTargets::UniquePtr publish_data,
        ai_msgs::msg::Perf perf)
{
    struct timespec time_start = {0, 0};
    clock_gettime(CLOCK_REALTIME, &time_start);
    perf.stamp_end.sec = time_start.tv_sec;
    perf.stamp_end.nanosec = time_start.tv_nsec;

    publish_data->header.set__stamp(msg->header.stamp);
    publish_data->header.set__frame_id(msg->header.frame_id);
    publish_data->set__fps(msg->fps);
    publish_data->perfs.emplace_back(perf);
    falldown_publisher_->publish(std::move(publish_data));
}

bool hobot_falldown_detection::IsFallDown(
    const std::vector<geometry_msgs::msg::Point32> &body_kps)
{
    if (body_kps.size() != BodyKpsSize)
    {
        RCLCPP_WARN(rclcpp::get_logger("body_kps_Subscriber"),
                    "body_kps size: ", body_kps.size());
        return false;
    }
    PointDebugInfo(body_kps);

    auto center_56_x = (body_kps[5].x + body_kps[6].x) / 2;
    auto center_56_y = (body_kps[5].y + body_kps[6].y) / 2;
    geometry_msgs::msg::Point32 center_56;
    center_56.set__x(center_56_x);
    center_56.set__y(center_56_y);

    auto center_1112_x = (body_kps[11].x + body_kps[12].x) / 2;
    auto center_1112_y = (body_kps[11].y + body_kps[12].y) / 2;
    geometry_msgs::msg::Point32 center_1112;
    center_1112.set__x(center_1112_x);
    center_1112.set__y(center_1112_y);

    auto a = center_56.y - center_1112.y;
    auto b = center_56.x - center_1112.x;

    if (b != 0) {
        float tan = a / b;
        auto arctan = std::atan(std::abs(tan)) * 180.0f / PI;

        auto center_1516_x = (body_kps[15].x + body_kps[16].x) / 2;
        auto center_1516_y = (body_kps[15].y + body_kps[16].y) / 2;
        geometry_msgs::msg::Point32 center_1516;
        center_1516.set__x(center_1516_x);
        center_1516.set__y(center_1516_y);

        a = center_1516.y - center_1112.y;
        b = center_1516.x - center_1112.x;

        if (b != 0)
        {
            tan = a / b;
            auto before = arctan;
            arctan = std::atan(std::abs(tan)) * 180.0f / PI;

            auto differ = std::abs(arctan - before);
            if (differ > differ_)
            {
                return false;
            }

            if ((before <= upper_body_low_ && arctan <= lower_body_high_) ||
                (arctan <= lower_body_low_ && before <= upper_body_high_))
            {
                return true;
            }
        }
    }

    return false;
}

void hobot_falldown_detection::PointDebugInfo(
    const std::vector<geometry_msgs::msg::Point32> &body_kps)
{
    std::stringstream ss;
    ss << "body_kps:" <<
    "\n kps[0].x: " << body_kps[0].x << " kps[0].y: " << body_kps[0].y <<
    "\n kps[1].x: " << body_kps[1].x << " kps[1].y: " << body_kps[1].y <<
    "\n kps[2].x: " << body_kps[2].x << " kps[2].y: " << body_kps[2].y <<
    "\n kps[3].x: " << body_kps[3].x << " kps[3].y: " << body_kps[3].y <<
    "\n kps[4].x: " << body_kps[4].x << " kps[4].y: " << body_kps[4].y <<
    "\n kps[5].x: " << body_kps[5].x << " kps[5].y: " << body_kps[5].y <<
    "\n kps[6].x: " << body_kps[6].x << " kps[6].y: " << body_kps[6].y <<
    "\n kps[7].x: " << body_kps[7].x << " kps[7].y: " << body_kps[7].y <<
    "\n kps[8].x: " << body_kps[8].x << " kps[8].y: " << body_kps[8].y <<
    "\n kps[9].x: " << body_kps[9].x << " kps[9].y: " << body_kps[9].y <<
    "\n kps[10].x: " << body_kps[10].x << " kps[10].y: " << body_kps[10].y <<
    "\n kps[11].x: " << body_kps[11].x << " kps[11].y: " << body_kps[11].y <<
    "\n kps[12].x: " << body_kps[12].x << " kps[12].y: " << body_kps[12].y <<
    "\n kps[13].x: " << body_kps[13].x << " kps[13].y: " << body_kps[13].y <<
    "\n kps[14].x: " << body_kps[14].x << " kps[14].y: " << body_kps[14].y <<
    "\n kps[15].x: " << body_kps[15].x << " kps[15].y: " << body_kps[15].y <<
    "\n kps[16].x: " << body_kps[16].x << " kps[16].y: " << body_kps[16].y <<
    "\n kps[17].x: " << body_kps[17].x << " kps[17].y: " << body_kps[17].y <<
    "\n kps[18].x: " << body_kps[18].x << " kps[18].y: " << body_kps[18].y;

    RCLCPP_DEBUG(rclcpp::get_logger("body_kps_Subscriber"),
                    "%s", ss.str().c_str());
}
