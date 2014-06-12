/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "service_instance.h"

#include "ifmap/ifmap_node.h"
#include "schema/vnc_cfg_types.h"

#include "oper/ifmap_dependency_manager.h"
#include "oper/operdb_init.h"
#include <cfg/cfg_init.h>
#include <cmn/agent.h>
#include <cmn/agent_param.h>

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

class ServiceInstanceTypesMapping {
public:
    static const std::string kOtherType;
    static ServiceInstance::ServiceType StrServiceTypeToInt(
        const std::string &type);
    static const std::string &IntServiceTypeToStr(
        const ServiceInstance::ServiceType &type);
    static ServiceInstance::VirtualizationType StrVirtualizationTypeToInt(
        const std::string &type);

private:
    typedef std::map<std::string, int> StrTypeToIntMap;
    typedef std::pair<std::string, int> StrTypeToIntPair;
    static StrTypeToIntMap service_type_map_;
    static StrTypeToIntMap virtualization_type_map_;

    static StrTypeToIntMap InitServiceTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("source-nat", ServiceInstance::SourceNAT));
        types.insert(StrTypeToIntPair("load-balancer", ServiceInstance::LoadBalancer));

        return types;
    };

    static StrTypeToIntMap InitVirtualizationTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("virtual-machine", ServiceInstance::VirtualMachine));
        types.insert(StrTypeToIntPair("network-namespace", ServiceInstance::NetworkNamespace));

        return types;
    };
};

static uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

static bool IsNodeType(IFMapNode *node, const char *node_typename) {
    return (strcmp(node->table()->Typename(), node_typename) == 0);
}

/*
 * Walks through the graph starting from the service instance in order to
 * find the Virtual Machine associated. Set the vm_id of the ServiceInstanceData
 * object and return the VM node.
 */
static IFMapNode *FindAndSetVirtualMachine(
    DBGraph *graph, IFMapNode *si_node,
    ServiceInstance::Properties *properties) {

    for (DBGraphVertex::adjacency_iterator iter = si_node->begin(graph);
         iter != si_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "virtual-machine")) {
            autogen::VirtualMachine *vm =
                    static_cast<autogen::VirtualMachine *>(adj->GetObject());
            properties->instance_id = IdPermsGetUuid(vm->id_perms());
            return adj;
        }
    }
    return NULL;
}

static std::string FindNetworkName(DBGraph *graph, IFMapNode *vmi_node) {
    /*
     * Lookup for VirtualNetwork nodes
     */
    for (DBGraphVertex::adjacency_iterator iter = vmi_node->begin(graph);
         iter != vmi_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "virtual-network")) {
            return adj->name();
        }
    }
    return std::string();
}

static void FindAndSetInterfaces(
    DBGraph *graph, IFMapNode *vm_node,
    const std::string &left, const std::string &right,
    ServiceInstance::Properties *properties) {

    /*
     * Lookup for VMI nodes
     */
    for (DBGraphVertex::adjacency_iterator iter = vm_node->begin(graph);
         iter != vm_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (!IsNodeType(adj, "virtual-machine-interface")) {
            continue;
        }
        autogen::VirtualMachineInterface *vmi =
                static_cast<autogen::VirtualMachineInterface *>(
                    adj->GetObject());
        std::string netname = FindNetworkName(graph, adj);
        if (netname == left) {
            properties->vmi_inside = IdPermsGetUuid(vmi->id_perms());
        } else if (netname == right) {
            properties->vmi_outside = IdPermsGetUuid(vmi->id_perms());
        }
    }
}

/*
 * Walks through the graph in order to get the template associated to the
 * Service Instance Node and set the types in the ServiceInstanceData object.
 */
static void FindAndSetTypes(DBGraph *graph, IFMapNode *si_node,
                            ServiceInstance::Properties *properties) {
    IFMapNode *st_node = NULL;

    for (DBGraphVertex::adjacency_iterator iter = si_node->begin(graph);
         iter != si_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "service-template")) {
            st_node = adj;
            break;
        }
    }

    if (st_node == NULL) {
        return;
    }

    autogen::ServiceTemplate *svc_template =
            static_cast<autogen::ServiceTemplate *>(st_node->GetObject());
    autogen::ServiceTemplateType svc_template_props =
            svc_template->properties();

    ServiceInstance::ServiceType service_type =
            ServiceInstanceTypesMapping::StrServiceTypeToInt(
                svc_template_props.service_type);
    properties->service_type = service_type;

    /*
    * TODO(safchain) waiting for the edouard's patch merge
    */
    /*int virtualization_type = ServiceInstanceTypeMapping::StrServiceTypeToInt(
       svc_template_props.service_virtualization_type);*/
    ServiceInstance::VirtualizationType virtualization_type
            = ServiceInstance::NetworkNamespace;
    properties->virtualization_type = virtualization_type;
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

const std::string &ServiceInstance::Properties::ServiceTypeString() const {
    return ServiceInstanceTypesMapping::IntServiceTypeToStr(
        static_cast<ServiceType>(service_type));
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

bool ServiceInstance::IsUsable() const {
    return (!properties_.instance_id.is_nil() &&
            !properties_.vmi_inside.is_nil() &&
            !properties_.vmi_outside.is_nil());
}

void ServiceInstance::CalculateProperties(
    DBGraph *graph, Properties *properties) {
    properties->Clear();

    FindAndSetTypes(graph, node_, properties);

    IFMapNode *vm_node = FindAndSetVirtualMachine(graph, node_, properties);
    if (vm_node == NULL) {
        return;
    }

    autogen::ServiceInstance *svc_instance =
                 static_cast<autogen::ServiceInstance *>(node_->GetObject());
    const autogen::ServiceInstanceType &si_properties =
            svc_instance->properties();
    FindAndSetInterfaces(graph, vm_node,
                         si_properties.left_virtual_network,
                         si_properties.right_virtual_network,
                         properties);
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
    svc_instance->CalculateProperties(agent()->cfg()->cfg_graph(), &properties);
    if (properties.CompareTo(svc_instance->properties()) != 0) {
        std::auto_ptr<DBRequest> request(new DBRequest());
        request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request->key = svc_instance->GetDBRequestKey();
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

/*
 * ServiceInstanceTypeMapping class
 */
ServiceInstanceTypesMapping::StrTypeToIntMap
ServiceInstanceTypesMapping::service_type_map_ = InitServiceTypeMap();
ServiceInstanceTypesMapping::StrTypeToIntMap
ServiceInstanceTypesMapping::virtualization_type_map_ = InitVirtualizationTypeMap();
const std::string ServiceInstanceTypesMapping::kOtherType = "Other";

ServiceInstance::ServiceType ServiceInstanceTypesMapping::StrServiceTypeToInt(
        const std::string &type) {
    StrTypeToIntMap::const_iterator it = service_type_map_.find(type);
    if (it != service_type_map_.end()) {
        return static_cast<ServiceInstance::ServiceType>(it->second);
    }
    return ServiceInstance::Other;
}

ServiceInstance::VirtualizationType ServiceInstanceTypesMapping::StrVirtualizationTypeToInt(
        const std::string &type) {
    StrTypeToIntMap::const_iterator it = virtualization_type_map_.find(type);
    if (it != virtualization_type_map_.end()) {
        return static_cast<ServiceInstance::VirtualizationType>(it->second);
    }
    return ServiceInstance::VirtualMachine;
}

const std::string &ServiceInstanceTypesMapping::IntServiceTypeToStr(
    const ServiceInstance::ServiceType &type) {
    StrTypeToIntMap::const_iterator it = service_type_map_.begin();
    if (it != service_type_map_.end()) {
        if (it->second == type) {
            return it->first;
        }
    }
    return kOtherType;
}
