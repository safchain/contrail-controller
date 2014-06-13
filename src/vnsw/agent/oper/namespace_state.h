/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_NAMESPACE_STATE_H__
#define __AGENT_OPER_NAMESPACE_STATE_H__

#include <unistd.h>
#include <string>

class ServiceInstance;

class NamespaceState {
public:
    NamespaceState();

    void set_svc_instance(const ServiceInstance *svc_instance) { svc_instance_ = svc_instance; }
    const ServiceInstance *svc_instance() const { return svc_instance_; }

    void set_pid(const pid_t &pid) { pid_ = pid; }
    pid_t pid() const { return pid_; }

    void set_status(const int status) { status_ = status; }
    pid_t status() const { return status_; }

    void set_last_errrors(const std::string &errors) { last_errors_ = errors; }
    std::string last_errors() const { return last_errors_; }

    void set_last_cmd(const std::string &cmd) { last_cmd_ = cmd; }
    std::string last_cmd() const { return last_cmd_; }

private:
    pid_t pid_;
    const ServiceInstance *svc_instance_;
    int status_;
    std::string last_errors_;
    std::string last_cmd_;
};

#endif
