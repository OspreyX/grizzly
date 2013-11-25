
#include "grizzly_motion/encoders_monitor.h"
#include "diagnostic_updater/publisher.h"

/**
 * Separate ROS initialization step for better testability.
 */
EncodersMonitor::EncodersMonitor(ros::NodeHandle* nh)
  : encoders_timeout(0.11),
    encoder_speed_error_diff_threshold(0.5),
    encoder_fault_time_to_failure(0.5),
    failed_encoder_(-1)
{
  sub_encoders_ = nh->subscribe("motors/encoders", 1, &EncodersMonitor::encodersCallback, this);
  sub_drive_ = nh->subscribe("safe_cmd_drive", 1, &EncodersMonitor::driveCallback, this); 

  double encoders_timeout_seconds;
  ros::param::param<double>("~encoders_timeout", encoders_timeout_seconds, encoders_timeout.toSec());
  encoders_timeout = ros::Duration(encoders_timeout_seconds);

  double encoder_fault_time_to_failure_seconds;
  ros::param::param<double>("~encoder_fault_time_to_failure",
      encoder_fault_time_to_failure_seconds, encoder_fault_time_to_failure.toSec());
  encoder_fault_time_to_failure = ros::Duration(encoder_fault_time_to_failure_seconds);
}

template<class M>
static inline ros::Duration age(M msg) 
{
  return ros::Time::now() - msg->header.stamp;
}

bool EncodersMonitor::detectFailedEncoderCandidate(VectorDrive::Index* candidate)
{
  // Attempt to detect a failed encoder. The symptom will be that the reported velocity will
  // be zero or very near it despite a non-zero commanded velocity. To avoid a false positive
  // due to motors under heavy load/stall, only flag the error when it greatly exceeds that 
  // of the second-most-erroneous wheel--- this is on the theory that typical operation is
  // unlikely to stall a single wheel.
  VectorDrive wheelSpeedMeasured = grizzly_msgs::vectorFromDriveMsg(*last_received_encoders_);
  VectorDrive wheelSpeedCommanded = grizzly_msgs::vectorFromDriveMsg(*last_received_drive_);
  VectorDrive wheelSpeedError = (wheelSpeedMeasured - wheelSpeedCommanded).cwiseAbs();

  // Find the index with maximum error, which is our failed encoder candidate.
  double max_error = wheelSpeedError.maxCoeff(candidate);

  // Now set that one to zero and use the new max to get the difference between greatest and
  // second-greatest error amounts.
  wheelSpeedError[*candidate] = 0;
  double max_error_diff = max_error - wheelSpeedError.maxCoeff();

  // If the measured speed is not small, then it's not a failure. A failed encoder will be either
  // still, or buzzing back and forth.
  if (wheelSpeedMeasured[*candidate] > 0.01) return false;

  // If the error difference does not exceed a threshold, then not an error.
  if (max_error_diff < encoder_speed_error_diff_threshold) return false;

  // Candidate failure is valid. Calling function will assert error if
  // this state persists for a set time period.
  return true;
}

bool EncodersMonitor::detectFailedEncoder()
{
  if (!last_received_encoders_ || !last_received_drive_) return false;

  VectorDrive::Index candidate_failed_encoder;
  if (detectFailedEncoderCandidate(&candidate_failed_encoder)) {
    if (last_received_encoders_->header.stamp - time_of_last_nonsuspect_encoders_ > encoder_fault_time_to_failure) {
      failed_encoder_ = candidate_failed_encoder;
      return true;
    } 
  } else {
    time_of_last_nonsuspect_encoders_ = last_received_encoders_->header.stamp;
  }
  return false;
}

bool EncodersMonitor::ok()
{
  // If we have no encoder data, or its old, then definitely not okay.
  if (!last_received_encoders_ || age(last_received_encoders_) > encoders_timeout) return false;

  // If we have no drive data, or it's old, then we're initializing; that's fine.
  if (!last_received_drive_ || age(last_received_drive_) > encoders_timeout) return true;

  if (detectFailedEncoder()) {
    // Not a recoverable fault.
    return false;
  }
 
  return true;
}

/**
 * Called in the context of making sure the vehicle is stopped before releasing the estop assertion.
 */
bool EncodersMonitor::moving()
{
  return last_received_encoders_ && !grizzly_msgs::isStationary(*last_received_encoders_.get());
}

/**
 * Prepare diagnostics. Called at 1Hz by the Updater.
 */
void EncodersMonitor::diagnostic(diagnostic_updater::DiagnosticStatusWrapper& stat)
{
  if (!last_received_encoders_)
  {
    stat.summary(2, "No encoders messages received.");
    return;
  } 

  stat.add("Age of last encoders message", age(last_received_encoders_).toSec());
  if (age(last_received_encoders_) > encoders_timeout)
  {
    stat.summaryf(2, "Last encoders message is stale.");
    return;
  }

  if (failed_encoder_ >= 0)
  {
    std::string wheel_str(grizzly_msgs::nameFromDriveIndex(failed_encoder_));
    stat.summaryf(2, "Encoder failure detected in %s wheel. Not a recoverable error, please service system.", wheel_str.c_str());
    return;
  }

  //stat.summary(1, "Encoders monitoring not implemented.");
  stat.summary(0, "Encoders look good.");
}

/**
 * New encoder data received. The important logic here is detecting when an encoder
 * or the associated cabling has failed. The general principle is:
 *   - compared commanded speed to actual speed (for an "error" value)
 *   - monitor how much time each encoder spends over an acceptable threshold of error.
 *   - compute the variance of the time-in-excess-error array. If it exceeds a
 *     a heuristically-set value, conclude that there may be a failed encoder.
 * Future work on this function could also account for current consumption by motors;
 * eg, a spike in current by a motor relative to the others with no associated speed change.
 */
void EncodersMonitor::encodersCallback(const grizzly_msgs::DriveConstPtr& encoders)
{
  last_received_encoders_ = encoders;
}

/**
 * New commands went out to the motors.
 */
void EncodersMonitor::driveCallback(const grizzly_msgs::DriveConstPtr& drive)
{
  last_received_drive_ = drive;
}

