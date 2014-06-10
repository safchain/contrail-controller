/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "service_instance.h"

#include "ifmap/ifmap_node.h"
#include "schema/vnc_cfg_types.h"

#include "oper/ifmap_dependency_manager.h"
#include "oper/operdb_init.h"

using boost::uuids::uuid;

/*
 * ServiceInstanceTable create requests contain the IFMapNode that this
 * entry corresponds to.
 */
class ServiceInstanceCreate : public AgentData {
  public:
    ServiceInstanceCreate(IFMapNode *node) :
            node_(node) {
    }
    IFMapNode *node() { return node_; }

  private:
    IFMapNode *node_;
};

/*
 * ServiceInstanceTable update requests contain ServiceInstance Properties.
 */
class ServiceInstanceUpdate : public AgentData {
  public:
    typedef ServiceInstance::Properties Properties;
    ServiceInstanceUpdate(Properties &properties) :
            properties_(properties) {
    }
    const Properties &properties() { return properties_; }

  private:
    Properties properties_;
};

static uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

void ServiceInstance::Properties::Clear() {
    service_type = 0;
    virtualization_type = 0;
    instance_id = boost::uuids::nil_uuid();
    vmi_inside = boost::uuids::nil_uuid();
    vmi_outside = boost::uuids::nil_uuid();
}

template <typename Type>
static int compare(const Type &lhs, const Type &rhs) {
    if (lhs < rhs) {
        return -1;
    }
    if (rhs < lhs) {
        return 1;
    }
    return 0;
}

int ServiceInstance::Properties::CompareTo(const Properties &rhs) const {
    int cmp = 0;
    cmp = compare(service_type, rhs.service_type);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(virtualization_type, rhs.virtualization_type);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(instance_id, rhs.instance_id);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(vmi_inside, rhs.vmi_inside);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(vmi_outside, rhs.vmi_outside);
    if (cmp != 0) {
        return cmp;
    }
    return cmp;
}

/*
 * ServiceInstance class
 */
ServiceInstance::ServiceInstance() {
    properties_.Clear();
}

bool ServiceInstance::IsLess(const DBEntry &rhs) const {
    const ServiceInstance &si = static_cast<const ServiceInstance &>(rhs);
    return uuid_ < si.uuid_;
}

std::string ServiceInstance::ToString() const {
    std::stringstream uuid_str;
    uuid_str << uuid_;
    return uuid_str.str();
}

void ServiceInstance::SetKey(const DBRequestKey *key) {
    const ServiceInstanceKey *si_key =
            static_cast<const ServiceInstanceKey *>(key);
    uuid_ = si_key->instance_id();
}

DBEntryBase::KeyPtr ServiceInstance::GetDBRequestKey() const {
    ServiceInstanceKey *key = new ServiceInstanceKey(uuid_);
    return KeyPtr(key);
}

bool ServiceInstance::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    return false;
}

void ServiceInstance::CalculateProperties(Properties *properties) {
    // TODO
}

/*
 * ServiceInstanceTable class
 */
ServiceInstanceTable::ServiceInstanceTable(DB *db, const std::string &name)
        : AgentDBTable(db, name) {
}

std::auto_ptr<DBEntry> ServiceInstanceTable::AllocEntry(
    const DBRequestKey *key) const {
    std::auto_ptr<DBEntry> entry(new ServiceInstance());
    entry->SetKey(key);
    return entry;
}

DBEntry *ServiceInstanceTable::Add(const DBRequest *request) {
    ServiceInstance *svc_instance = new ServiceInstance();
    svc_instance->SetKey(request->key.get());
    ServiceInstanceCreate *data =
            static_cast<ServiceInstanceCreate *>(request->data.get());
    svc_instance->set_node(data->node());
    IFMapDependencyManager *manager = agent()->oper_db()->dependency_manager();
    manager->SetObject(data->node(), svc_instance);
    return svc_instance;
}

void ServiceInstanceTable::Delete(DBEntry *entry, const DBRequest *request) {
    ServiceInstance *svc_instance  = static_cast<ServiceInstance *>(entry);
    IFMapDependencyManager *manager = agent()->oper_db()->dependency_manager();
    manager->ResetObject(svc_instance->node());
}

bool ServiceInstanceTable::OnChange(DBEntry *entry, const DBRequest *request) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);
    ServiceInstanceUpdate *data =
            static_cast<ServiceInstanceUpdate *>(request->data.get());
    svc_instance->set_properties(data->properties());
    return true;
}

void ServiceInstanceTable::Initialize(Agent *agent) {
    set_agent(agent);
    IFMapDependencyManager *manager = agent->oper_db()->dependency_manager();

    manager->Register(
        "service-instance",
        boost::bind(&ServiceInstanceTable::ChangeEventHandler, this, _1));
}

bool ServiceInstanceTable::IFNodeToReq(IFMapNode *node, DBRequest &request) {
    autogen::ServiceInstance *svc_instance =
            static_cast<autogen::ServiceInstance *>(node->GetObject());
    autogen::IdPermsType id = svc_instance->id_perms();
    request.key.reset(new ServiceInstanceKey(IdPermsGetUuid(id)));
    if (!node->IsDeleted()) {
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.data.reset(new ServiceInstanceCreate(node));
    } else {
        request.oper = DBRequest::DB_ENTRY_DELETE;
    }
    return true;
}

void ServiceInstanceTable::ChangeEventHandler(DBEntry *entry) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);
    ServiceInstance::Properties properties;
    svc_instance->CalculateProperties(&properties);
    if (properties.CompareTo(svc_instance->properties()) != 0) {
        std::auto_ptr<DBRequest> request(new DBRequest());
        request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request->data.reset(new ServiceInstanceUpdate(properties));
        Enqueue(request.release());
    }
}
 
DBTableBase *ServiceInstanceTable::CreateTable(
    DB *db, const std::string &name) {
    ServiceInstanceTable *table = new ServiceInstanceTable(db, name);
    table->Init();
    return table;
}
