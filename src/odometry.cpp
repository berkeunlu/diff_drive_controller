/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013, PAL Robotics, S.L.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the PAL Robotics nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/*
 * Author: Luca Marchionni
 * Author: Bence Magyar
 * Author: Enrique Fernández
 * Author: Paul Mathieu
 */

#include <diff_drive_controller/odometry.h>

namespace diff_drive_controller
{
  namespace bacc = boost::accumulators;

  Odometry::Odometry(size_t velocity_rolling_window_size)
  : timestamp_(0.0)
  , x_(0.0)
  , y_(0.0)
  , heading_(0.0)
  , linear_vel_(0.0)
  , angular_vel_(0.0)
  , wheel_separation_(0.0)
  , left_wheel_radius_(0.0)
  , right_wheel_radius_(0.0)
  , left_wheel_old_pos_(0.0)
  , right_wheel_old_pos_(0.0)
  , velocity_rolling_window_size_(velocity_rolling_window_size)
  , linear_accum_(RollingWindow::window_size = velocity_rolling_window_size)
  , angular_accum_(RollingWindow::window_size = velocity_rolling_window_size)
  , integrate_fun_(std::bind(&Odometry::integrateExact, this, std::placeholders::_1, std::placeholders::_2))
  {
  }

  void Odometry::init(const ros::Time& time)
  {
    // Reset accumulators and timestamp:
    resetAccumulators();
    timestamp_ = time;
  }

  bool Odometry::updateWithVelEst(double left_pos, double right_pos, const ros::Time &time)
  {
    /// Get current wheel joint positions:
    const double left_wheel_cur_pos  = left_pos  * left_wheel_radius_;
    const double right_wheel_cur_pos = right_pos * right_wheel_radius_;

    /// Estimate velocity of wheels using old and current position:
    const double left_wheel_dist_traveled  = left_wheel_cur_pos  - left_wheel_old_pos_;
    const double right_wheel_dist_traveled = right_wheel_cur_pos - right_wheel_old_pos_;

    /// Update old position with current:
    left_wheel_old_pos_  = left_wheel_cur_pos;
    right_wheel_old_pos_ = right_wheel_cur_pos;

    /// Compute linear and angular diff:
    const double linear  = (right_wheel_dist_traveled + left_wheel_dist_traveled) * 0.5 ;
    const double angular = (right_wheel_dist_traveled - left_wheel_dist_traveled) / wheel_separation_;

    /// Integrate odometry:
    integrate_fun_(linear, angular);

    /// We cannot estimate the speed with very small time intervals:
    const double dt = (time - timestamp_).toSec();
    if (dt < 0.0001)
      return false; // Interval too small to integrate with

    timestamp_ = time;

    /// Estimate speeds using a rolling mean to filter them out:
    linear_accum_(linear / dt);
    angular_accum_(angular / dt);

    linear_vel_ = bacc::rolling_mean(linear_accum_);
    angular_vel_ = bacc::rolling_mean(angular_accum_);

    return true;
  }

  bool Odometry::update(double left_pos, double right_pos, double left_vel, double right_vel, const ros::Time &time)
  {
      /// Calculate linear and angular velocities
      const double r = ((left_wheel_radius_ + right_wheel_radius_) * 0.5000000); // cus y not
      linear_vel_ = (left_vel + right_vel) * r / 2.0000000;
      angular_vel_ = r * (right_vel - left_vel) / wheel_separation_;

      /// Get current wheel joint positions:
      const double left_wheel_cur_pos  = left_pos  * left_wheel_radius_;
      const double right_wheel_cur_pos = right_pos * right_wheel_radius_;

      /// Find the distances traveled since last read:
      const double left_wheel_dist_traveled  = left_wheel_cur_pos  - left_wheel_old_pos_;
      const double right_wheel_dist_traveled = right_wheel_cur_pos - right_wheel_old_pos_;

      /// Update old position with current:
      left_wheel_old_pos_  = left_wheel_cur_pos;
      right_wheel_old_pos_ = right_wheel_cur_pos;

      /// Compute linear and angular diff:
      const double linear_diff  = (right_wheel_dist_traveled + left_wheel_dist_traveled) * 0.5000000;
      const double angular_diff = (right_wheel_dist_traveled - left_wheel_dist_traveled) / wheel_separation_;

      /// Integrate odometry:
      integrate_fun_(linear_diff, angular_diff);

      return true;
  }

  void Odometry::updateOpenLoop(double linear, double angular, const ros::Time &time)
  {
    /// Save last linear and angular velocity:
    linear_vel_ = linear;
    angular_vel_ = angular;

    /// Integrate odometry:
    const double dt = (time - timestamp_).toSec();
    timestamp_ = time;
    integrate_fun_(linear * dt, angular * dt);
  }

  void Odometry::setWheelParams(double wheel_separation, double left_wheel_radius, double right_wheel_radius)
  {
    wheel_separation_   = wheel_separation;
    left_wheel_radius_  = left_wheel_radius;
    right_wheel_radius_ = right_wheel_radius;
  }

  void Odometry::setVelocityRollingWindowSize(size_t velocity_rolling_window_size)
  {
    velocity_rolling_window_size_ = velocity_rolling_window_size;

    resetAccumulators();
  }

  void Odometry::integrateRungeKutta2(double linear, double angular)
  {
    const double direction = heading_ + angular * 0.5;

    /// Runge-Kutta 2nd order integration:
    x_       += linear * cos(direction);
    y_       += linear * sin(direction);
    heading_ += angular;
  }

  /**
   * \brief Other possible integration method provided by the class
   * \param linear
   * \param angular
   */
  void Odometry::integrateExact(double linear, double angular)
  {
    if (fabs(angular) < 1e-4)
      integrateRungeKutta2(linear, angular);
    else
    {
      /// Exact integration (should solve problems when angular is zero):
      const double heading_old = heading_;
      const double radius = linear / angular;
      heading_ += angular;
      x_       +=  radius * (sin(heading_) - sin(heading_old));
      y_       += -radius * (cos(heading_) - cos(heading_old));
    }
  }

  void Odometry::resetAccumulators()
  {
    linear_accum_ = RollingMeanAcc(RollingWindow::window_size = velocity_rolling_window_size_);
    angular_accum_ = RollingMeanAcc(RollingWindow::window_size = velocity_rolling_window_size_);
  }

} // namespace diff_drive_controller
