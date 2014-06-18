/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_NAMESPACE_MANAGER_H__
#define __AGENT_OPER_NAMESPACE_MANAGER_H__

#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include "db/db_table.h"
#include "cmn/agent_signal.h"

class AgentSignal;
class DB;
class EventManager;
class ServiceInstance;
class NamespaceState;

/*
 * Starts and stops network namespaces corresponding to service-instances.
 */
class NamespaceManager {
public:
    typedef std::map<const pid_t, NamespaceState *> NamespaceStatePidMap;
    typedef std::pair<const pid_t, NamespaceState *> NamespaceStatePidPair;
    static const size_t kBufLen = 4098;

    NamespaceManager(EventManager *evm, AgentSignal *signal);

    void Initialize(DB *database, const std::string &netns_cmd);
    void Terminate();

    void HandleSigChild(const boost::system::error_code& error, int sig, pid_t pid, int status);

private:
    void ExecCmd(const std::string &cmd, NamespaceState *state);
    void StartNetNS(const ServiceInstance *svc_instance);
    void StopNetNS(const ServiceInstance *svc_instance);

    void InitSigHandler(AgentSignal *signal);
    void ReadErrors(const boost::system::error_code &ec, size_t read_bytes, pid_t pid);
    NamespaceState *GetState(pid_t pid);
    void RemoveState(pid_t pid);

    /*
     * Event observer for changes in the "db.service-instance.0" table.
     */
    void EventObserver(DBTablePartBase *db_part, DBEntryBase *entry);

    DBTableBase *si_table_;
    DBTableBase::ListenerId listener_id_;
    std::string netns_cmd_;
    boost::asio::posix::stream_descriptor errors_;
    std::stringstream errors_data_;
    char rx_buff_[kBufLen];
    NamespaceStatePidMap namespace_state_pid_map_;
};

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

    boost::system::error_code ec_;
};

#endif
