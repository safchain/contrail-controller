/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_SERVICE_INSTANCE_H__
#define __AGENT_OPER_SERVICE_INSTANCE_H__

#include <map>
#include <boost/uuid/uuid.hpp>
#include "cmn/agent_db.h"

class ServiceInstanceKey : public AgentKey {
  public:
    ServiceInstanceKey(boost::uuids::uuid uuid) {
        uuid_ = uuid;
    }
    const boost::uuids::uuid &instance_id() const {
        return uuid_;
    }

  private:
    boost::uuids::uuid uuid_;
};

class ServiceInstance : public AgentRefCount<ServiceInstance>,
    public AgentDBEntry {
public:
    enum ServiceType {
        Other   = 1,
        SourceNAT,
        LoadBalancer,
    };
    enum VirtualizationType {
        VirtualMachine  = 1,
        NetworkNamespace,
    };

    ServiceInstance();
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    virtual uint32_t GetRefCount() const {
        return AgentRefCount<ServiceInstance>::GetRefCount();
    }

private:
    boost::uuids::uuid uuid_;

    /* template parameters */
    int service_type_;
    int virtualization_type_;

    /* virtual machine */
    boost::uuids::uuid instance_id_;

    /* interfaces */
    boost::uuids::uuid vmi_inside_;
    boost::uuids::uuid vmi_outside_;

    DISALLOW_COPY_AND_ASSIGN(ServiceInstance);
};

class ServiceInstanceTable : public AgentDBTable {
 public:
    ServiceInstanceTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    /*
     * IFNodeToReq
     *
     * Convert the ifmap node to a (key,data) pair stored in the database.
     */
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

 private:
    DISALLOW_COPY_AND_ASSIGN(ServiceInstanceTable);
};

#endif
