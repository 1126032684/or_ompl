#include <TSRGoal.h>

#include <boost/foreach.hpp>
#include <ompl/base/spaces/RealVectorStateSpace.h>
#include <ompl/util/RandomNumbers.h>

using namespace or_ompl;
namespace ob = ompl::base;

TSRGoal::TSRGoal(const ob::SpaceInformationPtr &si,
				 const TSR::Ptr &tsr, 
				 OpenRAVE::RobotBasePtr robot)
    : ob::GoalSampleableRegion(si),  _robot(robot){
    
	std::vector<TSR::Ptr> tsrs(1);
	tsrs.push_back(tsr);
	TSRChain::Ptr tsrchain = boost::make_shared<TSRChain>(true, false, false, tsrs);
	_tsr_chains.push_back(tsrchain);
}

TSRGoal::TSRGoal(const ob::SpaceInformationPtr &si,
				 const TSRChain::Ptr &tsrchain, 
				 OpenRAVE::RobotBasePtr robot)
    : ob::GoalSampleableRegion(si), _robot(robot){

	_tsr_chains.push_back(tsrchain);
}

TSRGoal::TSRGoal(const ob::SpaceInformationPtr &si,
				 const std::vector<TSRChain::Ptr> &tsrchains, 
				 OpenRAVE::RobotBasePtr robot)
    : ob::GoalSampleableRegion(si), _tsr_chains(tsrchains), _robot(robot){
    
}

TSRGoal::~TSRGoal() {
    
}

bool TSRGoal::isSatisfied(const ompl::base::State *state) const {

    bool satisfied = (distanceGoal(state) == 0.0);
	return satisfied;
}
            
double TSRGoal::distanceGoal(const ompl::base::State *state) const {

	// Save the state of the robot
	OpenRAVE::EnvironmentMutex::scoped_lock lockenv(_robot->GetEnv()->GetMutex());
	OpenRAVE::KinBody::KinBodyStateSaver rsaver(_robot);

	// Put the robot in the pose that is represented in the state
	const ompl::base::RealVectorStateSpace::StateType* mstate = state->as<ompl::base::RealVectorStateSpace::StateType>();
	std::vector<double> dof_values(_robot->GetActiveDOF());
	for(unsigned int idx=0; idx < _robot->GetActiveDOF(); idx++){
		dof_values[idx] = mstate->values[idx];
	}
	_robot->SetActiveDOFValues(dof_values, false);

	// Get the end effector transform
	OpenRAVE::Transform or_tf = _robot->GetActiveManipulator()->GetEndEffectorTransform();
    OpenRAVE::TransformMatrix or_matrix(or_tf);
	
	// Convert to Eigen
    Eigen::Affine3d ee_pose = Eigen::Affine3d::Identity();
    ee_pose.linear() << or_matrix.m[0], or_matrix.m[1], or_matrix.m[2],
                         or_matrix.m[4], or_matrix.m[5], or_matrix.m[6],
                         or_matrix.m[8], or_matrix.m[9], or_matrix.m[10];
    ee_pose.translation() << or_matrix.trans.x, or_matrix.trans.y, or_matrix.trans.z;

	// Get distance to TSR
	double distance = std::numeric_limits<double>::infinity();
	BOOST_FOREACH(TSRChain::Ptr tsrchain, _tsr_chains){
		Eigen::Matrix<double, 6, 1> ee_distance = tsrchain->distance(ee_pose);
		double tdistance = ee_distance.norm();
		if(tdistance < distance){
			distance = tdistance;
		}
	}

	// Reset the state of the robot
	rsaver.Restore();

	return distance;
}
            
void TSRGoal::sampleGoal(ompl::base::State *state) const {

	bool success = false;

	// TODO: Figure out how to bail correctly if an IK isn't found
	for(unsigned int count=0; count < 20 && !success; count++){
		// Pick a TSR to sample
		int idx = 0;
		if(_tsr_chains.size() > 1){
			ompl::RNG rng;
			idx = rng.uniformInt(0, _tsr_chains.size()-1);
		}

		// Sample the TSR
		Eigen::Affine3d ee_pose = _tsr_chains[idx]->sample();

		// Find an associated IK
		OpenRAVE::TransformMatrix or_matrix;
		or_matrix.rotfrommat(
			ee_pose.matrix()(0, 0), ee_pose.matrix()(0, 1), ee_pose.matrix()(0, 2),
			ee_pose.matrix()(1, 0), ee_pose.matrix()(1, 1), ee_pose.matrix()(1, 2),
			ee_pose.matrix()(2, 0), ee_pose.matrix()(2, 1), ee_pose.matrix()(2, 2)
			);
		or_matrix.trans.x = ee_pose(0, 3);
		or_matrix.trans.y = ee_pose(1, 3);
		or_matrix.trans.z = ee_pose(2, 3);

		OpenRAVE::IkParameterization ik_param(or_matrix, OpenRAVE::IKP_Transform6D);
		std::vector<OpenRAVE::dReal> ik_solution;
		success = _robot->GetActiveManipulator()->FindIKSolution(ik_param, ik_solution, OpenRAVE::IKFO_CheckEnvCollisions);

		// Set the state
		if(success){
			const ompl::base::RealVectorStateSpace::StateType* mstate = state->as<ompl::base::RealVectorStateSpace::StateType>();
			for(unsigned int idx=0; idx < ik_solution.size(); idx++){
				mstate->values[idx] = ik_solution[idx];
			}
		}
	}

	if(!success){
		RAVELOG_ERROR("[TSRGoal] Failed to sample valid goal.");
	}
}

unsigned int TSRGoal::maxSampleCount() const {

    return std::numeric_limits<unsigned int>::max();
}