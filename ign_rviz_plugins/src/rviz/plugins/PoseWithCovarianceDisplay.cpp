// Copyright (c) 2022 Open Source Robotics Foundation, Inc.
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

#include "ignition/rviz/plugins/PoseWithCovarianceDisplay.hpp"
#include "ignition/rviz/plugins/CovarianceVisual.hpp"

#include <ignition/gui/Application.hh>
#include <ignition/gui/GuiEvents.hh>
#include <ignition/math/Pose3.hh>
#include <ignition/math/Quaternion.hh>
#include <ignition/plugin/Register.hh>

#include <string>
#include <utility>
#include <memory>
#include <vector>

namespace ignition
{
namespace rviz
{
namespace plugins
{
////////////////////////////////////////////////////////////////////////////////
PoseWithCovarianceDisplay::PoseWithCovarianceDisplay()
: MessageDisplay(), visualShape(true), dirty(true)
{
  // Get reference to scene
  this->engine = ignition::rendering::engine("ogre");
  this->scene = this->engine->SceneByName("scene");

  this->rootVisual = this->scene->CreateVisual();
  this->scene->RootVisual()->AddChild(this->rootVisual);

  this->arrow.mat = this->scene->CreateMaterial();
  this->arrow.mat->SetAmbient(1.0, 0.098, 0.0);
  this->arrow.mat->SetDiffuse(1.0, 0.098, 0.0);
  this->arrow.mat->SetEmissive(1.0, 0.098, 0.0);

  CovarianceUserData covUserData;
  covUserData.visible = true;
  covUserData.position_visible = true;
  covUserData.position_frame = Frame::Local;
  covUserData.position_color = ignition::math::Color(0.8, 0.2, 0.8, 0.3);
  covUserData.position_scale = 1.0;
  covUserData.orientation_visible = true;
  covUserData.orientation_frame = Frame::Local;
  covUserData.orientation_color_style = ColorStyle::Unique;
  covUserData.orientation_color = ignition::math::Color(1.0, 1.0, 0.5, 0.3);
  covUserData.orientation_offset = 1.0;
  covUserData.orientation_scale = 1.0;

  this->covVisual = std::make_shared<CovarianceVisual>(this->rootVisual, covUserData);
}

////////////////////////////////////////////////////////////////////////////////
PoseWithCovarianceDisplay::~PoseWithCovarianceDisplay()
{
  std::lock_guard<std::mutex>(this->lock);
  // Delete visual
  ignition::gui::App()->findChild<ignition::gui::MainWindow *>()->removeEventFilter(this);
  this->scene->DestroyVisual(this->rootVisual, true);
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::initialize(rclcpp::Node::SharedPtr _node)
{
  std::lock_guard<std::mutex>(this->lock);
  this->node = std::move(_node);
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::subscribe()
{
  std::lock_guard<std::mutex>(this->lock);

  this->subscriber = this->node->create_subscription<geometry_msgs::msg::PoseWithCovarianceStamped>(
    this->topic_name,
    this->qos,
    std::bind(&PoseWithCovarianceDisplay::callback, this, std::placeholders::_1));
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::setTopic(const std::string & topic_name)
{
  std::lock_guard<std::mutex>(this->lock);
  this->topic_name = topic_name;

  this->subscribe();

  // Refresh combo-box on plugin load
  this->onRefresh();
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::setTopic(const QString & topic_name)
{
  std::lock_guard<std::mutex>(this->lock);
  this->topic_name = topic_name.toStdString();

  // Destroy previous subscription
  this->unsubscribe();
  // Reset visualization
  this->reset();
  // Create new subscription
  this->subscribe();
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::callback(const geometry_msgs::msg::PoseWithCovarianceStamped::SharedPtr _msg)
{
  std::lock_guard<std::mutex>(this->lock);
  this->msg = std::move(_msg);
}

////////////////////////////////////////////////////////////////////////////////
bool PoseWithCovarianceDisplay::eventFilter(QObject * _object, QEvent * _event)
{
  if (_event->type() == gui::events::Render::kType) {
    update();
  }

  return QObject::eventFilter(_object, _event);
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::reset()
{
  if (this->arrow.visual != nullptr)
    this->arrow.visual->SetLocalPose(math::Pose3d::Zero);
  if (this->axis.visual != nullptr)
    this->axis.visual->SetLocalPose(math::Pose3d::Zero);
  this->msg.reset();
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::update()
{
  std::lock_guard<std::mutex>(this->lock);
  // Create axis
  if (this->axis.visual == nullptr) {
    this->axis.visual = this->scene->CreateAxisVisual();
    this->rootVisual->AddChild(this->axis.visual);
  }

  // Create arrow
  if (this->arrow.visual == nullptr) {
    this->arrow.visual = this->scene->CreateArrowVisual();
    this->arrow.visual->SetMaterial(this->arrow.mat);
    this->rootVisual->AddChild(this->arrow.visual);
  }

  if (this->dirty) {
    // Update Arrow
    this->arrow.visual->SetVisible(this->visualShape);
    this->arrow.updateVisual();

    // Update Axis
    this->axis.visual->SetVisible(!this->visualShape);
    this->axis.visual->ShowAxisHead(!this->visualShape && this->axis.headVisible);
    this->axis.updateVisual();

    this->dirty = false;
  }

  if (!this->msg) {
    return;
  }

  math::Pose3d pose;
  bool poseAvailable = this->frameManager->getFramePose(this->msg->header.frame_id, pose);

  if (!poseAvailable) {
    RCLCPP_ERROR(
      this->node->get_logger(), "Unable to get frame pose: %s",
      this->msg->header.frame_id.c_str());
    return;
  }

  this->rootVisual->SetLocalPose(pose);

  math::Pose3d localPose(this->msg->pose.pose.position.x, this->msg->pose.pose.position.y,
    this->msg->pose.pose.position.z, this->msg->pose.pose.orientation.w, this->msg->pose.pose.orientation.x,
    this->msg->pose.pose.orientation.y, this->msg->pose.pose.orientation.z);

  this->axis.visual->SetLocalPose(localPose);
  this->covVisual->setPose(localPose);

  this->arrow.visual->SetLocalPosition(localPose.Pos());
  this->arrow.visual->SetLocalRotation(localPose.Rot() * math::Quaterniond(0, 1.57, 0));
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::setShape(const bool & _shape)
{
  std::lock_guard<std::mutex>(this->lock);
  this->visualShape = _shape;
  this->dirty = true;
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::setAxisHeadVisibility(const bool & _visible)
{
  std::lock_guard<std::mutex>(this->lock);
  this->axis.headVisible = _visible;
  this->dirty = true;
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::setAxisDimensions(const float & _length, const float & _radius)
{
  std::lock_guard<std::mutex>(this->lock);
  this->axis.length = _length;
  this->axis.radius = _radius;
  this->dirty = true;
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::setArrowDimensions(
  const float & _shaftLength, const float & _shaftRadius,
  const float & _headLength, const float & _headRadius)
{
  std::lock_guard<std::mutex>(this->lock);
  this->arrow.shaftLength = _shaftLength;
  this->arrow.shaftRadius = _shaftRadius;
  this->arrow.headLength = _headLength;
  this->arrow.headRadius = _headRadius;
  this->dirty = true;
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::setColor(const QColor & _color)
{
  std::lock_guard<std::mutex>(this->lock);
  this->arrow.mat->SetAmbient(_color.redF(), _color.greenF(), _color.blueF(), _color.alphaF());
  this->arrow.mat->SetDiffuse(_color.redF(), _color.greenF(), _color.blueF(), _color.alphaF());
  this->arrow.mat->SetEmissive(_color.redF(), _color.greenF(), _color.blueF(), _color.alphaF());

  if (this->arrow.visual != nullptr)
    this->arrow.visual->SetMaterial(this->arrow.mat);
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::setFrameManager(std::shared_ptr<common::FrameManager> _frameManager)
{
  std::lock_guard<std::mutex>(this->lock);
  this->frameManager = std::move(_frameManager);
}

////////////////////////////////////////////////////////////////////////////////
QStringList PoseWithCovarianceDisplay::getTopicList() const
{
  return this->topicList;
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::onRefresh()
{
  std::lock_guard<std::mutex>(this->lock);

  // Clear
  this->topicList.clear();

  int index = 0, position = 0;

  // Get topic list
  auto topics = this->node->get_topic_names_and_types();
  for (const auto & topic : topics) {
    for (const auto & topicType : topic.second) {
      if (topicType == "geometry_msgs/msg/PoseWithCovarianceStamped") {
        this->topicList.push_back(QString::fromStdString(topic.first));
        if (topic.first == this->topic_name) {
          position = index;
        }
        index++;
      }
    }
  }
  // Update combo-box
  this->topicListChanged();
  emit setCurrentIndex(position);
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::updateQoS(
  const int & _depth, const int & _history, const int & _reliability,
  const int & _durability)
{
  std::lock_guard<std::mutex>(this->lock);
  this->setHistoryDepth(_depth);
  this->setHistoryPolicy(_history);
  this->setReliabilityPolicy(_reliability);
  this->setDurabilityPolicy(_durability);

  // Resubscribe with updated QoS profile
  this->unsubscribe();
  this->reset();
  this->subscribe();
}

////////////////////////////////////////////////////////////////////////////////
void PoseWithCovarianceDisplay::LoadConfig(const tinyxml2::XMLElement * /*_pluginElem*/)
{
  if (this->title.empty()) {
    this->title = "PoseWithCovariance";
  }
}

}  // namespace plugins
}  // namespace rviz
}  // namespace ignition

IGNITION_ADD_PLUGIN(
  ignition::rviz::plugins::PoseWithCovarianceDisplay,
  ignition::gui::Plugin)
