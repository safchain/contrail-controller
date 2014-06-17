/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/namespace_manager.h"

#include <boost/bind.hpp>
#include <sys/wait.h>
#include "db/db.h"
#include "io/event_manager.h"
#include "oper/service_instance.h"
#include "cmn/agent_signal.h"

using boost::uuids::uuid;

NamespaceManager::NamespaceManager(EventManager *evm)
        : si_table_(NULL),
          listener_id_(DBTableBase::kInvalidId),
          errors_(*(evm->io_service())) {
    InitSigHandler();
}

void NamespaceManager::Initialize(DB *database, const std::string &netns_cmd) {
    si_table_ = database->FindTable("db.service-instance.0");
    assert(si_table_);
    listener_id_ = si_table_->Register(
        boost::bind(&NamespaceManager::EventObserver, this, _1, _2));

    netns_cmd_ = netns_cmd;
    if (netns_cmd_.length() == 0) {
        LOG(ERROR, "Path for network namespace command not specified"
                   "in the config file");
    }
}

void NamespaceManager::HandleSigChild(const boost::system::error_code &error, int sig, pid_t pid, int status) {
    switch(sig) {
    case SIGCHLD:
        NamespaceState *state = GetState(pid);
        if (state != NULL) {
            state->set_status(status);
        }
        break;
    }
}

NamespaceState *NamespaceManager::GetState(pid_t pid) {
    NamespaceStatePidMap::const_iterator it = namespace_state_pid_map_.find(pid);
    if (it != namespace_state_pid_map_.end()) {
        return it->second;
    }
    return NULL;
}

void NamespaceManager::RemoveState(pid_t pid) {
    NamespaceStatePidMap::const_iterator it = namespace_state_pid_map_.find(pid);
    if (it != namespace_state_pid_map_.end()) {
        delete it->second;
        namespace_state_pid_map_.erase(pid);
    }
}

void NamespaceManager::InitSigHandler() {
    Agent *agent = Agent::GetInstance();
    AgentSignal::SignalChildHandler handler = boost::bind(&NamespaceManager::HandleSigChild, this, _1, _2, _3, _4);
    agent->agent_signal()->RegisterHandler(handler);
}

void NamespaceManager::Terminate() {
    si_table_->Unregister(listener_id_);
}

void NamespaceManager::ReadErrors(const boost::system::error_code &ec,
                      size_t read_bytes, pid_t pid) {
    if (read_bytes) {
        errors_data_ << rx_buff_;
    }

    if (ec) {
        boost::system::error_code close_ec;
        errors_.close(close_ec);

        std::string errors = errors_data_.str();
        if (errors.length() > 0) {
            LOG(ERROR, errors);

            NamespaceState *state = GetState(pid);
            if (state != NULL) {
                state->set_last_errrors(errors);
            }
        }
        errors_data_.clear();
    } else {
        bzero(rx_buff_, sizeof(rx_buff_));
        boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
                boost::bind(&NamespaceManager::ReadErrors, this, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, pid));
    }
}

void NamespaceManager::ExecCmd(const std::string &cmd,
        NamespaceState *state) {
    std::vector<std::string> argv;

    LOG(DEBUG, "Start a NetNS command: " << cmd);

    state->set_last_cmd(cmd);

    argv.push_back("/bin/sh");
    argv.push_back("-c");

    boost::split(argv, cmd, boost::is_any_of(" "), boost::token_compress_on);

    std::vector<const char *> c_argv(argv.size() + 1);
    for (std::size_t i = 0; i != argv.size(); ++i) {
        c_argv[i] = argv[i].c_str();
    }

    int err[2];
    if (pipe(err) < 0) {
        return;
    }

    pid_t pid = vfork();
    if (pid == 0) {
        close(err[0]);
        dup2(err[1], STDERR_FILENO);
        close(err[1]);

        close(STDOUT_FILENO);
        close(STDIN_FILENO);

        execvp(c_argv[0], (char **) c_argv.data());
        perror("execvp");

        exit(127);
    }
    close(err[1]);

    state->set_pid(pid);
    namespace_state_pid_map_.insert(NamespaceStatePidPair(pid, state));

    boost::system::error_code ec;
    errors_.assign(::dup(err[0]), ec);
    close(err[0]);
    if (ec) {
        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
            boost::bind(&NamespaceManager::ReadErrors, this, boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred, pid));
}

void NamespaceManager::StartNetNS(
    const ServiceInstance *svc_instance) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " start";

    const ServiceInstance::Properties &props = svc_instance->properties();
    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(props.vmi_inside);
    cmd_str << " " << UuidToString(props.vmi_outside);
    cmd_str << " --ip_inside " << props.ip_addr_inside;
    cmd_str << " --ip_outside " << props.ip_addr_outside;
    cmd_str << " --mac_inside " << props.mac_addr_inside;
    cmd_str << " --mac_outside " << props.mac_addr_outside;


    NamespaceState *state = new NamespaceState();
    state->set_svc_instance(svc_instance);



    ExecCmd(cmd_str.str(), state);
}

void NamespaceManager::StopNetNS(
    const ServiceInstance *svc_instance) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " stop ";

    const ServiceInstance::Properties &props = svc_instance->properties();
    if (props.instance_id.is_nil() ||
        props.vmi_inside.is_nil() ||
        props.vmi_outside.is_nil()) {
        return;
    }

    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(props.vmi_inside);
    cmd_str << " " << UuidToString(props.vmi_outside);

    NamespaceState *state = new NamespaceState();
    state->set_svc_instance(svc_instance);

    ExecCmd(cmd_str.str(), state);
}

void NamespaceManager::EventObserver(
    DBTablePartBase *db_part, DBEntryBase *entry) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);

    bool usable = !svc_instance->IsDeleted() && svc_instance->IsUsable();
    if (usable) {
        StartNetNS(svc_instance);
    } else {
        StopNetNS(svc_instance);
    }
}

/*
 * NamespaceState class
 */
NamespaceState::NamespaceState() : pid_(0), svc_instance_(NULL), status_(0) {
}
