

#include "msgs_utils.h"
#include "RemoteReasoner.h"

#include "bwi_kr_execution/ExecutePlanAction.h"

#include "actasp/reasoners/Clingo.h"
#include "actasp/executors/ReplanningActionExecutor.h"
#include "actasp/ActionExecutor.h"
#include "actasp/AspFluent.h"
#include "actasp/planners/AnyPlan.h"
#include "actasp/ExecutionObserver.h"
#include "actasp/PlanningObserver.h"

#include "actions/ActionFactory.h"
#include "actions/LogicalNavigation.h"

#include <actionlib/server/simple_action_server.h>

#include <ros/ros.h>
#include <ros/console.h>

#include <boost/filesystem.hpp>

#include <string>


using namespace std;
using namespace bwi_krexec;
using namespace actasp;

typedef actionlib::SimpleActionServer<bwi_kr_execution::ExecutePlanAction> Server;


ActionExecutor *executor;

struct PrintFluent {
  
  PrintFluent(ostream& stream) : stream(stream) {}
  
  string operator()(const AspFluent& fluent) {
    stream << fluent.toString() << " ";
  }
  
  ostream &stream;
  
};

struct Observer : public ExecutionObserver, public PlanningObserver {
  
  void actionStarted(const AspFluent& action) throw() {
    ROS_INFO_STREAM("Starting execution: " << action.toString());
  }
  
  void actionTerminated(const AspFluent& action) throw() {
    ROS_INFO_STREAM("Terminating execution: " << action.toString());
  }
  
  
  void planChanged(const AnswerSet& newPlan) throw() {
   stringstream planStream;
   
   ROS_INFO_STREAM("plan size: " << newPlan.getFluents().size());
   
   copy(newPlan.getFluents().begin(),newPlan.getFluents().end(),ostream_iterator<string>(planStream," "));
   
   ROS_INFO_STREAM(planStream.str());
  }
  
  
};

void executePlan(const bwi_kr_execution::ExecutePlanGoalConstPtr& plan, Server* as) {

  ROS_INFO("Invoked");
  vector<AspRule> goalRules;

  transform(plan->aspGoal.begin(),plan->aspGoal.end(),back_inserter(goalRules),TranslateRule());

  executor->setGoal(goalRules);

  ros::Rate loop(10);

  while (!executor->goalReached() && !executor->failed() && ros::ok()) {

    if (!as->isPreemptRequested()) {
      executor->executeActionStep();
    }
    else {
      if(as->isNewGoalAvailable()) {
        goalRules.clear();
        const bwi_kr_execution::ExecutePlanGoalConstPtr& newGoal = as->acceptNewGoal();
        transform(newGoal->aspGoal.begin(),newGoal->aspGoal.end(),back_inserter(goalRules),TranslateRule());
        executor->setGoal(goalRules);
      }
    }
         loop.sleep();
  }


  if (executor->goalReached()) {
    ROS_INFO("Execution succeded");
    as->setSucceeded();
  }
  else {
    ROS_INFO("Execution failed");
    as->setAborted();
  }
}

int main(int argc, char**argv) {
  ros::init(argc, argv, "bwi_action_execution");
  ros::NodeHandle n;

  if (ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Debug)) {
    ros::console::notifyLoggerLevelsChanged();
  }

//  create initial state
  LogicalNavigation setInitialState("noop");
  setInitialState.run();

  ros::NodeHandle privateNode("~");
  bool simulating;
  privateNode.param<bool>("simulation",simulating,false);
  ActionFactory::setSimulation(simulating); //parametrize this

  AspKR *reasoner = new RemoteReasoner();
  Planner *planner = new AnyPlan(reasoner,1.5);
  
  //need a pointer to the specific type for the observer
  ReplanningActionExecutor *replanner = new ReplanningActionExecutor(reasoner,planner,ActionFactory::actions());
  executor = replanner;
  
  Observer observer;
  executor->addExecutionObserver(&observer);
  replanner->addPlanningObserver(&observer);

  Server server(n, "execute_plan", boost::bind(&executePlan, _1, &server), false);
  server.start();

  ros::spin();

  
  return 0;
}