#include "aikido/robot/util.hpp"
#include <dart/common/Console.hpp>
#include <dart/common/StlHelpers.hpp>
#include <dart/common/Timer.hpp>
#include <ompl/geometric/planners/rrt/RRTConnect.h>
#include "aikido/common/RNG.hpp"
#include "aikido/constraint/CyclicSampleable.hpp"
#include "aikido/constraint/FiniteSampleable.hpp"
#include "aikido/constraint/NewtonsMethodProjectable.hpp"
#include "aikido/constraint/Testable.hpp"
#include "aikido/constraint/TestableIntersection.hpp"
#include "aikido/constraint/dart/FrameDifferentiable.hpp"
#include "aikido/constraint/dart/FrameTestable.hpp"
#include "aikido/constraint/dart/InverseKinematicsSampleable.hpp"
#include "aikido/constraint/dart/JointStateSpaceHelpers.hpp"
#include "aikido/distance/defaults.hpp"
#include "aikido/planner/PlanningResult.hpp"
#include "aikido/planner/SnapPlanner.hpp"
#include "aikido/planner/ompl/CRRTConnect.hpp"
#include "aikido/planner/ompl/Planner.hpp"
#include "aikido/planner/parabolic/ParabolicSmoother.hpp"
#include "aikido/planner/parabolic/ParabolicTimer.hpp"
#include "aikido/planner/vectorfield/VectorFieldPlanner.hpp"
#include "aikido/statespace/GeodesicInterpolator.hpp"
#include "aikido/statespace/StateSpace.hpp"
#include "aikido/statespace/dart/MetaSkeletonStateSaver.hpp"
#include "aikido/statespace/dart/MetaSkeletonStateSpace.hpp"

namespace aikido {
namespace robot {

namespace {
/// Gets Goal and Constraint TSR for End Effector.
/// \param[in] bodyNode End-effector body node
/// \param[in] direction End-effector direction
/// \param[in] distance Offset to move
/// \param[in] goal Goal TSR
/// \param[in] constraint Constraint TSR
/// \param[in] positionTolerance Tolerance in position
/// \param[in] angularTolerance Tolerance in angle
bool getGoalAndConstraintTSRForEndEffectorOffset(
    const dart::dynamics::BodyNodePtr& bodyNode,
    const Eigen::Vector3d& direction,
    double distance,
    const constraint::dart::TSRPtr& goal,
    const constraint::dart::TSRPtr& constraint,
    double positionTolerance = 1e-3,
    double angularTolerance = 1e-3);
}

namespace util {

using constraint::dart::CollisionFreePtr;
using constraint::dart::InverseKinematicsSampleable;
using constraint::dart::TSR;
using constraint::dart::TSRPtr;
using constraint::TestablePtr;
using constraint::dart::createProjectableBounds;
using constraint::dart::createSampleableBounds;
using constraint::dart::createTestableBounds;
using distance::createDistanceMetric;
using statespace::GeodesicInterpolator;
using statespace::dart::MetaSkeletonStateSpacePtr;
using statespace::dart::MetaSkeletonStateSaver;
using statespace::dart::MetaSkeletonStateSpace;
using statespace::StateSpace;
using trajectory::Interpolated;
using trajectory::InterpolatedPtr;
using trajectory::SplinePtr;
using common::cloneRNGFrom;
using common::RNG;

using dart::collision::FCLCollisionDetector;
using dart::common::make_unique;
using dart::dynamics::BodyNodePtr;
using dart::dynamics::ChainPtr;
using dart::dynamics::InverseKinematics;
using dart::dynamics::MetaSkeleton;
using dart::dynamics::MetaSkeletonPtr;
using dart::dynamics::SkeletonPtr;

static const double collisionResolution = 0.1;

//==============================================================================
InterpolatedPtr planToConfiguration(
    const MetaSkeletonStateSpacePtr& space,
    const MetaSkeletonPtr& metaSkeleton,
    const StateSpace::State* goalState,
    const TestablePtr& collisionTestable,
    RNG* rng,
    double timelimit)
{
  using planner::ompl::planOMPL;
  using planner::planSnap;

  auto robot = metaSkeleton->getBodyNode(0)->getSkeleton();
  std::lock_guard<std::mutex> lock(robot->getMutex());
  // Save the current state of the space
  auto saver = MetaSkeletonStateSaver(metaSkeleton);
  DART_UNUSED(saver);

  // First test with Snap Planner
  planner::PlanningResult pResult;
  InterpolatedPtr untimedTrajectory;

  auto startState = space->getScopedStateFromMetaSkeleton(metaSkeleton.get());

  untimedTrajectory = planSnap(
      space,
      startState,
      goalState,
      std::make_shared<GeodesicInterpolator>(space),
      collisionTestable,
      pResult);

  // Return if the trajectory is non-empty
  if (untimedTrajectory)
    return untimedTrajectory;

  untimedTrajectory = planOMPL<ompl::geometric::RRTConnect>(
      startState,
      goalState,
      space,
      std::make_shared<GeodesicInterpolator>(space),
      createDistanceMetric(space),
      createSampleableBounds(space, rng->clone()),
      collisionTestable,
      createTestableBounds(space),
      createProjectableBounds(space),
      timelimit,
      collisionResolution);

  return untimedTrajectory;
}

//==============================================================================
InterpolatedPtr planToConfigurations(
    const MetaSkeletonStateSpacePtr& space,
    const MetaSkeletonPtr& metaSkeleton,
    const std::vector<StateSpace::State*>& goalStates,
    const TestablePtr& collisionTestable,
    RNG* rng,
    double timelimit)
{
  using planner::ompl::planOMPL;

  auto robot = metaSkeleton->getBodyNode(0)->getSkeleton();
  std::lock_guard<std::mutex> lock(robot->getMutex());
  // Save the current state of the space
  auto saver = MetaSkeletonStateSaver(metaSkeleton);
  DART_UNUSED(saver);

  auto startState = space->getScopedStateFromMetaSkeleton(metaSkeleton.get());

  for (const auto& goalState : goalStates)
  {
    // First test with Snap Planner
    planner::PlanningResult pResult;
    InterpolatedPtr untimedTrajectory;

    untimedTrajectory = planner::planSnap(
        space,
        startState,
        goalState,
        std::make_shared<GeodesicInterpolator>(space),
        collisionTestable,
        pResult);

    // Return if the trajectory is non-empty
    if (untimedTrajectory)
      return untimedTrajectory;

    untimedTrajectory = planOMPL<ompl::geometric::RRTConnect>(
        startState,
        goalState,
        space,
        std::make_shared<GeodesicInterpolator>(space),
        createDistanceMetric(space),
        createSampleableBounds(space, rng->clone()),
        collisionTestable,
        createTestableBounds(space),
        createProjectableBounds(space),
        timelimit,
        collisionResolution);

    return untimedTrajectory;
  }

  return nullptr;
}

//==============================================================================
InterpolatedPtr planToTSR(
    const MetaSkeletonStateSpacePtr& space,
    const MetaSkeletonPtr& metaSkeleton,
    const BodyNodePtr& bn,
    const TSRPtr& tsr,
    const TestablePtr& collisionTestable,
    RNG* rng,
    double timelimit,
    std::size_t maxNumTrials)
{
  // Convert TSR constraint into IK constraint
  InverseKinematicsSampleable ikSampleable(
      space,
      metaSkeleton,
      tsr,
      createSampleableBounds(space, rng->clone()),
      InverseKinematics::create(bn),
      maxNumTrials);

  auto generator = ikSampleable.createSampleGenerator();

  // Goal state
  auto goalState = space->createState();

  auto startState = space->getScopedStateFromMetaSkeleton(metaSkeleton.get());

  Eigen::VectorXd goal;

  // TODO: Change this to timelimit once we use a fail-fast planner
  double timelimitPerSample = timelimit / maxNumTrials;

  // Save the current state of the space
  auto saver = MetaSkeletonStateSaver(metaSkeleton);
  DART_UNUSED(saver);

  // HACK: try lots of snap plans first
  static const std::size_t maxSnapSamples{100};
  std::size_t snapSamples = 0;

  auto robot = metaSkeleton->getBodyNode(0)->getSkeleton();
  while (snapSamples < maxSnapSamples && generator->canSample())
  {
    // Sample from TSR
    {
      std::lock_guard<std::mutex> lock(robot->getMutex());
      bool sampled = generator->sample(goalState);
      if (!sampled)
        continue;

      space->convertStateToPositions(goalState, goal);

      // Set to start state
      space->setState(metaSkeleton.get(), startState);
    }
    ++snapSamples;

    planner::PlanningResult pResult;
    auto traj = planner::planSnap(
        space,
        startState,
        goalState,
        std::make_shared<GeodesicInterpolator>(space),
        collisionTestable,
        pResult);

    if (traj)
      return traj;
  }

  // Start the timer
  dart::common::Timer timer;
  timer.start();
  while (timer.getElapsedTime() < timelimit && generator->canSample())
  {
    // Sample from TSR
    {
      std::lock_guard<std::mutex> lock(robot->getMutex());
      bool sampled = generator->sample(goalState);
      if (!sampled)
        continue;

      // Set to start state
      space->setState(metaSkeleton.get(), startState);
    }

    auto traj = planToConfiguration(
        space,
        metaSkeleton,
        goalState,
        collisionTestable,
        rng,
        std::min(timelimitPerSample, timelimit - timer.getElapsedTime()));

    if (traj)
      return traj;
  }

  return nullptr;
}

//==============================================================================
InterpolatedPtr planToTSRwithTrajectoryConstraint(
    const MetaSkeletonStateSpacePtr& space,
    const dart::dynamics::MetaSkeletonPtr& metaSkeleton,
    const dart::dynamics::BodyNodePtr& bodyNode,
    const TSRPtr& goalTsr,
    const TSRPtr& constraintTsr,
    const TestablePtr& collisionTestable,
    double timelimit,
    const CRRTPlannerParameters& crrtParameters)
{
  using aikido::constraint::Sampleable;
  using aikido::constraint::dart::InverseKinematicsSampleable;
  using aikido::constraint::CyclicSampleable;
  using aikido::constraint::dart::FrameDifferentiable;
  using aikido::constraint::dart::FrameTestable;
  using aikido::constraint::NewtonsMethodProjectable;
  using aikido::planner::ompl::planCRRTConnect;

  std::size_t projectionMaxIteration = crrtParameters.projectionMaxIteration;
  double projectionTolerance = crrtParameters.projectionTolerance;

  auto robot = metaSkeleton->getBodyNode(0)->getSkeleton();
  std::lock_guard<std::mutex> lock(robot->getMutex());

  // Save the current state of the space
  auto saver = MetaSkeletonStateSaver(metaSkeleton);
  DART_UNUSED(saver);

  // Create seed constraint
  std::shared_ptr<Sampleable> seedConstraint
      = std::move(createSampleableBounds(space, crrtParameters.rng->clone()));

  // crate IK
  auto ik = InverseKinematics::create(bodyNode);

  // create goal sampleable
  auto goalSampleable = std::make_shared<InverseKinematicsSampleable>(
      space,
      metaSkeleton,
      std::make_shared<CyclicSampleable>(goalTsr),
      seedConstraint,
      ik,
      crrtParameters.maxNumTrials);

  // create goal testable
  auto goalTestable = std::make_shared<FrameTestable>(
      space, metaSkeleton, bodyNode.get(), goalTsr);

  // create constraint sampleable
  auto constraintSampleable = std::make_shared<InverseKinematicsSampleable>(
      space,
      metaSkeleton,
      constraintTsr,
      seedConstraint,
      ik,
      crrtParameters.maxNumTrials);

  // create constraint projectable
  auto frameDiff = std::make_shared<FrameDifferentiable>(
      space, metaSkeleton, bodyNode.get(), constraintTsr);

  std::vector<double> projectionToleranceVec(
      frameDiff->getConstraintDimension(), projectionTolerance);
  auto constraintProjectable = std::make_shared<NewtonsMethodProjectable>(
      frameDiff, projectionToleranceVec, projectionMaxIteration);

  // Current state
  auto startState = space->getScopedStateFromMetaSkeleton(metaSkeleton.get());

  // Call planner
  auto traj = planCRRTConnect(
      startState,
      goalTestable,
      goalSampleable,
      constraintProjectable,
      space,
      std::make_shared<GeodesicInterpolator>(space),
      createDistanceMetric(space),
      constraintSampleable,
      collisionTestable,
      createTestableBounds(space),
      createProjectableBounds(space),
      timelimit,
      crrtParameters.maxExtensionDistance,
      crrtParameters.maxDistanceBtwProjections,
      crrtParameters.minStepSize,
      crrtParameters.minTreeConnectionDistance);

  return traj;
}

//==============================================================================
trajectory::TrajectoryPtr planToEndEffectorOffset(
    const MetaSkeletonStateSpacePtr& space,
    const dart::dynamics::MetaSkeletonPtr& metaSkeleton,
    const dart::dynamics::BodyNodePtr& bodyNode,
    const Eigen::Vector3d& direction,
    const TestablePtr& collisionTestable,
    double distance,
    double timelimit,
    double positionTolerance,
    double angularTolerance,
    const VectorFieldPlannerParameters& vfParameters,
    const CRRTPlannerParameters& crrtParameters)
{

  auto robot = metaSkeleton->getBodyNode(0)->getSkeleton();
  std::lock_guard<std::mutex> lock(robot->getMutex());

  auto saver = MetaSkeletonStateSaver(metaSkeleton);
  DART_UNUSED(saver);

  auto minDistance = distance - vfParameters.negativeDistanceTolerance;
  auto maxDistance = distance + vfParameters.positiveDistanceTolerance;

  auto traj = planner::vectorfield::planToEndEffectorOffset(
      space,
      metaSkeleton,
      bodyNode,
      collisionTestable,
      direction,
      minDistance,
      maxDistance,
      positionTolerance,
      angularTolerance,
      vfParameters.initialStepSize,
      vfParameters.jointLimitTolerance,
      vfParameters.constraintCheckResolution,
      std::chrono::duration<double>(timelimit));

  if (traj)
    return std::move(traj);

  return planToEndEffectorOffsetByCRRT(
      space,
      metaSkeleton,
      bodyNode,
      collisionTestable,
      direction,
      distance,
      timelimit,
      positionTolerance,
      angularTolerance,
      crrtParameters);
}

//==============================================================================
InterpolatedPtr planToEndEffectorOffsetByCRRT(
    const MetaSkeletonStateSpacePtr& space,
    const dart::dynamics::MetaSkeletonPtr& metaSkeleton,
    const BodyNodePtr& bodyNode,
    const TestablePtr& collisionTestable,
    const Eigen::Vector3d& direction,
    double distance,
    double timelimit,
    double positionTolerance,
    double angularTolerance,
    const CRRTPlannerParameters& crrtParameters)
{
  // if direction vector is a zero vector
  if (direction.norm() == 0.0)
  {
    throw std::runtime_error("Direction vector is a zero vector");
  }

  // normalize direction vector
  Eigen::Vector3d directionCopy = direction;
  directionCopy.normalize();

  if (distance < 0.0)
  {
    distance = -distance;
    directionCopy = -directionCopy;
  }

  auto goalTsr = std::make_shared<TSR>();
  auto constraintTsr = std::make_shared<TSR>();
  bool success = getGoalAndConstraintTSRForEndEffectorOffset(
      bodyNode,
      directionCopy,
      distance,
      goalTsr,
      constraintTsr,
      positionTolerance,
      angularTolerance);

  if (!success)
    throw std::runtime_error("failed in creating TSR");

  auto untimedTrajectory = planToTSRwithTrajectoryConstraint(
      space,
      metaSkeleton,
      bodyNode,
      goalTsr,
      constraintTsr,
      collisionTestable,
      timelimit,
      crrtParameters);

  return untimedTrajectory;
}
//==============================================================================
std::unordered_map<std::string, const Eigen::VectorXd>
parseYAMLToNamedConfigurations(const YAML::Node& node)
{
  std::unordered_map<std::string, const Eigen::VectorXd> namedConfigurations;

  for (const auto& configurationNode : node)
  {
    auto configurationName = configurationNode.first.as<std::string>();
    auto configuration = configurationNode.second.as<Eigen::VectorXd>();

    namedConfigurations.emplace(configurationName, configuration);
  }

  return namedConfigurations;
}

//==============================================================================
bool getGoalAndConstraintTSRForEndEffectorOffset(
    const BodyNodePtr& bodyNode,
    const Eigen::Vector3d& direction,
    double distance,
    const TSRPtr& goal,
    const TSRPtr& constraint,
    double positionTolerance,
    double angularTolerance)
{
  if (goal == nullptr || constraint == nullptr)
    return false;

  // create goal TSR
  // 'object frame w' is at end-effector, z pointed along direction to move
  Eigen::Isometry3d H_world_ee = bodyNode->getWorldTransform();
  Eigen::Isometry3d H_world_w
      = getLookAtIsometry(H_world_ee.translation(), direction);
  Eigen::Isometry3d H_w_ee = H_world_w.inverse() * H_world_ee;

  Eigen::Isometry3d Hw_end = Eigen::Isometry3d::Identity();
  Hw_end.translation()[2] = distance;

  goal->mT0_w = H_world_w * Hw_end;
  goal->mTw_e = H_w_ee;
  goal->mBw.setZero();

  // create constraint TSR
  constraint->mT0_w = H_world_w;
  constraint->mTw_e = H_w_ee;
  constraint->mBw << -positionTolerance, positionTolerance, -positionTolerance,
      positionTolerance, 0.0, distance, -angularTolerance, angularTolerance,
      -angularTolerance, angularTolerance, -angularTolerance, angularTolerance;

  return true;
}

//==============================================================================
Eigen::Isometry3d getLookAtIsometry(
    const Eigen::Vector3d& positionFrom, const Eigen::Vector3d& positionTo)
{
  if (positionTo.norm() < 1e-6)
  {
    throw std::runtime_error("positionTo cannot be a zero vector.");
  }
  Eigen::Isometry3d H = Eigen::Isometry3d::Identity();
  H.translation() = positionFrom;

  // original z axis direction
  H.linear()
      = Eigen::Quaterniond::FromTwoVectors(Eigen::Vector3d::UnitZ(), positionTo)
            .toRotationMatrix();
  return H;
}

} // namespace util
} // namespace robot
} // namespace aikido
