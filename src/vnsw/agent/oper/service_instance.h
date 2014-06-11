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

    /*
     * Properties computed from walking the ifmap graph.
     * POD type.
     */
    struct Properties {
        void Clear();
        int CompareTo(const Properties &rhs) const;
        const std::string &ServiceTypeString() const;

        /* template parameters */
        int service_type;
        int virtualization_type;

        /* virtual machine */
        boost::uuids::uuid instance_id;

        /* interfaces */
        boost::uuids::uuid vmi_inside;
        boost::uuids::uuid vmi_outside;
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

    /*
     * Walk the IFMap graph and calculate the properties for this node.
     */
    void CalculateProperties(Properties *properties);

    void set_node(IFMapNode *node) { node_ = node; }

    IFMapNode *node() { return node_; }

    void set_properties(const Properties &properties) {
        properties_ = properties;
    }

    const Properties &properties() const { return properties_; }

    bool IsUsable() const;

private:
    boost::uuids::uuid uuid_;
    IFMapNode *node_;
    Properties properties_;

    DISALLOW_COPY_AND_ASSIGN(ServiceInstance);
};

class ServiceInstanceTable : public AgentDBTable {
 public:
    ServiceInstanceTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    /*
     * Register with the dependency manager.
     */
    void Initialize(Agent *agent);

    /*
     * Add/Delete methods establish the mapping between the IFMapNode
     * and the ServiceInstance DBEntry with the dependency manager.
     */
    virtual DBEntry *Add(const DBRequest *request);
    virtual void Delete(DBEntry *entry, const DBRequest *request);
    virtual bool OnChange(DBEntry *entry, const DBRequest *request);

    /*
     * IFNodeToReq
     *
     * Convert the ifmap node to a (key,data) pair stored in the database.
     */
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

 private:
    /*
     * Invoked by dependency manager whenever a node in the graph
     * changes in a way that it affects the service instance
     * configuration. The dependency tracking policy is configured in
     * the dependency manager directly.
     */
    void ChangeEventHandler(DBEntry *entry);

    DISALLOW_COPY_AND_ASSIGN(ServiceInstanceTable);
};

#endif
