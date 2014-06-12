/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/namespace_manager.h"

#include <boost/bind.hpp>
#include <sys/wait.h>
#include "db/db.h"
#include "io/event_manager.h"
#include "oper/service_instance.h"

NamespaceManager::NamespaceManager(EventManager *evm)
        : si_table_(NULL),
          listener_id_(DBTableBase::kInvalidId),
          signal_(*(evm->io_service())),
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

void NamespaceManager::HandleSigChild(const boost::system::error_code& error, int sig) {
    if (!error) {
        int status;
        while (::waitpid(-1, &status, WNOHANG) > 0);
        RegisterSigHandler();
    }
}

void NamespaceManager::RegisterSigHandler() {
    signal_.async_wait(boost::bind(&NamespaceManager::HandleSigChild, this, _1, _2));
}

void NamespaceManager::InitSigHandler() {
    boost::system::error_code ec;
    signal_.add(SIGCHLD, ec);
    if (ec) {
        LOG(ERROR, "SIGCHLD registration failed");
    }
    RegisterSigHandler();
}

void NamespaceManager::Terminate() {
    si_table_->Unregister(listener_id_);
    boost::system::error_code ec;
    signal_.cancel(ec);
}

void NamespaceManager::ReadErrors(const boost::system::error_code &ec,
                      size_t read_bytes) {
    if (read_bytes) {
        errors_data_ << rx_buff_;
    }

    if (ec) {
        boost::system::error_code close_ec;
        errors_.close(close_ec);

        std::string errors = errors_data_.str();
        if (errors.length() > 0) {
            LOG(ERROR, errors);
        }
        errors_data_.clear();
    } else {
        bzero(rx_buff_, sizeof(rx_buff_));
        boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
                boost::bind(&NamespaceManager::ReadErrors, this, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }
}

void NamespaceManager::ExecCmd(const std::string cmd) {
    std::vector<std::string> argv;

    LOG(DEBUG, "Start a NetNS command: " << cmd);

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

    if (vfork() == 0) {
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

    boost::system::error_code ec;
    errors_.assign(::dup(err[0]), ec);
    close(err[0]);
    if (ec) {
        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
            boost::bind(&NamespaceManager::ReadErrors, this, boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void NamespaceManager::StartNetNS(
    const ServiceInstance *svc_instance) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " start ";

    const ServiceInstance::Properties &props = svc_instance->properties();
    cmd_str << " --instance_id " << UuidToString(props.instance_id);
    cmd_str << " --vmi_inside " << UuidToString(props.vmi_inside);
    cmd_str << " --vmi_outside " << UuidToString(props.vmi_outside);
    cmd_str << " --service_type " << props.ServiceTypeString();

    ExecCmd(cmd_str.str());
}

void NamespaceManager::StopNetNS(
    const ServiceInstance *svc_instance) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " stop ";

    const ServiceInstance::Properties &props = svc_instance->properties();
    if (props.instance_id.is_nil()) {
        return;
    }

    cmd_str << " --instance_id " << UuidToString(props.instance_id);

    ExecCmd(cmd_str.str());
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
