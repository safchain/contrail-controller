/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/namespace_manager.h"

#include <boost/bind.hpp>
#include <boost/functional/hash.hpp>
#include <sys/wait.h>
#include "db/db.h"
#include "io/event_manager.h"
#include "oper/service_instance.h"
#include "cmn/agent_signal.h"

using boost::uuids::uuid;

NamespaceManager::NamespaceManager(EventManager *evm, AgentSignal *signal)
        : evm_(evm), si_table_(NULL),
          listener_id_(DBTableBase::kInvalidId) {
    if (signal) {
        InitSigHandler(signal);
    }
}

void NamespaceManager::Initialize(DB *database, const std::string &netns_cmd, const int netns_workers) {
    si_table_ = database->FindTable("db.service-instance.0");
    assert(si_table_);
    listener_id_ = si_table_->Register(
        boost::bind(&NamespaceManager::EventObserver, this, _1, _2));

    netns_cmd_ = netns_cmd;
    if (netns_cmd_.length() == 0) {
        LOG(ERROR, "Path for network namespace command not specified"
                   "in the config file, the namespaces won't be started");
    }

    int workers = 1;
    if (netns_workers > 1) {
       workers = netns_workers;
    }
    task_queues_.resize(workers);
    for (std::vector<TaskQueue *>::iterator iter = task_queues_.begin();
         iter != task_queues_.end(); ++iter) {
        *iter = new TaskQueue();
    }
}

void NamespaceManager::HandleSigChild(const boost::system::error_code &error, int sig, pid_t pid, int status) {
    switch(sig) {
    case SIGCHLD:
        /*
         * check the head of each taskqueue in order to check whether there is
         * a task with the corresponding pid, if present dequeue it.
         */
        for (std::vector<TaskQueue *>::iterator iter = task_queues_.begin();
                 iter != task_queues_.end(); ++iter) {
            TaskQueue *task_queue = *iter;
            if (! task_queue->empty()) {
                NamespaceTask *task = task_queue->front();
                if (task->pid() == pid) {
                    ServiceInstance *svc_instance = GetSvcInstance(task);
                    if (svc_instance) {
                        NamespaceState *state = GetState(svc_instance);
                        if (state != NULL) {
                            state->set_status(status);

                            if (status != 0) {
                                state->set_status_type(NamespaceState::Error);
                            } else if (state->status_type() == NamespaceState::Starting) {
                                state->set_status_type(NamespaceState::Started);
                            } else if (state->status_type() == NamespaceState::Stopping) {
                                state->set_status_type(NamespaceState::Stopped);
                            }
                        }
                    }
                    task_queue->pop();
                    delete task;

                    ScheduleNextTasks();
                    return;
                }
            }
        }
        break;
    }
}

NamespaceState *NamespaceManager::GetState(ServiceInstance *svc_instance) {
    return static_cast<NamespaceState *>(
        svc_instance->GetState(si_table_, listener_id_));
}

void NamespaceManager::SetState(ServiceInstance *svc_instance, NamespaceState *state) {
    svc_instance->SetState(si_table_, listener_id_, state);
}

void NamespaceManager::ClearState(ServiceInstance *svc_instance) {
    svc_instance->ClearState(si_table_, listener_id_);
}

void NamespaceManager::InitSigHandler(AgentSignal *signal) {
    signal->RegisterChildHandler(
        boost::bind(&NamespaceManager::HandleSigChild, this, _1, _2, _3, _4));
}

void NamespaceManager::Terminate() {
    si_table_->Unregister(listener_id_);

    TaskQueue *list;
    for (std::vector<TaskQueue *>::iterator iter = task_queues_.begin();
         iter != task_queues_.end(); ++iter) {
        if ((list = *iter) == NULL) {
            continue;
        }
        *iter = NULL;
        delete list;
    }
}

void  NamespaceManager::Enqueue(NamespaceTask *task, const boost::uuids::uuid &uuid) {
    std::stringstream ss;
    ss << uuid;
    GetTaskQueue(ss.str())->push(task);
    ScheduleNextTasks();
}

NamespaceManager::TaskQueue *NamespaceManager::GetTaskQueue(const std::string &str) {
    boost::hash<std::string> hash;
    int index = hash(str) % task_queues_.capacity();
    return task_queues_[index];
}

void NamespaceManager::ScheduleNextTask(TaskQueue *task_queue) {
    while (! task_queue->empty()) {
        NamespaceTask *task = task_queue->front();
        if (! task->is_running()) {
            bool running = task->Run();
            if (running) {
                return;
            }

            task_queue->pop();
            delete task;
        } else {
            return;
        }
    }
}

void NamespaceManager::ScheduleNextTasks() {
    for (std::vector<TaskQueue *>::iterator iter = task_queues_.begin();
             iter != task_queues_.end(); ++iter) {
        ScheduleNextTask(*iter);
    }
}

ServiceInstance *NamespaceManager::GetSvcInstance(NamespaceTask *task) {
    std::map<NamespaceTask *, ServiceInstance*>::iterator iter = task_svc_instances_.find(task);
    if (iter != task_svc_instances_.end()) {
        return iter->second;
    }
    return NULL;
}

void NamespaceManager::RegisterSvcInstance(NamespaceTask *task, ServiceInstance *svc_instance) {
    task_svc_instances_.insert(std::make_pair(task, svc_instance));
}

void NamespaceManager::UnRegisterSvcInstance(ServiceInstance *svc_instance) {
    for (std::map<NamespaceTask *, ServiceInstance*>::iterator iter = task_svc_instances_.begin();
         iter != task_svc_instances_.end(); ++iter) {
        if (svc_instance == iter->second) {
            task_svc_instances_.erase(iter);
        }
    }
}

void NamespaceManager::StartNetNS(ServiceInstance *svc_instance, NamespaceState *state, bool update) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " create";

    const ServiceInstance::Properties &props = svc_instance->properties();
    cmd_str << " source-nat ";// << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(props.vmi_inside);
    cmd_str << " " << UuidToString(props.vmi_outside);
    cmd_str << " --vmi-left-ip " << props.ip_addr_inside << "/" << props.ip_prefix_len_inside;
    cmd_str << " --vmi-right-ip " << props.ip_addr_outside << "/" << props.ip_prefix_len_outside;
    cmd_str << " --vmi-left-mac " << props.mac_addr_inside;
    cmd_str << " --vmi-right-mac " << props.mac_addr_outside;

    if (update) {
        cmd_str << " --update";
    }
    state->set_properties(props);

    state->Clear();
    state->set_last_cmd(cmd_str.str());
    state->set_status_type(NamespaceState::Starting);

    NamespaceTask *task = new NamespaceTask(cmd_str.str(), evm_);
    task->set_on_error_cb(boost::bind(&NamespaceManager::OnError, this, _1, _2));

    RegisterSvcInstance(task, svc_instance);
    boost::uuids::uuid uuid = svc_instance->properties().instance_id;
    Enqueue(task, uuid);
}

void NamespaceManager::OnError(NamespaceTask *task, const std::string errors) {
    ServiceInstance *svc_instance = GetSvcInstance(task);
    if (! svc_instance) {
        return;
    }

    NamespaceState *state = GetState(svc_instance);
    if (state != NULL) {
        state->set_last_errors(errors);

        std::cout << "ERROR: " << errors << std::endl;
    }
}

void NamespaceManager::StopNetNS(ServiceInstance *svc_instance, NamespaceState *state) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " destroy";

    const ServiceInstance::Properties &props = state->properties();
    if (props.instance_id.is_nil() ||
        props.vmi_inside.is_nil() ||
        props.vmi_outside.is_nil()) {
        return;
    }

    cmd_str << " source-nat "; //props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(props.vmi_inside);
    cmd_str << " " << UuidToString(props.vmi_outside);

    state->set_last_cmd(cmd_str.str());
    state->set_status_type(NamespaceState::Stopping);

    NamespaceTask *task = new NamespaceTask(cmd_str.str(), evm_);
    boost::uuids::uuid uuid = svc_instance->properties().instance_id;
    Enqueue(task, uuid);
}

void NamespaceManager::EventObserver(
    DBTablePartBase *db_part, DBEntryBase *entry) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);

    NamespaceState *state = GetState(svc_instance);

    bool isUsuable = svc_instance->IsUsable();
    if (! isUsuable && state != NULL &&
        state->status_type() != NamespaceState::Stopping &&
        state->status_type() != NamespaceState::Stopped) {
        StopNetNS(svc_instance, state);
    } else if (isUsuable) {
        if (state == NULL) {
            state = new NamespaceState();
            SetState(svc_instance, state);

            StartNetNS(svc_instance, state, false);
        } else if (state->status_type() == NamespaceState::Error ||
                   ! state->properties().CompareTo(svc_instance->properties())) {
            /*
             * a previous instance has been started but is in a fail state,
             * so try to restart it
             */
            StartNetNS(svc_instance, state, true);
        }
    }
    if (svc_instance->IsDeleted()) {
        if (state) {
            UnRegisterSvcInstance(svc_instance);
            ClearState(svc_instance);
            delete state;
        }
    }
}

/*
 * NamespaceState class
 */
NamespaceState::NamespaceState() : DBState(),
        pid_(0), status_(0), status_type_(0) {
}

void NamespaceState::Clear() {
    pid_ = 0;
    status_ = 0;
    last_errors_.empty();
    last_cmd_.empty();
}

/*
 * NamespaceTask class
 */
NamespaceTask::NamespaceTask(const std::string &cmd, EventManager *evm) :
        cmd_(cmd), errors_(*(evm->io_service())), is_running_(false), pid_(0) {
}

void NamespaceTask::ReadErrors(const boost::system::error_code &ec, size_t read_bytes) {
    if (read_bytes) {
        errors_data_ << rx_buff_;
    }

    if (ec) {
        boost::system::error_code close_ec;
        errors_.close(close_ec);

        std::string errors = errors_data_.str();
        if (errors.length() > 0) {
            LOG(ERROR, errors);

            if (! on_error_cb_.empty()) {
                on_error_cb_(this, errors);
            }
        }
        errors_data_.clear();
    } else {
        bzero(rx_buff_, sizeof(rx_buff_));
        boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
                boost::bind(&NamespaceTask::ReadErrors, this, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }
}

bool NamespaceTask::Run() {
    std::vector<std::string> argv;
    LOG(DEBUG, "Start a NetNS command: " << cmd_);

    std::cout << "Run: " << cmd_ << std::endl;

    is_running_ = true;

    boost::split(argv, cmd_, boost::is_any_of(" "), boost::token_compress_on);
    std::vector<const char *> c_argv(argv.size() + 1);
    for (std::size_t i = 0; i != argv.size(); ++i) {
        c_argv[i] = argv[i].c_str();
    }

    int err[2];
    if (pipe(err) < 0) {
        return false;
    }

    pid_ = vfork();
    if (pid_ == 0) {
        close(err[0]);
        dup2(err[1], STDERR_FILENO);
        close(err[1]);

        close(STDOUT_FILENO);
        close(STDIN_FILENO);

        execvp(c_argv[0], (char **) c_argv.data());
        perror("execvp");

        _exit(127);
    }
    close(err[1]);

    boost::system::error_code ec;
    errors_.assign(::dup(err[0]), ec);
    close(err[0]);
    if (ec) {
        is_running_ = false;
        return false;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
            boost::bind(&NamespaceTask::ReadErrors, this, boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));

    return true;
}
