/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_NAMESPACE_MANAGER_H__
#define __AGENT_OPER_NAMESPACE_MANAGER_H__

#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include "db/db_table.h"

class DB;
class EventManager;
class ServiceInstance;
class NamespaceState;

/*
 * Starts and stops network namespaces corresponding to service-instances.
 */
class NamespaceManager {
public:
    typedef std::map<const pid_t, const NamespaceState &> NamespaceStatePidMap;
    typedef std::pair<const pid_t, const NamespaceState &> NamespaceStatePidPair;
    static const size_t kBufLen = 4098;

    NamespaceManager(EventManager *evm);

    void Initialize(DB *database, const std::string &netns_cmd);
    void Terminate();

    void HandleSigChild(const boost::system::error_code& error, int sig);

private:
    void ExecCmd(const std::string &cmd, NamespaceState &state);
    void StartNetNS(const ServiceInstance *svc_instance);
    void StopNetNS(const ServiceInstance *svc_instance);
    void RegisterSigHandler();
    void InitSigHandler();
    void ReadErrors(const boost::system::error_code &ec, size_t read_bytes,
            NamespaceState &state);

    /*
     * Event observer for changes in the "db.service-instance.0" table.
     */
    void EventObserver(DBTablePartBase *db_part, DBEntryBase *entry);

    DBTableBase *si_table_;
    DBTableBase::ListenerId listener_id_;
    std::string netns_cmd_;
    boost::asio::signal_set signal_;
    boost::asio::posix::stream_descriptor errors_;
    std::stringstream errors_data_;
    char rx_buff_[kBufLen];

    NamespaceStatePidMap namespace_state_pid_map_;
};

#endif
