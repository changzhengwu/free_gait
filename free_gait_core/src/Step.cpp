/*
 * Step.cpp
 *
 *  Created on: Jan 20, 2015
 *      Author: Péter Fankhauser
 *   Institute: ETH Zurich, Autonomous Systems Lab
 */
#include "free_gait_core/Step.hpp"

// Roco
#include <roco/log/log_messages.hpp>

// Loco
#include "loco/utils/math.hpp"

namespace free_gait {

Step::Step(std::shared_ptr<loco::LegGroup> legs, std::shared_ptr<loco::TorsoBase> torso)
    : legs_(legs),
      torso_(torso),
      stepNumber_(0),
      time_(0.0),
      state_(Step::State::Undefined),
      previousState_(Step::State::Undefined),
      previousStatus_(false),
      isComplete_(false),
      totalDuration_(NAN),
      atStepDuration_(NAN),
      isDurationComputed_(false)
{

}

Step::~Step()
{
}

void Step::addSimpleStep(const int stepNumber, const std::string& legName,
                       const loco::Position& target)
{
  stepNumber_ = stepNumber;
  SwingData swingData;
  swingData.setName(legName);
  SwingProfile profile;
  profile.setTarget(target);
  swingData.setTrajectory(profile);
  swingData.setUseProfile(true);
  swingData_.insert(std::pair<std::string, SwingData>(legName, swingData));
}

void Step::setStepNumber(const int stepNumber)
{
  stepNumber_ = stepNumber;
}

void Step::addSwingData(const std::string& legName, const SwingData& data)
{
  swingData_.insert(std::pair<std::string, SwingData>(legName, data));
}

void Step::addBaseShiftData(const Step::State& state, const BaseShiftData& data)
{
  baseShiftData_.insert(std::pair<Step::State, BaseShiftData>(state, data));
}

bool Step::isComplete() const
{
  return isComplete_;
}

bool Step::advance(double dt)
{
  if (!isComplete())
    return false;  // TODO This is not ok.

  bool status = checkStatus();
  if (!status) {
    ROCO_WARN_THROTTLE_STREAM(1.0, "Not continuing step.");
    return true;
  }

  if (status && !previousStatus_) {
    ROCO_INFO_STREAM("Continuing with step.");
  }

  previousStatus_ = status;
  previousState_ = state_;

  time_ += dt;
  if (time_ >= getTotalDuration())
    return false;
  if (time_ >= getPreStepDuration() + getAtStepDuration()) {
    state_ = Step::State::PostStep;
  } else if (time_ >= getPreStepDuration()) {
    state_ = Step::State::AtStep;
  } else {
    state_ = Step::State::PreStep;
  }

  return true;
}

bool Step::checkStatus()
{
  for (auto leg : *legs_) {
    if (leg->shouldBeGrounded()) {
      if (!leg->isSupportLeg()) {
        ROCO_WARN_THROTTLE_STREAM(
            1.0, "Leg " << leg->getName() << " should be grounded but it is not.");
        return false;
      }
    }
  }
  return true;
}

unsigned int Step::getStepNumber() const
{
  return stepNumber_;
}

const Step::State& Step::getState() const
{
  return state_;
}

bool Step::hasSwitchedState() const
{
  return (previousState_ != state_);
}

std::unordered_map<std::string, SwingData>& Step::getSwingData()
{
  return swingData_;
}

BaseShiftData& Step::getCurrentBaseShiftData()
{
  if (!hasBaseShiftData(state_)) throw std::out_of_range("No base shift data for current state!");
  return baseShiftData_.at(state_);
}

std::map<Step::State, BaseShiftData>& Step::getBaseShiftData()
{
  return baseShiftData_;
}

double Step::getTime() const
{
  return time_;
}

bool Step::hasSwingData() const
{
  return !swingData_.empty();
}

bool Step::hasSwingData(const std::string& legName) const
{
  return !(swingData_.find(legName) == swingData_.end());
}

bool Step::hasBaseShiftData(const Step::State& state) const
{
  return !(baseShiftData_.find(state) == baseShiftData_.end());
}

double Step::getStateDuration()
{
  switch (getState()) {
    case Step::State::PreStep:
      return getPreStepDuration();
    case Step::State::AtStep:
      return getAtStepDuration();
    case Step::State::PostStep:
      return getPostStepDuration();
    default:
      return 0.0;
  }
}

double Step::getStateTime()
{
  return getStateTime(getState());
}

double Step::getStateTime(const Step::State& state)
{
  switch (state) {
    case Step::State::PreStep:
      return getTime();
    case Step::State::AtStep:
      return getTime() - getPreStepDuration();
    case Step::State::PostStep:
      return getTime() - getPreStepDuration() - getAtStepDuration();
    default:
      return 0.0;
  }
}

double Step::getStatePhase()
{
  switch (getState()) {
    case Step::State::PreStep:
      return getPreStepPhase();
    case Step::State::AtStep:
      return getAtStepPhase();
    case Step::State::PostStep:
      return getPostStepPhase();
    default:
      return 0.0;
  }
}

// TODO Make this more pretty.

double Step::getPreStepDuration() const
{
  return hasBaseShiftData(State::PreStep) ? baseShiftData_.at(State::PreStep).getTrajectory().getDuration() : 0.0;
}

double Step::getPreStepPhase() const
{
  return loco::mapTo01Range(getTime(), 0.0, getPreStepDuration());
}

double Step::getAtStepDuration()
{
  if (!isDurationComputed_)
    computeDurations();
  return atStepDuration_;
}

double Step::getAtStepDuration(const std::string& legName) const
{
  if (!hasSwingData(legName)) return 0.0;
  return swingData_.at(legName).getTrajectory().getDuration();
}

double Step::getAtStepPhase()
{
  return loco::mapTo01Range(getTime() - getPreStepDuration(), 0.0, getAtStepDuration());
}

double Step::getAtStepPhase(const std::string& legName)
{
  return loco::mapTo01Range(getTime() - getPreStepDuration(), 0.0, getAtStepDuration(legName));
}

double Step::getPostStepDuration() const
{
  return hasBaseShiftData(State::PostStep) ? baseShiftData_.at(State::PostStep).getTrajectory().getDuration() : 0.0;
}

double Step::getPostStepPhase()
{
  return loco::mapTo01Range(getTime() - getPreStepDuration() - getAtStepDuration(), 0.0,
                      getPostStepDuration());
}

double Step::getTotalDuration()
{
  if (!isDurationComputed_)
    computeDurations();
  return totalDuration_;
}

double Step::getTotalPhase()
{
  return loco::mapTo01Range(getTime(), 0.0, getTotalDuration());
}

bool Step::computeDurations()
{
  if (!isComplete())
    return false;

  double maxAtStepDuration = 0.0;
  for (const auto& swingData : swingData_) {
    if (swingData.second.getTrajectory().getDuration() > maxAtStepDuration)
      maxAtStepDuration = swingData.second.getTrajectory().getDuration();
  }
  if (hasBaseShiftData(State::AtStep)) {
    if (baseShiftData_.at(State::AtStep).getTrajectory()->getDuration() > maxAtStepDuration)
      maxAtStepDuration = baseShiftData_.at(State::AtStep).getTrajectory()->getDuration();
  }
  atStepDuration_ = maxAtStepDuration;
  totalDuration_ = getPreStepDuration() + atStepDuration_ + getPostStepDuration();
  return isDurationComputed_ = true;
}

std::ostream& operator<<(std::ostream& out, const Step& step)
{
  out << "Step number: " << step.stepNumber_ << ", " << step.state_ << std::endl;
  out << "Swing data: " << std::endl;
  for (const auto& swingData : step.swingData_) out << swingData.second << std::endl;
  out << "Base shift data: " << std::endl;
  for (const auto& baseShiftData : step.baseShiftData_) out << baseShiftData.second << std::endl;
  return out;
}

Step::State& operator++(Step::State& phase)
{
  switch (phase) {
    case Step::State::PreStep:
      return phase = Step::State::AtStep;
    case Step::State::AtStep:
      return phase = Step::State::PostStep;
    case Step::State::PostStep:
      return phase = Step::State::PostStep;
  }
}

std::ostream& operator<<(std::ostream& os, const Step::State& phase)
{
  switch (phase) {
    case Step::State::Undefined:
          os << "State::Undefined";
          return os;
    case Step::State::PreStep:
      os << "State::PreStep";
      return os;
    case Step::State::AtStep:
      os << "State::AtStep";
      return os;
    case Step::State::PostStep:
      os << "State::PostStep";
      return os;
  }
}

} /* namespace loco */
