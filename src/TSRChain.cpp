#include <TSRChain.h>

#include <limits>
#include <vector>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>

using namespace or_ompl;

TSRChain::TSRChain() : _initialized(false), _sample_start(false), _sample_goal(false), _constrain(false), _tsr_robot(TSRRobot::Ptr()) {

}

TSRChain::TSRChain(const bool &sample_start, 
				   const bool &sample_goal, 
				   const bool &constrain,
				   const std::vector<TSR::Ptr> &tsrs) :
	_initialized(true), _sample_start(sample_start), _sample_goal(sample_goal),
	_constrain(constrain), _tsrs(tsrs), _tsr_robot(TSRRobot::Ptr()) {

}

bool TSRChain::deserialize(std::stringstream &ss) {

	ss >> _sample_start;
	ss >> _sample_goal;
	ss >> _constrain;

	int num_tsrs;
	ss >> num_tsrs;

	_tsrs.resize(num_tsrs);
    bool valid = true;
	for(unsigned int idx = 0; idx < num_tsrs; idx++){
		TSR::Ptr new_tsr = boost::make_shared<TSR>();
		valid &= new_tsr->deserialize(ss);
		_tsrs[idx] = new_tsr;
	}

    return valid;

	// TODO: Ignored are mmicbody name and mimicbodyjoints	
}

Eigen::Affine3d TSRChain::sample() const {

	Eigen::Affine3d T0_w;
	if(_tsrs.size() == 0){
        throw OpenRAVE::openrave_exception(
            "There are no TSRs in this TSR Chain.",
            OpenRAVE::ORE_InvalidState
        );
	}

	T0_w = _tsrs.front()->getOriginTransform();
	BOOST_FOREACH(TSR::Ptr tsr, _tsrs){
		T0_w  = T0_w * tsr->sampleDisplacementTransform() * tsr->getEndEffectorOffsetTransform();
	}

	return T0_w;
}

Eigen::Matrix<double, 6, 1> TSRChain::distance(const Eigen::Affine3d &ee_pose) const {

	if(_tsrs.size() == 1){
		TSR::Ptr tsr = _tsrs.front();
		return tsr->distance(ee_pose);
	}
     
    if(!_tsr_robot){
        throw OpenRAVE::openrave_exception(
            "Failed to compute distance to TSRChain. Did you set the"
            " environment by calling the setEnv function?",
            OpenRAVE::ORE_InvalidState
        );
    }
    
    if(!_tsr_robot->construct()){
        throw OpenRAVE::openrave_exception(
            "Failed to robotize TSR.",
            OpenRAVE::ORE_Failed
        );
    }
    
    RAVELOG_DEBUG("[TSRChain] Solving IK to compute distance");
    // Compute the ideal pose of the end-effector
    Eigen::Affine3d Ttarget = ee_pose * _tsrs.back()->getEndEffectorOffsetTransform().inverse();
    
    // Ask the robot to solve ik to find the closest possible end-effector transform
    Eigen::Affine3d Tnear = _tsr_robot->findNearestFeasibleTransform(Ttarget);

    // Compute the distance between the two
    Eigen::Affine3d offset = Tnear.inverse() * Ttarget;
    Eigen::Matrix<double, 6, 1> dist = Eigen::Matrix<double, 6, 1>::Zero();
    dist[0] = offset.translation()[0];
	dist[1] = offset.translation()[1];
	dist[2] = offset.translation()[2];
	dist[3] = atan2(offset.rotation()(2,1), offset.rotation()(2,2));
	dist[4] = -asin(offset.rotation()(2,0));
	dist[5] = atan2(offset.rotation()(1,0), offset.rotation()(0,0));

    return dist;
}

void TSRChain::setEnv(const OpenRAVE::EnvironmentBasePtr &penv){

    _tsr_robot = boost::make_shared<TSRRobot>(_tsrs, penv);
    
}
