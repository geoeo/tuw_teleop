/***************************************************************************
 *   Software License Agreement (BSD License)                              *
 *   Copyright (C) 2015 by Horatiu George Todoran <todorangrg@gmail.com>   *
 *                                                                         *
 *   Redistribution and use in source and binary forms, with or without    *
 *   modification, are permitted provided that the following conditions    *
 *   are met:                                                              *
 *                                                                         *
 *   1. Redistributions of source code must retain the above copyright     *
 *      notice, this list of conditions and the following disclaimer.      *
 *   2. Redistributions in binary form must reproduce the above copyright  *
 *      notice, this list of conditions and the following disclaimer in    *
 *      the documentation and/or other materials provided with the         *
 *      distribution.                                                      *
 *   3. Neither the name of the copyright holder nor the names of its      *
 *      contributors may be used to endorse or promote products derived    *
 *      from this software without specific prior written permission.      *
 *                                                                         *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS   *
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT     *
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS     *
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE        *
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,  *
 *   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,  *
 *   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;      *
 *   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER      *
 *   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT    *
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY *
 *   WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           *
 *   POSSIBILITY OF SUCH DAMAGE.                                           *
 ***************************************************************************/


#include <tuw_gui2iws/tuw_gui2iws_node.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <tf/transform_datatypes.h>
#include <cmath>

using namespace tuw;
using namespace std;

int main ( int argc, char **argv ) {

    ros::init ( argc, argv, "blue_control" );  /// initializes the ros node with default name
    ros::NodeHandle n;
    
    double figurePixSize = 1024;
    double figureRadius  = 2;
    double figureGrid  = 0.0;
    
    Gui2IwsNode gui2IwsNode ( n );
    gui2IwsNode.init();
    gui2IwsNode.initFigure(figurePixSize, figureRadius, figureGrid);
    
    ros::Rate rate ( 100. ); 
    while ( ros::ok() ) {
	
	gui2IwsNode.plot();
	
        gui2IwsNode.publishJntsCmds();

        /// calls all callbacks waiting in the queue
        ros::spinOnce();

        /// sleeps for the time remaining to let us hit our publish rate
        rate.sleep();
    }
    return 0;
}

/**
 * Constructor
 **/
Gui2IwsNode::Gui2IwsNode ( ros::NodeHandle & n )
    : Gui2Iws(ros::NodeHandle("~").getNamespace()), 
    n_ ( n ), 
    n_param_ ( "~" ){

    pub_jnts_cmd_     = n.advertise<tuw_gazebo_msgs::IwsCmd_VRAT>("base_cmds"  , 1);
    sub_joint_states_ = n.subscribe( "joint_states", 1, &Gui2IwsNode::callbackJointStates, this );
    sub_odometry_     = n.subscribe( "odom"        , 1, &Gui2IwsNode::callbackOdometry   , this );
    
    reconfigureFnc_ = boost::bind ( &Gui2IwsNode::callbackConfigBlueControl, this,  _1, _2 );
    reconfigureServer_.setCallback ( reconfigureFnc_ );
}

void Gui2IwsNode::callbackConfigBlueControl ( tuw_teleop::Gui2IwsConfig &config, uint32_t level ) {
    ROS_DEBUG ( "callbackConfigBlueControl!" );
    config_ = config;
    init();
}

void Gui2IwsNode::callbackJointStates( const sensor_msgs::JointState::ConstPtr &joint_ ){
    std::size_t k;
    for ( std::size_t i = 0; i < IwsSpSystem::legSize; i++ ) {
	k = IwsSpSystem::ij2k(i,0);
	jointStates_.steerState[i].angPos = joint_->position[k];
	jointStates_.steerState[i].angVel = joint_->velocity[k];
	jointStates_.steerState[i].angAcc = 0;//msg does not have this info
	k = IwsSpSystem::ij2k(i,1);
	jointStates_.wheelState[i].angVel = joint_->velocity[k];
	jointStates_.wheelState[i].angTau = 0;//msg does not have this info
    }
    ///@todo compute body state now estimate from those ones lol
}


void Gui2IwsNode::callbackOdometry ( const nav_msgs::Odometry::ConstPtr& odom_){
    double vx = odom_->twist.twist. linear.x;
    double vy = odom_->twist.twist. linear.y;
    double w  = odom_->twist.twist.angular.z;
    
    double roll, pitch, yaw;
    tf::Quaternion qt = tf::Quaternion ( odom_->pose.pose.orientation.x, 
		                	 odom_->pose.pose.orientation.y, 
					 odom_->pose.pose.orientation.z, 
					 odom_->pose.pose.orientation.w );
    tf::Matrix3x3(qt).getRPY(roll, pitch, yaw);
    
    double vNorm = sqrt(vx*vx + vy*vy);
    if(fabs(vNorm) < 1e-1){//if robot is stationary, current parametric state is estimated along the pre-planned trajectory
// 	if(shouldCurState.size() == 0){ shouldCurState.resize(1); idxShouldCurrState = 0; shouldCurState[0].ICC.rho() = 0.0000001; }
// 	curState = shouldCurState[fmin(idxShouldCurrState,shouldCurState.size() - 1)];
	///@todo compute body state now estimate from those ones lol
    }
    else{//otherwise it is extracted from the robot base state
	bodyStateNow_.state[asInt(IwsSpSystem::BodyStateVars::VRP::V  )] = vNorm;
	bodyStateNow_.state[asInt(IwsSpSystem::BodyStateVars::VRP::RHO)] = - w  /  vNorm ;
	bodyStateNow_.state[asInt(IwsSpSystem::BodyStateVars::VRP::PHI)] = atan2(vy,vx) - M_PI/2.;
    }
}

void Gui2IwsNode::publishJntsCmds () {
    
    if(!new_trajectory){ return; }
    new_trajectory = false;
    
    tuw_gazebo_msgs::IwsCmd_VRAT jnts_cmd;
    jnts_cmd.header.stamp = ros::Time::now();
    jnts_cmd.deltaT = computeBodyStateTargetDeltaT();
    jnts_cmd.v   = bodyStateTarget_.state[asInt(IwsSpSystem::BodyStateVars::VRP::V  )];
    jnts_cmd.rho = bodyStateTarget_.state[asInt(IwsSpSystem::BodyStateVars::VRP::RHO)];
    jnts_cmd.phi = bodyStateTarget_.state[asInt(IwsSpSystem::BodyStateVars::VRP::PHI)];
    
    pub_jnts_cmd_.publish ( jnts_cmd );
}

