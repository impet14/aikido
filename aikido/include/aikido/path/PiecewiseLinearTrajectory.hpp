#ifndef AIKIDO_PATH_PIECEWISELINEAR_TRAJECTORY_HPP_
#define AIKIDO_PATH_PIECEWISELINEAR_TRAJECTORY_HPP_

#include "Trajectory.hpp"
#include "../statespace/GeodesicInterpolator.hpp"

namespace aikido {
namespace path {

/// Trajectory that uses an \c Interpolator to interpolate between waypoints.
class PiecewiseLinearTrajectory : public Trajectory
{
public:
  /// Constructs an empty trajectory.
  ///
  /// \param _stateSpace state space this trajectory is defined in
  /// \param _interpolator interpolator used to interpolate between waypoints
  PiecewiseLinearTrajectory(
    aikido::statespace::StateSpacePtr _sspace,
    aikido::statespace::InterpolatorPtr _interpolator);

  /// Add a waypoint to the trajectory at the given time.
  ///
  /// \param _t time of the waypoint
  /// \param _state state at the waypoint
  void addWaypoint(double _t,
                   const aikido::statespace::StateSpace::State *_state);

  /// Gets a waypoint.
  ///
  /// \param _index waypoint index
  /// \return state of the waypoint at index \c _index
  const statespace::StateSpace::State* getWaypoint(size_t _index) const;

  /// Gets the number of waypoints.
  size_t getNumWaypoints() const;

  // Documentation inherited
  aikido::statespace::StateSpacePtr getStateSpace() const override;

  /// Gets the interpolator used to interpolate between waypoints.
  aikido::statespace::InterpolatorPtr getInterpolator() const;

  // Documentation inherited
  size_t getNumDerivatives() const override;

  // Documentation inherited
  double getStartTime() const override;

  // Documentation inherited
  double getEndTime() const override;

  // Documentation inherited
  double getDuration() const override;

  // Documentation inherited
  void evaluate(double _t,
                aikido::statespace::StateSpace::State *_state) const override;

  // Documentation inherited
  Eigen::VectorXd evaluate(double _t, int _derivative) const override;

private:
  /// Waypint in the trajectory.
  struct Waypoint
  {
    Waypoint(double _t, aikido::statespace::StateSpace::State *_state);

    /// Comparator to allow sorting waypoints based on time
    bool operator<(const Waypoint &rhs) const;

    /// Comparator to allow sorting waypoints based on time
    bool operator<(const double &rhs) const;

    double t;
    aikido::statespace::StateSpace::State *state;
  };

  /// Get the index of the first waypoint whose time value is larger than _t.
  /// Throws std::domain_error if _t is larger than last waypoint in the
  /// trajectory.
  int getWaypointIndexAfterTime(double _t) const;

  aikido::statespace::StateSpacePtr mStateSpace;
  aikido::statespace::InterpolatorPtr mInterpolator;
  std::vector<Waypoint> mWaypoints;
};

using PiecewiseLinearTrajectoryPtr = std::shared_ptr<PiecewiseLinearTrajectory>;

} // namespace path
} // namespace aikido

#endif // ifndef AIKIDO_PATH_PIECEWISELINEAR_TRAJECTORY_HPP_
