/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_NAMESPACE_MANAGER_H__
#define __AGENT_OPER_NAMESPACE_MANAGER_H__

#include <queue>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include "db/db_table.h"
#include "cmn/agent_signal.h"
#include "db/db_entry.h"
#include "oper/service_instance.h"

class DB;
class EventManager;
class NamespaceState;
class NamespaceTask;


/*
 * Starts and stops network namespaces corresponding to service-instances.
 */
class NamespaceManager {
public:
    typedef std::queue<NamespaceTask *> TaskQueue;
    NamespaceManager(EventManager *evm, AgentSignal *signal);

    void Initialize(DB *database, const std::string &netns_cmd, const int netns_workers);
    void Terminate();

private:
    friend class NamespaceManagerTest;

    void HandleSigChild(const boost::system::error_code& error, int sig, pid_t pid, int status);
    void RegisterSigHandler();
    void InitSigHandler(AgentSignal *signal);
    void StartNetNS(ServiceInstance *svc_instance, NamespaceState *state, bool update);
    void StopNetNS(ServiceInstance *svc_instance, NamespaceState *state);
    void OnError(NamespaceTask *task, const std::string errors);
    void RegisterSvcInstance(NamespaceTask *task, ServiceInstance *svc_instance);
    void UnRegisterSvcInstance(ServiceInstance *svc_instance);
    ServiceInstance *GetSvcInstance(NamespaceTask *task);

    TaskQueue *GetTaskQueue(const std::string &str);
    void Enqueue(NamespaceTask *task, const boost::uuids::uuid &uuid);
    void ScheduleNextTask(TaskQueue *task_queue);
    void ScheduleNextTasks();

    NamespaceState *GetState(ServiceInstance *);
    void SetState(ServiceInstance *svc_instance, NamespaceState *state);
    void ClearState(ServiceInstance *svc_instance);

    /*
     * Event observer for changes in the "db.service-instance.0" table.
     */
    void EventObserver(DBTablePartBase *db_part, DBEntryBase *entry);

    EventManager *evm_;
    DBTableBase *si_table_;
    DBTableBase::ListenerId listener_id_;
    std::string netns_cmd_;

    std::vector<TaskQueue *> task_queues_;
    std::map<NamespaceTask *, ServiceInstance *> task_svc_instances_;
};

class NamespaceState : public DBState {

public:
    enum StatusType {
        Starting = 1,
        Started,
        Stopping,
        Stopped,
        Error
    };

    NamespaceState();

    void set_pid(const pid_t &pid) { pid_ = pid; }
    pid_t pid() const { return pid_; }

    void set_status(const int status) { status_ = status; }
    pid_t status() const { return status_; }

    void set_last_errors(const std::string &errors) { last_errors_ = errors; }
    std::string last_errors() const { return last_errors_; }

    void set_last_cmd(const std::string &cmd) { last_cmd_ = cmd; }
    std::string last_cmd() const { return last_cmd_; }

    void set_properties(const ServiceInstance::Properties &properties) {
        properties_ = properties;
    }
    const ServiceInstance::Properties &properties() const { return properties_; }

    void set_status_type(const int status) { status_type_ = status; }
    int status_type() const { return status_type_; }

    void Clear();

private:
    pid_t pid_;
    int status_;
    std::string last_errors_;
    std::string last_cmd_;
    int status_type_;

    ServiceInstance::Properties properties_;

    boost::system::error_code ec_;
};

class NamespaceTask {
public:
    static const size_t kBufLen = 4098;
    typedef boost::function<void (NamespaceTask *task, const std::string errors)> OnErrorCallback;

    NamespaceTask(const std::string &cmd, EventManager *evm);

    void ReadErrors(const boost::system::error_code &ec, size_t read_bytes);
    bool Run();

    bool is_running() const { return is_running_; }
    pid_t pid() const { return pid_; }

    void set_on_error_cb(OnErrorCallback cb) { on_error_cb_ = cb; }

private:
    const std::string cmd_;

    boost::asio::posix::stream_descriptor errors_;
    std::stringstream errors_data_;
    char rx_buff_[kBufLen];
    AgentSignal::SignalChildHandler sig_handler_;

    bool is_running_;
    pid_t pid_;

    OnErrorCallback on_error_cb_;

    /*
     * TODO(safchain) add a start time here to be able to add a timeout mechanism
     */
};

#endif
