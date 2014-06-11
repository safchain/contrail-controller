#include "oper/namespace_manager.h"

#include <boost/bind.hpp>
#include "db/db.h"
#include "cmn/agent.h"
#include "cmn/agent_param.h"
#include "oper/service_instance.h"

NamespaceManager::NamespaceManager(Agent *agent)
        : agent_(agent),
          si_table_(NULL),
          listener_id_(DBTableBase::kInvalidId) {
}

void NamespaceManager::Initialize() {
    DB *database = agent_->GetDB();
    si_table_ = database->FindTable("db.service-instance.0");
    assert(si_table_);
    listener_id_ = si_table_->Register(
        boost::bind(&NamespaceManager::EventObserver, this, _1, _2));
}

void NamespaceManager::Terminate() {
    si_table_->Unregister(listener_id_);
}

static void ExecCmd(std::string cmd) {
    /*
     * TODO(safchain) start async process
     */
    std::cout << "Start NetNS Script: " << cmd << std::endl;
}

void NamespaceManager::StartNetworkNamespace(
    const ServiceInstance *svc_instance, bool restart) {
    std::stringstream cmd_str;

    std::string cmd = agent_->params()->si_netns_command();
    if (cmd.length() == 0) {
        LOG(DEBUG, "Path for network namespace service instance not specified"
                "in the config file");
        return;
    }
    cmd_str << cmd;

    if (restart) {
        cmd_str << " restart ";
    }
    else {
        cmd_str << " start ";
    }

    const ServiceInstance::Properties &props = svc_instance->properties();

    cmd_str << " --instance_id " << UuidToString(props.instance_id);
    cmd_str << " --vmi_inside " << UuidToString(props.vmi_inside);
    cmd_str << " --vmi_outside " << UuidToString(props.vmi_outside);
    cmd_str << " --service_type " << props.ServiceTypeString();

    ExecCmd(cmd_str.str());
}

void NamespaceManager::StopNetworkNamespace(
    const ServiceInstance *svc_instance) {
    std::stringstream cmd_str;

    std::string cmd = agent_->params()->si_netns_command();
    if (cmd.length() == 0) {
        LOG(DEBUG, "Path for network namespace service instance not specified"
                "in the config file");
        return;
    }
    cmd_str << cmd;

    const ServiceInstance::Properties &props = svc_instance->properties();

    cmd_str << " stop ";
    cmd_str << " --instance_id " << UuidToString(props.instance_id);

    ExecCmd(cmd_str.str());
}

void NamespaceManager::EventObserver(
    DBTablePartBase *db_part, DBEntryBase *entry) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);

    bool usable = !svc_instance->IsDeleted() && svc_instance->IsUsable();
    if (usable) {
        StartNetworkNamespace(svc_instance, false);
    } else {
        StopNetworkNamespace(svc_instance);
    }
}
