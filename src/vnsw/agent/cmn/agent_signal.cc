/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "agent_signal.h"

#include <boost/bind.hpp>
#include <sys/wait.h>
#include "base/logging.h"
#include "io/event_manager.h"

AgentSignal::AgentSignal(EventManager *evm) : signal_(*(evm->io_service())) {
    Initialize();
}

void AgentSignal::RegisterHandler(SignalHandler handler) {
    callbacks_.push_back(handler);
}

void AgentSignal::Notify(const boost::system::error_code &error, int sig, int pid, int status) {
    for (std::vector<SignalHandler>::iterator it = callbacks_.begin();
         it != callbacks_.end(); ++it) {
        SignalHandler sh = *it;
        sh(error, sig, pid, status);
    }
}

void AgentSignal::HandleSig(const boost::system::error_code &error, int sig) {
    if (!error) {
        int status = 0;
        pid_t pid = 0;

        switch(sig) {
        case SIGCHLD:
            while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0) {
                Notify(error, sig, pid, status);
            }
            break;
        default:
            Notify(error, sig, pid, status);
        }
        RegisterSigHandler();
    }
}

void AgentSignal::RegisterSigHandler() {
    signal_.async_wait(boost::bind(&AgentSignal::HandleSig, this, _1, _2));
}

void AgentSignal::Initialize() {
    boost::system::error_code ec;

    /*
     * FIX(safchain) currently only handling SIGCHLD
     */
    signal_.add(SIGCHLD, ec);
    if (ec) {
        LOG(ERROR, "SIGCHLD registration failed");
    }
    RegisterSigHandler();
}

void AgentSignal::Terminate() {
    boost::system::error_code ec;
    signal_.cancel(ec);
}
